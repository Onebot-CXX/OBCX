#include "standard_query_codec.hpp"

#include "standard_service_codec.hpp"

#include <charconv>
#include <initializer_list>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace obcx::onebot11::bot::standard {
namespace detail {
// Borrowed-only access lets native encoding retain its caller's single ledger.
// No selector export/copy or independently reset budget is involved.
class QuerySelectorAccess final {
public:
  static auto message_id(const MessageSelector &value) noexcept
      -> std::int32_t {
    return value.message_id_;
  }
  static auto resource(const ForwardSelector &value) noexcept
      -> const std::string & {
    return value.resource_id_;
  }
};
} // namespace detail
namespace {
namespace domain = obcx::bot;
using Json = detail::QueryJson;
using Codec = detail::QueryNativeCodec;

[[noreturn]] void malformed() {
  throw std::invalid_argument("invalid standard query value");
}
[[noreturn]] void exceeded() {
  throw std::invalid_argument("standard query value exceeds bound");
}
void installation(const domain::BotInstallationRef &value) {
  value.validate();
  if (value.surface.value() != "onebot11.qq")
    malformed();
}
void installation(const domain::BotInstallationRef &value, const Codec &codec) {
  installation(value);
  codec.validate(value);
}

// Domain-style accounting: every tree node, string and object key is charged,
// including unknown fields. The entire retained projection is preflighted in
// that SAME budget before allocation. DTO fields have no hypothetical JSON
// keys; wire/framing accounting is separate. Each representation has root depth
// zero.
class QueryBudget {
public:
  explicit QueryBudget(const ValueLimits &limits) : limits_(limits) {
    limits.validate();
  }
  void bytes(std::size_t count) {
    if (count > limits_.maximum_bytes - bytes_ ||
        count > domain::detail::domain_maximum_bytes - bytes_)
      exceeded();
    bytes_ += count;
  }
  void node(std::size_t depth) {
    if (depth > limits_.maximum_depth || depth > 32 ||
        nodes_ == limits_.maximum_nodes ||
        nodes_ == domain::detail::domain_maximum_items)
      exceeded();
    ++nodes_;
  }
  void text_size(std::size_t size) {
    if (size > limits_.maximum_text_bytes)
      exceeded();
    bytes(size);
  }
  void text(std::string_view value) {
    text_size(value.size());
    domain::detail::validate_utf8(value);
  }
  void inspect(const Json &value, std::size_t depth = 0) {
    node(depth);
    if (value.is_string()) {
      text(value.get_ref<const std::string &>());
    } else if (value.is_structured()) {
      for (auto it = value.begin(); it != value.end(); ++it) {
        if (value.is_object())
          text(it.key());
        inspect(it.value(), depth + 1);
      }
    } else if (value.is_binary() || value.is_discarded()) {
      malformed();
    }
  }
  void scope(const domain::BotInstallationRef &value, std::size_t depth) {
    node(depth);
    node(depth + 1);
    text(value.installation_id);
    node(depth + 1);
    text(value.surface.value());
  }
  template <class Ref> void reference(const Ref &value, std::size_t depth) {
    node(depth);
    scope(value.installation, depth + 1);
    node(depth + 1);
    text(value.id.serialized());
  }

  void native_reference(const domain::BotInstallationRef &value,
                        std::int64_t id, std::string_view tag,
                        std::string_view kind, std::size_t depth) {
    if (id <= 0)
      malformed();
    node(depth);
    scope(value, depth + 1);
    node(depth + 1);
    char decimal[20];
    const auto [end, error] =
        std::to_chars(decimal, decimal + sizeof(decimal), id);
    if (error != std::errc{})
      malformed();
    // Existing domain_ids.hpp encode_opaque: obcx1:<tag>: plus hex of
    // three length-prefixed fields (surface, kind, one decimal component).
    // Charge its ASCII string before the codec allocates the retained ID.
    const auto size = 6 + tag.size() + 1 +
                      2 * (12 + value.surface.value().size() + kind.size() +
                           static_cast<std::size_t>(end - decimal));
    text_size(size);
  }

private:
  const ValueLimits &limits_;
  std::size_t bytes_{};
  std::size_t nodes_{};
};

// Borrowed shape only: caller bounds must precede validation/unpack, which
// may allocate. The same ledger later charges retained/generated values.
void preflight_selector(const domain::BotInstallationRef &scope,
                        QueryBudget &budget, std::size_t depth) {
  budget.node(depth);
  budget.scope(scope, depth + 1);
  budget.node(depth + 1);
}
void preflight_selector(const domain::BotInstallationRef &scope,
                        std::string_view resource, QueryBudget &budget,
                        std::size_t depth) {
  preflight_selector(scope, budget, depth);
  budget.text(resource);
}
void preflight_request(const GetStrangerInfoParameters &p,
                       QueryBudget &budget) {
  budget.node(0);
  budget.reference(p.user, 1);
  budget.node(1); // no_cache
}
void preflight_request(const GetFriendListParameters &p, QueryBudget &budget) {
  budget.node(0);
  budget.scope(p.installation, 1);
}
void preflight_request(const GetGroupInfoParameters &p, QueryBudget &budget) {
  budget.node(0);
  budget.reference(p.group, 1);
  budget.node(1); // no_cache
}
void preflight_request(const GetGroupHonorInfoParameters &p,
                       QueryBudget &budget) {
  budget.node(0);
  budget.reference(p.group, 1);
  budget.node(1); // type
}
void preflight_request(const GetGroupListParameters &p, QueryBudget &budget) {
  budget.node(0);
  budget.scope(p.installation, 1);
}
void preflight_request(const GetGroupMemberInfoParameters &p,
                       QueryBudget &budget) {
  budget.node(0);
  budget.reference(p.group, 1);
  budget.reference(p.user, 1);
  budget.node(1); // no_cache
}
void preflight_request(const GetGroupMemberListParameters &p,
                       QueryBudget &budget) {
  budget.node(0);
  budget.reference(p.group, 1);
}
void preflight_parameters(QueryBudget &budget,
                          std::initializer_list<std::string_view> keys) {
  budget.node(0);
  for (const auto key : keys) {
    budget.text(key); // Actual generated native JSON keys, not DTO field names.
    budget.node(1);
  }
}

auto field(const Json &object, std::string_view key) -> const Json & {
  if (!object.is_object())
    malformed();
  const auto it = object.find(key);
  if (it == object.end())
    malformed();
  return *it;
}
auto text(const Json &object, std::string_view key) -> const std::string & {
  const auto &value = field(object, key);
  if (!value.is_string())
    malformed();
  return value.get_ref<const std::string &>();
}
auto integer(const Json &object, std::string_view key) -> std::int64_t {
  const auto &value = field(object, key);
  if (!value.is_number_integer() ||
      (value.is_number_unsigned() &&
       value.get<std::uint64_t>() > std::uint64_t{INT64_MAX}))
    malformed();
  return value.get<std::int64_t>();
}
auto int32(const Json &object, std::string_view key) -> std::int32_t {
  const auto value = integer(object, key);
  if (value < INT32_MIN || value > INT32_MAX)
    malformed();
  return static_cast<std::int32_t>(value);
}
auto boolean(const Json &object, std::string_view key) -> bool {
  const auto &value = field(object, key);
  if (!value.is_boolean())
    malformed();
  return value.get<bool>();
}
auto sex(const Json &object, std::string_view key) -> Sex {
  const auto &value = field(object, key);
  if (value == "male")
    return Sex::Male;
  if (value == "female")
    return Sex::Female;
  if (value == "unknown")
    return Sex::Unknown;
  malformed();
}
auto honor_kind(HonorKind value) -> std::string_view {
  switch (value) {
  case HonorKind::Talkative:
    return "talkative";
  case HonorKind::Performer:
    return "performer";
  case HonorKind::Legend:
    return "legend";
  case HonorKind::StrongNewbie:
    return "strong_newbie";
  case HonorKind::Emotion:
    return "emotion";
  case HonorKind::All:
    return "all";
  }
  malformed();
}
auto role(const Json &object, std::string_view key) -> GroupRole {
  const auto &value = field(object, key);
  if (value == "owner")
    return GroupRole::Owner;
  if (value == "admin")
    return GroupRole::Administrator;
  if (value == "member")
    return GroupRole::Member;
  malformed();
}
void array(const Json &value) {
  if (!value.is_array())
    malformed();
}

template <class Read>
auto optional(const Json &object, std::string_view key, Read read)
    -> std::optional<std::remove_cvref_t<decltype(read(object, key))>> {
  if (!object.is_object())
    malformed();
  if (!object.contains(key))
    return std::nullopt;
  return read(object, key);
}

auto native_id(const auto &ref, const Codec &codec, std::string_view kind)
    -> std::int64_t {
  installation(ref.installation, codec);
  codec.validate(ref);
  const auto parts = codec.unpack(ref.id);
  if (parts.kind != kind || parts.components.size() != 1)
    malformed();
  const auto &value = parts.components.front();
  std::int64_t result{};
  const auto [end, error] =
      std::from_chars(value.data(), value.data() + value.size(), result);
  if (error != std::errc{} || end != value.data() + value.size() ||
      result <= 0 || std::to_string(result) != value)
    malformed();
  return result;
}
void member_scope(const domain::ConversationRef &group,
                  const domain::UserRef &user) {
  if (group.installation != user.installation)
    malformed();
}
auto user(const Json &value, const domain::BotInstallationRef &scope,
          const Codec &codec) -> domain::UserRef {
  const auto id = integer(value, "user_id");
  if (id <= 0)
    malformed();
  domain::UserRef result{
      scope, codec.make<domain::UserId>("user", {std::to_string(id)})};
  return result;
}
auto group(const Json &value, const domain::BotInstallationRef &scope,
           const Codec &codec) -> domain::ConversationRef {
  const auto id = integer(value, "group_id");
  if (id <= 0)
    malformed();
  domain::ConversationRef result{
      scope, codec.make<domain::ConversationId>("group", {std::to_string(id)})};
  return result;
}
auto group_info(const Json &value, const domain::BotInstallationRef &scope,
                const Codec &codec) -> GroupInfo {
  return {group(value, scope, codec), text(value, "group_name"),
          int32(value, "member_count"), int32(value, "max_member_count")};
}
auto member_fields(const Json &v) -> MemberFields {
  return {text(v, "nickname"),
          text(v, "card"),
          sex(v, "sex"),
          int32(v, "age"),
          text(v, "area"),
          int32(v, "join_time"),
          int32(v, "last_sent_time"),
          text(v, "level"),
          role(v, "role"),
          boolean(v, "unfriendly"),
          text(v, "title"),
          int32(v, "title_expire_time"),
          boolean(v, "card_changeable")};
}
auto listed_member_fields(const Json &v) -> ListedMemberFields {
  return {optional(v, "nickname", text),
          optional(v, "card", text),
          optional(v, "sex", sex),
          optional(v, "age", int32),
          optional(v, "area", text),
          optional(v, "join_time", int32),
          optional(v, "last_sent_time", int32),
          optional(v, "level", text),
          optional(v, "role", role),
          optional(v, "unfriendly", boolean),
          optional(v, "title", text),
          optional(v, "title_expire_time", int32),
          optional(v, "card_changeable", boolean)};
}
// No retained payload is constructed until these projection passes complete.
void project_text(const Json &value, std::string_view key, QueryBudget &budget,
                  std::size_t depth) {
  budget.node(depth);
  budget.text(text(value, key));
}
void project_user(const Json &value, const domain::BotInstallationRef &scope,
                  QueryBudget &budget, std::size_t depth) {
  budget.native_reference(scope, integer(value, "user_id"), "user", "user",
                          depth);
}
void project_group(const Json &value, const domain::BotInstallationRef &scope,
                   QueryBudget &budget, std::size_t depth) {
  budget.native_reference(scope, integer(value, "group_id"), "conversation",
                          "group", depth);
}
void project_group_info(const Json &value,
                        const domain::BotInstallationRef &scope,
                        QueryBudget &budget, std::size_t depth) {
  budget.node(depth);
  project_group(value, scope, budget, depth + 1);
  project_text(value, "group_name", budget, depth + 1);
  for (auto key : {"member_count", "max_member_count"}) {
    (void)int32(value, key);
    budget.node(depth + 1);
  }
}
void project_honor_entry(const Json &value,
                         const domain::BotInstallationRef &scope,
                         QueryBudget &budget, std::size_t depth, bool current) {
  budget.node(depth);
  project_user(value, scope, budget, depth + 1);
  project_text(value, "nickname", budget, depth + 1);
  project_text(value, "avatar", budget, depth + 1);
  if (current) {
    (void)int32(value, "day_count");
    budget.node(depth + 1);
  } else {
    project_text(value, "description", budget, depth + 1);
  }
}
auto honor_list(const Json &data, std::string_view key,
                const domain::BotInstallationRef &scope, const Codec &codec)
    -> std::optional<std::vector<HonorEntry>> {
  if (!data.contains(key))
    return std::nullopt;
  const auto &values = field(data, key);
  std::vector<HonorEntry> result;
  result.reserve(values.size());
  for (const auto &entry : values)
    result.push_back(
        {user(entry, scope, codec), text(entry, "nickname"),
         detail::SensitiveValueFactory::text(text(entry, "avatar")),
         text(entry, "description")});
  return result;
}
void project_member_fields(const Json &value, QueryBudget &budget,
                           std::size_t depth, bool incomplete) {
  budget.node(depth);
  for (auto key : {"nickname", "card", "area", "level", "title"})
    if (!incomplete || value.contains(key))
      project_text(value, key, budget, depth + 1);
  for (auto key : {"age", "join_time", "last_sent_time", "title_expire_time"})
    if (!incomplete || value.contains(key)) {
      (void)int32(value, key);
      budget.node(depth + 1);
    }
  for (auto key : {"unfriendly", "card_changeable"})
    if (!incomplete || value.contains(key)) {
      (void)boolean(value, key);
      budget.node(depth + 1);
    }
  if (!incomplete || value.contains("sex")) {
    (void)sex(value, "sex");
    budget.node(depth + 1);
  }
  if (!incomplete || value.contains("role")) {
    (void)role(value, "role");
    budget.node(depth + 1);
  }
}
} // namespace

MessageSelector::MessageSelector(domain::BotInstallationRef scope,
                                 std::int32_t id)
    : installation_(std::move(scope)), message_id_(id) {}
auto MessageSelector::installation() const noexcept
    -> const domain::BotInstallationRef & {
  return installation_;
}
ForwardSelector::ForwardSelector(domain::BotInstallationRef scope,
                                 std::string id)
    : installation_(std::move(scope)), resource_id_(std::move(id)) {}
auto ForwardSelector::installation() const noexcept
    -> const domain::BotInstallationRef & {
  return installation_;
}
auto QuerySelectors::import_message(const domain::BotInstallationRef &scope,
                                    std::int32_t id, const ValueLimits &limits)
    -> MessageSelector {
  QueryBudget budget{limits};
  preflight_selector(scope, budget, 0); // borrowed arguments
  preflight_selector(scope, budget,
                     0); // retained selector, including scope copy
  installation(scope);
  return MessageSelector{scope, id};
}
auto QuerySelectors::export_message(const MessageSelector &value,
                                    const ValueLimits &limits) -> std::int32_t {
  QueryBudget budget{limits};
  preflight_selector(value.installation_, budget, 0);
  budget.node(0); // retained scalar; export does not copy the scope
  installation(value.installation_);
  return value.message_id_;
}
auto QuerySelectors::import_forward(const domain::BotInstallationRef &scope,
                                    std::string_view id,
                                    const ValueLimits &limits)
    -> ForwardSelector {
  QueryBudget budget{limits};
  preflight_selector(scope, id, budget, 0); // borrowed arguments
  preflight_selector(scope, id, budget,
                     0); // retained scope and resource copies
  installation(scope);
  return ForwardSelector{scope, std::string{id}};
}
auto QuerySelectors::export_forward(const ForwardSelector &value,
                                    const ValueLimits &limits) -> std::string {
  QueryBudget budget{limits};
  preflight_selector(value.installation_, value.resource_id_, budget, 0);
  budget.node(0);
  budget.text(value.resource_id_); // retained string; scope remains borrowed
  installation(value.installation_);
  return value.resource_id_;
}

namespace detail {
auto encode_get_msg(const GetMessageParameters &p,
                    const domain::BotInstallationRef &expected,
                    const Codec &codec, const ValueLimits &limits) -> Json {
  QueryBudget budget{limits};
  budget.node(0);
  preflight_selector(p.message.installation(), budget, 1);
  budget.scope(expected, 0);
  preflight_parameters(budget, {"message_id"});
  installation(expected, codec);
  if (p.message.installation() != expected)
    malformed();
  auto result = Json::object();
  result.emplace("message_id", QuerySelectorAccess::message_id(p.message));
  return result;
}
auto encode_get_forward_msg(const GetForwardMessageParameters &p,
                            const domain::BotInstallationRef &expected,
                            const Codec &codec, const ValueLimits &limits)
    -> Json {
  QueryBudget budget{limits};
  const auto &resource = QuerySelectorAccess::resource(p.forward);
  budget.node(0);
  preflight_selector(p.forward.installation(), resource, budget, 1);
  budget.scope(expected, 0);
  preflight_parameters(budget, {"id"});
  budget.text(resource); // Actual generated JSON string, before its only copy.
  installation(expected, codec);
  if (p.forward.installation() != expected)
    malformed();
  auto result = Json::object();
  result.emplace("id", resource);
  return result;
}
auto encode_get_stranger_info(const GetStrangerInfoParameters &p,
                              const Codec &codec, const ValueLimits &limits)
    -> Json {
  QueryBudget budget{limits};
  preflight_request(p, budget);
  preflight_parameters(budget, {"user_id", "no_cache"});
  const auto id = native_id(p.user, codec, "user");
  auto result = Json::object();
  result.emplace("user_id", id);
  result.emplace("no_cache", p.no_cache);
  return result;
}
auto encode_get_friend_list(const GetFriendListParameters &p,
                            const Codec &codec, const ValueLimits &limits)
    -> Json {
  QueryBudget budget{limits};
  preflight_request(p, budget);
  preflight_parameters(budget, {});
  installation(p.installation, codec);
  return Json::object();
}
auto encode_get_group_info(const GetGroupInfoParameters &p, const Codec &codec,
                           const ValueLimits &limits) -> Json {
  QueryBudget budget{limits};
  preflight_request(p, budget);
  preflight_parameters(budget, {"group_id", "no_cache"});
  const auto id = native_id(p.group, codec, "group");
  auto result = Json::object();
  result.emplace("group_id", id);
  result.emplace("no_cache", p.no_cache);
  return result;
}
auto encode_get_group_honor_info(const GetGroupHonorInfoParameters &p,
                                 const Codec &codec, const ValueLimits &limits)
    -> Json {
  QueryBudget budget{limits};
  preflight_request(p, budget);
  const auto type = honor_kind(p.type);
  preflight_parameters(budget, {"group_id", "type"});
  budget.text(type);
  const auto id = native_id(p.group, codec, "group");
  auto result = Json::object();
  result.emplace("group_id", id);
  result.emplace("type", type);
  return result;
}
auto encode_get_group_list(const GetGroupListParameters &p, const Codec &codec,
                           const ValueLimits &limits) -> Json {
  QueryBudget budget{limits};
  preflight_request(p, budget);
  preflight_parameters(budget, {});
  installation(p.installation, codec);
  return Json::object();
}
auto encode_get_group_member_info(const GetGroupMemberInfoParameters &p,
                                  const Codec &codec, const ValueLimits &limits)
    -> Json {
  QueryBudget budget{limits};
  preflight_request(p, budget);
  preflight_parameters(budget, {"group_id", "user_id", "no_cache"});
  member_scope(p.group, p.user);
  const auto group_id = native_id(p.group, codec, "group");
  const auto user_id = native_id(p.user, codec, "user");
  auto result = Json::object();
  result.emplace("group_id", group_id);
  result.emplace("user_id", user_id);
  result.emplace("no_cache", p.no_cache);
  return result;
}
auto encode_get_group_member_list(const GetGroupMemberListParameters &p,
                                  const Codec &codec, const ValueLimits &limits)
    -> Json {
  QueryBudget budget{limits};
  preflight_request(p, budget);
  preflight_parameters(budget, {"group_id"});
  const auto id = native_id(p.group, codec, "group");
  auto result = Json::object();
  result.emplace("group_id", id);
  return result;
}

auto decode_get_stranger_info(const Json &data,
                              const GetStrangerInfoParameters &p,
                              const Codec &codec, const ValueLimits &limits)
    -> StrangerInfo {
  QueryBudget budget{limits};
  preflight_request(p, budget);
  budget.inspect(data);
  const auto expected = native_id(p.user, codec, "user");
  if (integer(data, "user_id") != expected)
    malformed();
  budget.node(0);
  project_user(data, p.user.installation, budget, 1);
  project_text(data, "nickname", budget, 1);
  (void)sex(data, "sex");
  budget.node(1);
  (void)int32(data, "age");
  budget.node(1);
  return {user(data, p.user.installation, codec), text(data, "nickname"),
          sex(data, "sex"), int32(data, "age")};
}
auto decode_get_friend_list(const Json &data, const GetFriendListParameters &p,
                            const Codec &codec, const ValueLimits &limits)
    -> FriendList {
  QueryBudget budget{limits};
  preflight_request(p, budget);
  budget.inspect(data);
  installation(p.installation, codec);
  array(data);
  budget.node(0);
  budget.scope(p.installation, 1);
  budget.node(1);
  for (const auto &entry : data) {
    budget.node(2);
    project_user(entry, p.installation, budget, 3);
    project_text(entry, "nickname", budget, 3);
    project_text(entry, "remark", budget, 3);
  }
  FriendList result{p.installation, {}};
  result.entries.reserve(data.size());
  for (const auto &entry : data)
    result.entries.push_back({user(entry, p.installation, codec),
                              text(entry, "nickname"), text(entry, "remark")});
  return result;
}
auto decode_get_group_info(const Json &data, const GetGroupInfoParameters &p,
                           const Codec &codec, const ValueLimits &limits)
    -> GroupInfo {
  QueryBudget budget{limits};
  preflight_request(p, budget);
  budget.inspect(data);
  const auto expected = native_id(p.group, codec, "group");
  if (integer(data, "group_id") != expected)
    malformed();
  project_group_info(data, p.group.installation, budget, 0);
  return group_info(data, p.group.installation, codec);
}
auto decode_get_group_honor_info(const Json &data,
                                 const GetGroupHonorInfoParameters &p,
                                 const Codec &codec, const ValueLimits &limits)
    -> GroupHonorInfo {
  QueryBudget budget{limits};
  preflight_request(p, budget);
  budget.inspect(data);
  (void)honor_kind(p.type);
  const auto expected = native_id(p.group, codec, "group");
  if (integer(data, "group_id") != expected)
    malformed();
  budget.node(0);
  project_group(data, p.group.installation, budget, 1);
  budget.node(1); // requested_type
  if (data.contains("current_talkative"))
    project_honor_entry(field(data, "current_talkative"), p.group.installation,
                        budget, 1, true);
  for (const auto key : {"talkative_list", "performer_list", "legend_list",
                         "strong_newbie_list", "emotion_list"}) {
    if (!data.contains(key))
      continue;
    const auto &values = field(data, key);
    array(values);
    budget.node(1);
    for (const auto &entry : values)
      project_honor_entry(entry, p.group.installation, budget, 2, false);
  }
  // All categories, refs and restricted copies are reserved before
  // construction.
  GroupHonorInfo result{group(data, p.group.installation, codec),
                        p.type,
                        std::nullopt,
                        std::nullopt,
                        std::nullopt,
                        std::nullopt,
                        std::nullopt,
                        std::nullopt};
  if (data.contains("current_talkative")) {
    const auto &value = field(data, "current_talkative");
    result.current_talkative.emplace(CurrentTalkative{
        user(value, p.group.installation, codec), text(value, "nickname"),
        SensitiveValueFactory::text(text(value, "avatar")),
        int32(value, "day_count")});
  }
  result.talkative_list =
      honor_list(data, "talkative_list", p.group.installation, codec);
  result.performer_list =
      honor_list(data, "performer_list", p.group.installation, codec);
  result.legend_list =
      honor_list(data, "legend_list", p.group.installation, codec);
  result.strong_newbie_list =
      honor_list(data, "strong_newbie_list", p.group.installation, codec);
  result.emotion_list =
      honor_list(data, "emotion_list", p.group.installation, codec);
  return result;
}
auto decode_get_group_list(const Json &data, const GetGroupListParameters &p,
                           const Codec &codec, const ValueLimits &limits)
    -> GroupList {
  QueryBudget budget{limits};
  preflight_request(p, budget);
  budget.inspect(data);
  installation(p.installation, codec);
  array(data);
  budget.node(0);
  budget.scope(p.installation, 1);
  budget.node(1);
  for (const auto &entry : data)
    project_group_info(entry, p.installation, budget, 2);
  GroupList result{p.installation, {}};
  result.entries.reserve(data.size());
  for (const auto &entry : data)
    result.entries.push_back(group_info(entry, p.installation, codec));
  return result;
}
auto decode_get_group_member_info(const Json &data,
                                  const GetGroupMemberInfoParameters &p,
                                  const Codec &codec, const ValueLimits &limits)
    -> GroupMemberInfo {
  QueryBudget budget{limits};
  preflight_request(p, budget);
  budget.inspect(data);
  member_scope(p.group, p.user);
  const auto expected_group = native_id(p.group, codec, "group");
  const auto expected_user = native_id(p.user, codec, "user");
  if (integer(data, "group_id") != expected_group ||
      integer(data, "user_id") != expected_user)
    malformed();
  budget.node(0);
  project_group(data, p.group.installation, budget, 1);
  project_user(data, p.group.installation, budget, 1);
  project_member_fields(data, budget, 1, false);
  return {group(data, p.group.installation, codec),
          user(data, p.group.installation, codec), member_fields(data)};
}
auto decode_get_group_member_list(const Json &data,
                                  const GetGroupMemberListParameters &p,
                                  const Codec &codec, const ValueLimits &limits)
    -> GroupMemberList {
  QueryBudget budget{limits};
  preflight_request(p, budget);
  budget.inspect(data);
  const auto expected = native_id(p.group, codec, "group");
  array(data);
  budget.node(0);
  budget.reference(p.group, 1);
  budget.node(1);
  for (const auto &entry : data) {
    if (integer(entry, "group_id") != expected)
      malformed();
    budget.node(2);
    project_group(entry, p.group.installation, budget, 3);
    project_user(entry, p.group.installation, budget, 3);
    project_member_fields(entry, budget, 3, true);
  }
  GroupMemberList result{p.group, {}};
  result.entries.reserve(data.size());
  for (const auto &entry : data)
    result.entries.push_back({group(entry, p.group.installation, codec),
                              user(entry, p.group.installation, codec),
                              listed_member_fields(entry)});
  return result;
}
} // namespace detail
} // namespace obcx::onebot11::bot::standard
