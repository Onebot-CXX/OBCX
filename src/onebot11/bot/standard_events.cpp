#include "standard_event_codec.hpp"
#include "standard_service_codec.hpp"

#include "core/bot/validation.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace obcx::onebot11::bot::standard::detail {
namespace {

using Json = common::json;

class NoticeBudget {
public:
  explicit NoticeBudget(const ValueLimits &limits)
      : bytes_left_(std::min(limits.maximum_bytes,
                             obcx::bot::detail::bot_maximum_bytes)),
        nodes_left_(std::min(limits.maximum_nodes,
                             obcx::bot::detail::bot_maximum_items)),
        maximum_text_bytes_(limits.maximum_text_bytes),
        maximum_depth_(std::min(limits.maximum_depth, std::size_t{32})) {}

  void inspect(const Json &value, std::size_t depth) {
    if (depth > maximum_depth_)
      throw std::invalid_argument("notice exceeds depth limit");
    charge(nodes_left_, 1);
    if (value.is_object()) {
      for (auto entry = value.begin(); entry != value.end(); ++entry) {
        text(entry.key().size());
        inspect(entry.value(), depth + 1);
      }
    } else if (value.is_array()) {
      for (const auto &entry : value)
        inspect(entry, depth + 1);
    } else if (value.is_string()) {
      text(value.get_ref<const std::string &>().size());
    } else if (value.is_binary()) {
      charge(bytes_left_, value.get_binary().size());
    } else if (!value.is_number() && !value.is_boolean() && !value.is_null()) {
      throw std::invalid_argument("notice contains a non-JSON value");
    }
  }

  // The accepted ten-notice inspector above keeps its existing behavior.
  // Sibling events require standard JSON/UTF-8, including discarded fields and
  // restricted status extras. Both traversal and projection use this ledger.
  void inspect_event(const Json &value, std::size_t depth) {
    nodes(1, depth);
    if (value.is_object() || value.is_array()) {
      for (auto entry = value.begin(); entry != value.end(); ++entry) {
        if (value.is_object())
          event_text(entry.key());
        inspect_event(entry.value(), depth + 1);
      }
    } else if (value.is_string()) {
      event_text(value.get_ref<const std::string &>());
    } else if (value.is_number()) {
      if (value.is_number_float() && !std::isfinite(value.get<double>()))
        throw std::invalid_argument("event contains a non-JSON number");
    } else if (!value.is_boolean() && !value.is_null()) {
      throw std::invalid_argument("event contains a non-JSON value");
    }
  }

  void nodes(std::size_t count, std::size_t depth) {
    if (depth > maximum_depth_)
      throw std::invalid_argument("event exceeds depth limit");
    charge(nodes_left_, count);
  }

  void event_text(std::string_view value) {
    text(value.size());
    // Allocation-free UTF-8 validation prevents native bytes in serializer
    // diagnostics and preserves valid spelling, including empty strings.
    for (std::size_t i = 0; i < value.size();) {
      const auto first = static_cast<unsigned char>(value[i]);
      if (first < 0x80) {
        ++i;
        continue;
      }
      const std::size_t width = first >= 0xC2 && first <= 0xDF   ? 2
                                : first >= 0xE0 && first <= 0xEF ? 3
                                : first >= 0xF0 && first <= 0xF4 ? 4
                                                                 : 0;
      if (width == 0 || width > value.size() - i)
        throw std::invalid_argument("event contains invalid UTF-8");
      for (std::size_t j = 1; j < width; ++j) {
        const auto next = static_cast<unsigned char>(value[i + j]);
        if (next < 0x80 || next > 0xBF)
          throw std::invalid_argument("event contains invalid UTF-8");
      }
      const auto second = static_cast<unsigned char>(value[i + 1]);
      if ((first == 0xE0 && second < 0xA0) ||
          (first == 0xED && second > 0x9F) ||
          (first == 0xF0 && second < 0x90) || (first == 0xF4 && second > 0x8F))
        throw std::invalid_argument("event contains invalid UTF-8");
      i += width;
    }
  }

  void projection(std::size_t payload_fields) {
    // Observation root at depth 0, header and selected payload at depth 1,
    // their fields at depth 2. The variant adds no container or tag node.
    if (maximum_depth_ < 2)
      throw std::invalid_argument("notice exceeds depth limit");
    // Root, header, two header scalars, selected payload and its fields share
    // the input reservation. Scalars/enums add no owned bytes.
    charge(nodes_left_, 5 + payload_fields);
  }

private:
  static void charge(std::size_t &remaining, std::size_t count) {
    if (count > remaining)
      throw std::invalid_argument("notice exceeds value limits");
    remaining -= count;
  }

  void text(std::size_t count) {
    if (count > maximum_text_bytes_)
      throw std::invalid_argument("notice exceeds text limit");
    charge(bytes_left_, count);
  }

  std::size_t bytes_left_;
  std::size_t nodes_left_;
  std::size_t maximum_text_bytes_;
  std::size_t maximum_depth_;
};

auto field(const Json &document, std::string_view key) -> const Json & {
  const auto entry = document.find(key);
  if (entry == document.end())
    throw std::invalid_argument("notice requires field " + std::string{key});
  return *entry;
}

auto integer(const Json &document, std::string_view key) -> std::int64_t {
  const auto &value = field(document, key);
  if (value.is_number_unsigned()) {
    const auto number = value.get<std::uint64_t>();
    if (number <=
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
      return static_cast<std::int64_t>(number);
  } else if (value.is_number_integer()) {
    return value.get<std::int64_t>();
  }
  throw std::invalid_argument("notice requires int64 " + std::string{key});
}

auto text(const Json &document, std::string_view key) -> std::string_view {
  const auto &value = field(document, key);
  if (!value.is_string())
    throw std::invalid_argument("notice requires string " + std::string{key});
  return value.get_ref<const std::string &>();
}

template <typename Enum>
auto enum_value(const Json &document, std::string_view key,
                std::initializer_list<std::pair<std::string_view, Enum>> values)
    -> Enum {
  const auto native = text(document, key);
  for (const auto &[name, value] : values)
    if (native == name)
      return value;
  throw std::invalid_argument("notice has unsupported " + std::string{key});
}

auto event_header(const Json &document, NoticeBudget &budget,
                  std::string_view post_type, std::string_view type_key,
                  std::string_view type) -> EventHeader {
  budget.inspect_event(document, 0);
  if (!document.is_object())
    throw std::invalid_argument("event must be an object");
  if (text(document, "post_type") != post_type ||
      text(document, type_key) != type)
    throw std::invalid_argument("event has unsupported discriminator");
  return {integer(document, "time"), integer(document, "self_id")};
}

void project_event_header(NoticeBudget &budget) {
  // Root, header, time and self_id; DTO member names are not owned strings.
  budget.nodes(1, 0);
  budget.nodes(1, 1);
  budget.nodes(2, 2);
}

} // namespace

auto decode_notice(const Json &document, const ValueLimits &limits)
    -> NoticeObservation {
  limits.validate();
  NoticeBudget budget{limits};
  budget.inspect(document, 0);
  if (!document.is_object())
    throw std::invalid_argument("notice must be an object");
  if (text(document, "post_type") != "notice")
    throw std::invalid_argument("notice has unsupported post_type");

  const NoticeHeader header{integer(document, "time"),
                            integer(document, "self_id")};
  const auto notice_type = text(document, "notice_type");
  const auto finish = [&]<typename Payload>(Payload payload,
                                            std::size_t fields) {
    budget.projection(fields);
    return NoticeObservation{header, std::move(payload)};
  };

  if (notice_type == "group_admin") {
    const auto subtype = enum_value<GroupAdminChange>(
        document, "sub_type",
        {{"set", GroupAdminChange::Set}, {"unset", GroupAdminChange::Unset}});
    return finish(GroupAdminNotice{subtype, integer(document, "group_id"),
                                   integer(document, "user_id")},
                  3);
  }
  if (notice_type == "group_decrease") {
    const auto subtype =
        enum_value<GroupDecreaseKind>(document, "sub_type",
                                      {{"leave", GroupDecreaseKind::Leave},
                                       {"kick", GroupDecreaseKind::Kick},
                                       {"kick_me", GroupDecreaseKind::KickMe}});
    const GroupDecreaseNotice payload{subtype, integer(document, "group_id"),
                                      integer(document, "operator_id"),
                                      integer(document, "user_id")};
    if (subtype == GroupDecreaseKind::Leave &&
        payload.operator_id != payload.user_id)
      throw std::invalid_argument(
          "notice leave operator_id conflicts with user_id");
    return finish(payload, 4);
  }
  if (notice_type == "group_increase") {
    const auto subtype =
        enum_value<GroupIncreaseKind>(document, "sub_type",
                                      {{"approve", GroupIncreaseKind::Approve},
                                       {"invite", GroupIncreaseKind::Invite}});
    return finish(GroupIncreaseNotice{subtype, integer(document, "group_id"),
                                      integer(document, "operator_id"),
                                      integer(document, "user_id")},
                  4);
  }
  if (notice_type == "group_ban") {
    const auto subtype = enum_value<GroupBanChange>(
        document, "sub_type",
        {{"ban", GroupBanChange::Ban}, {"lift_ban", GroupBanChange::LiftBan}});
    return finish(GroupBanNotice{subtype, integer(document, "group_id"),
                                 integer(document, "operator_id"),
                                 integer(document, "user_id"),
                                 integer(document, "duration")},
                  5);
  }
  if (notice_type == "friend_add")
    return finish(FriendAddNotice{integer(document, "user_id")}, 1);
  if (notice_type == "group_recall")
    return finish(GroupRecallNotice{integer(document, "group_id"),
                                    integer(document, "user_id"),
                                    integer(document, "operator_id"),
                                    integer(document, "message_id")},
                  4);
  if (notice_type == "friend_recall")
    return finish(FriendRecallNotice{integer(document, "user_id"),
                                     integer(document, "message_id")},
                  2);
  if (notice_type == "notify") {
    const auto subtype = text(document, "sub_type");
    if (subtype == "poke")
      return finish(GroupPokeNotice{integer(document, "group_id"),
                                    integer(document, "user_id"),
                                    integer(document, "target_id")},
                    3);
    if (subtype == "lucky_king")
      return finish(GroupLuckyKingNotice{integer(document, "group_id"),
                                         integer(document, "user_id"),
                                         integer(document, "target_id")},
                    3);
    if (subtype == "honor") {
      const auto honor =
          enum_value<GroupHonorKind>(document, "honor_type",
                                     {{"talkative", GroupHonorKind::Talkative},
                                      {"performer", GroupHonorKind::Performer},
                                      {"emotion", GroupHonorKind::Emotion}});
      return finish(GroupHonorNotice{integer(document, "group_id"), honor,
                                     integer(document, "user_id")},
                    3);
    }
    throw std::invalid_argument("notice has unsupported sub_type");
  }
  throw std::invalid_argument("notice has unsupported notice_type");
}

auto decode_lifecycle(const Json &document, const ValueLimits &limits)
    -> LifecycleObservation {
  limits.validate();
  NoticeBudget budget{limits};
  const auto header = event_header(document, budget, "meta_event",
                                   "meta_event_type", "lifecycle");
  const auto subtype =
      enum_value<LifecycleKind>(document, "sub_type",
                                {{"enable", LifecycleKind::Enable},
                                 {"disable", LifecycleKind::Disable},
                                 {"connect", LifecycleKind::Connect}});
  project_event_header(budget);
  budget.nodes(1, 1); // Subtype: five projection nodes total, depth two.
  return {header, subtype};
}

auto decode_group_upload(const Json &document, const ValueLimits &limits)
    -> GroupUploadObservation {
  limits.validate();
  NoticeBudget budget{limits};
  const auto header =
      event_header(document, budget, "notice", "notice_type", "group_upload");
  const auto group = integer(document, "group_id");
  const auto user = integer(document, "user_id");
  const auto &file = field(document, "file");
  if (!file.is_object())
    throw std::invalid_argument("event file must be an object");
  const auto id = text(file, "id");
  const auto name = text(file, "name");
  const auto size = integer(file, "size");
  const auto busid = integer(file, "busid");
  project_event_header(budget);
  budget.nodes(3, 1); // Group, user and file object.
  budget.nodes(4, 2); // Eleven projection nodes total; no copied DTO keys.
  budget.event_text(id);
  budget.event_text(name);
  return {
      header, group, user, {std::string{id}, std::string{name}, size, busid}};
}

auto decode_friend_request(const Json &document, const ValueLimits &limits)
    -> FriendRequestObservation {
  limits.validate();
  NoticeBudget budget{limits};
  const auto header =
      event_header(document, budget, "request", "request_type", "friend");
  const auto user = integer(document, "user_id");
  const auto comment = text(document, "comment");
  const auto flag = text(document, "flag");
  project_event_header(budget);
  budget.nodes(3, 1); // User, comment and restricted flag: seven total.
  budget.event_text(comment);
  budget.event_text(flag);
  return {header, user, std::string{comment},
          SensitiveValueFactory::text(flag)};
}

auto decode_group_request(const Json &document, const ValueLimits &limits)
    -> GroupRequestObservation {
  limits.validate();
  NoticeBudget budget{limits};
  const auto header =
      event_header(document, budget, "request", "request_type", "group");
  const auto subtype = enum_value<GroupRequestKind>(
      document, "sub_type",
      {{"add", GroupRequestKind::Add}, {"invite", GroupRequestKind::Invite}});
  const auto group = integer(document, "group_id");
  const auto user = integer(document, "user_id");
  const auto comment = text(document, "comment");
  const auto flag = text(document, "flag");
  project_event_header(budget);
  budget.nodes(5, 1); // Subtype, group, user, comment and flag: nine total.
  budget.event_text(comment);
  budget.event_text(flag);
  return {header,
          subtype,
          group,
          user,
          std::string{comment},
          SensitiveValueFactory::text(flag)};
}

auto decode_heartbeat(const Json &document, const ValueLimits &limits)
    -> HeartbeatObservation {
  limits.validate();
  NoticeBudget budget{limits};
  const auto header = event_header(document, budget, "meta_event",
                                   "meta_event_type", "heartbeat");
  const auto interval = integer(document, "interval");
  const auto &status = field(document, "status");
  if (!status.is_object())
    throw std::invalid_argument("event status must be an object");
  const auto &online = field(status, "online");
  if (!online.is_null() && !online.is_boolean())
    throw std::invalid_argument("event online must be null or boolean");
  const auto &good = field(status, "good");
  if (!good.is_boolean())
    throw std::invalid_argument("event good must be boolean");

  project_event_header(budget);
  budget.nodes(2, 1); // Status object and interval.
  budget.nodes(3, 2); // Required online (including null), good, extras object.
  // Nine fixed projection nodes, plus actual retained extras at event depth.
  // No copied known keys or synthetic wrapper/tag bytes. Precharge ALL extras
  // before calling the factory, which does not enforce its own limits.
  for (auto entry = status.begin(); entry != status.end(); ++entry) {
    if (entry.key() != "online" && entry.key() != "good") {
      budget.event_text(entry.key());
      budget.inspect_event(entry.value(), 3);
    }
  }
  return {header,
          {online.is_null() ? std::nullopt : std::optional{online.get<bool>()},
           good.get<bool>(),
           SensitiveValueFactory::extras(status, {"online", "good"})},
          interval};
}

namespace {
auto event_int32(const Json &d, std::string_view key) -> std::int32_t {
  auto n = integer(d, key);
  if (!std::in_range<std::int32_t>(n))
    throw std::invalid_argument("event integer exceeds int32");
  return static_cast<std::int32_t>(n);
}
auto event_string(const Json &d, std::string_view key, NoticeBudget &b)
    -> std::string {
  auto v = text(d, key);
  b.event_text(v);
  return std::string{v};
}
auto event_secret(const Json &d, std::string_view key, NoticeBudget &b)
    -> SensitiveText {
  auto v = text(d, key);
  b.event_text(v);
  return SensitiveValueFactory::text(v);
}
template <typename T, typename F>
auto event_optional(const Json &d, std::string_view key, F decode)
    -> EventField<T> {
  auto it = d.find(key);
  if (it == d.end())
    return {false, std::nullopt};
  if (it->is_null())
    return {true, std::nullopt};
  return {true, decode()};
}
auto event_optional_text(const Json &d, std::string_view key, NoticeBudget &b)
    -> EventField<std::string> {
  return event_optional<std::string>(d, key,
                                     [&] { return event_string(d, key, b); });
}
auto event_optional_secret(const Json &d, std::string_view key, NoticeBudget &b)
    -> EventField<SensitiveText> {
  return event_optional<SensitiveText>(d, key,
                                       [&] { return event_secret(d, key, b); });
}
auto event_segment(const Json &segment, NoticeBudget &b) -> EventSegment {
  if (!segment.is_object())
    throw std::invalid_argument("event segment must be an object");
  const auto type = text(segment, "type");
  const auto &d = field(segment, "data");
  if (!d.is_object() && !d.is_null())
    throw std::invalid_argument("event segment data must be object or null");
  b.nodes(10, 4); // Bound fixed segment fields and optional-value wrappers
                  // before copying.
  if (type == "text")
    return EventText{event_string(d, "text", b)};
  if (type == "face")
    return EventFace{event_string(d, "id", b)};
  if (type == "image")
    return EventImage{event_secret(d, "file", b),
                      event_optional_text(d, "type", b),
                      event_optional_secret(d, "url", b)};
  if (type == "record")
    return EventRecord{event_secret(d, "file", b),
                       event_optional_text(d, "magic", b),
                       event_optional_secret(d, "url", b)};
  if (type == "video")
    return EventVideo{event_secret(d, "file", b),
                      event_optional_secret(d, "url", b)};
  if (type == "at")
    return EventAt{event_string(d, "qq", b)};
  if (type == "rps")
    return EventRps{};
  if (type == "dice")
    return EventDice{};
  if (type == "poke")
    return EventPoke{event_string(d, "type", b), event_string(d, "id", b),
                     event_optional_text(d, "name", b)};
  if (type == "share")
    return EventShare{event_secret(d, "url", b), event_string(d, "title", b),
                      event_optional_text(d, "content", b),
                      event_optional_secret(d, "image", b)};
  if (type == "contact") {
    auto kind = event_string(d, "type", b);
    if (kind != "qq" && kind != "group")
      throw std::invalid_argument("event contact kind invalid");
    return EventContact{std::move(kind), event_string(d, "id", b)};
  }
  if (type == "location")
    return EventLocation{event_string(d, "lat", b), event_string(d, "lon", b),
                         event_optional_text(d, "title", b),
                         event_optional_text(d, "content", b)};
  if (type == "reply")
    return EventReply{event_string(d, "id", b)};
  if (type == "forward")
    return EventForward{event_secret(d, "id", b)};
  if (type == "xml")
    return EventXml{event_secret(d, "data", b)};
  if (type == "json")
    return EventJson{event_secret(d, "data", b)};
  // Send-only segments and forward-query nodes are not received message
  // segments.
  throw std::invalid_argument("unsupported received event segment");
}
auto event_unescape(std::string_view v, bool parameter, NoticeBudget &b)
    -> std::string {
  b.event_text(v); // Escaping only shrinks; reserve before allocation.
  std::string result;
  result.reserve(v.size());
  for (std::size_t i = 0; i < v.size();) {
    bool found = false;
    for (auto [encoded, decoded] :
         {std::pair<std::string_view, char>{"&amp;", '&'},
          {"&#91;", '['},
          {"&#93;", ']'},
          {"&#44;", ','}}) {
      if ((!parameter && decoded == ',') || !v.substr(i).starts_with(encoded))
        continue;
      result.push_back(decoded);
      i += encoded.size();
      found = true;
      break;
    }
    if (!found)
      result.push_back(v[i++]);
  }
  return result;
}
auto event_content(const Json &v, NoticeBudget &b) -> EventMessageContent {
  EventMessageContent result{v.is_string() ? EventMessageEncoding::CqString
                                           : EventMessageEncoding::SegmentArray,
                             {}};
  if (v.is_array()) {
    for (const auto &segment : v)
      result.segments.push_back(event_segment(segment, b));
    return result;
  }
  if (!v.is_string())
    throw std::invalid_argument("event message must be string or array");
  std::string_view input = v.get_ref<const std::string &>();
  while (!input.empty()) {
    auto start = input.find("[CQ:");
    if (start != 0) {
      const auto piece = input.substr(0, start);
      b.nodes(2, 3);
      result.segments.emplace_back(EventText{event_unescape(piece, false, b)});
      input.remove_prefix(piece.size());
      continue;
    }
    auto end = input.find(']');
    if (end == std::string_view::npos)
      throw std::invalid_argument("invalid event CQ segment");
    auto body = input.substr(4, end - 4);
    const auto comma = body.find(',');
    const auto type = body.substr(0, comma);
    if (type.empty() || comma == body.size() - 1)
      throw std::invalid_argument("invalid event CQ segment");
    b.nodes(3, 3);
    b.event_text(type);
    b.event_text("type");
    b.event_text("data");
    Json segment{{"type", std::string{type}}, {"data", Json::object()}};
    body.remove_prefix(comma == std::string_view::npos ? body.size()
                                                       : comma + 1);
    while (!body.empty()) {
      const auto next = body.find(',');
      if (next == body.size() - 1)
        throw std::invalid_argument("invalid event CQ parameter");
      auto pair = body.substr(0, next);
      auto eq = pair.find('=');
      if (eq == std::string_view::npos || eq == 0)
        throw std::invalid_argument("invalid event CQ parameter");
      auto key = pair.substr(0, eq);
      if (segment["data"].contains(key))
        throw std::invalid_argument("duplicate event CQ parameter");
      b.nodes(1, 4);
      b.event_text(key);
      segment["data"][std::string{key}] =
          event_unescape(pair.substr(eq + 1), true, b);
      body.remove_prefix(next == std::string_view::npos ? body.size()
                                                        : next + 1);
    }
    result.segments.push_back(event_segment(segment, b));
    input.remove_prefix(end + 1);
  }
  return result;
}
auto event_sender(const Json &d, NoticeBudget &b) -> EventSender {
  if (!d.is_object())
    throw std::invalid_argument("event sender must be object");
  b.nodes(19, 3);
  return {event_optional<std::int64_t>(d, "user_id",
                                       [&] { return integer(d, "user_id"); }),
          event_optional_text(d, "nickname", b),
          event_optional<Sex>(d, "sex",
                              [&] {
                                return enum_value<Sex>(
                                    d, "sex",
                                    {{"male", Sex::Male},
                                     {"female", Sex::Female},
                                     {"unknown", Sex::Unknown}});
                              }),
          event_optional<std::int32_t>(d, "age",
                                       [&] { return event_int32(d, "age"); }),
          event_optional_text(d, "card", b),
          event_optional_text(d, "area", b),
          event_optional_text(d, "level", b),
          event_optional<GroupRole>(d, "role",
                                    [&] {
                                      return enum_value<GroupRole>(
                                          d, "role",
                                          {{"owner", GroupRole::Owner},
                                           {"admin", GroupRole::Administrator},
                                           {"member", GroupRole::Member}});
                                    }),
          event_optional_text(d, "title", b)};
}
} // namespace

auto decode_standard_event(const Json &d, const ValueLimits &limits)
    -> StandardEventObservation {
  limits.validate();
  NoticeBudget b{limits};
  b.inspect_event(d, 0);
  if (!d.is_object())
    throw std::invalid_argument("event must be object");
  const auto post = text(d, "post_type");
  if (post == "notice") {
    if (text(d, "notice_type") == "group_upload")
      return decode_group_upload(d, limits);
    return decode_notice(d, limits);
  }
  if (post == "meta_event") {
    if (text(d, "meta_event_type") == "heartbeat")
      return decode_heartbeat(d, limits);
    return decode_lifecycle(d, limits);
  }
  EventHeader header{integer(d, "time"), integer(d, "self_id")};
  if (post == "request") {
    // Validate and reserve the secret without including it in public delivery.
    (void)text(d, "flag");
    b.nodes(9, 2);
    auto user = integer(d, "user_id");
    auto comment = event_string(d, "comment", b);
    if (text(d, "request_type") == "friend")
      return ReceivedFriendRequest{header, user, std::move(comment)};
    if (text(d, "request_type") == "group")
      return ReceivedGroupRequest{
          header,
          enum_value<GroupRequestKind>(d, "sub_type",
                                       {{"add", GroupRequestKind::Add},
                                        {"invite", GroupRequestKind::Invite}}),
          integer(d, "group_id"), user, std::move(comment)};
    throw std::invalid_argument("unsupported event request kind");
  }
  if (post != "message")
    throw std::invalid_argument("unsupported event kind");
  b.nodes(14, 2);
  auto id = event_int32(d, "message_id");
  auto user = integer(d, "user_id");
  auto font = event_int32(d, "font");
  auto sender = event_sender(field(d, "sender"), b);
  auto content = event_content(field(d, "message"), b);
  auto raw = event_secret(d, "raw_message", b);
  if (text(d, "message_type") == "private")
    return PrivateMessageObservation{
        header,
        enum_value<PrivateMessageSubtype>(
            d, "sub_type",
            {{"friend", PrivateMessageSubtype::Friend},
             {"group", PrivateMessageSubtype::Group},
             {"other", PrivateMessageSubtype::Other}}),
        id,
        user,
        std::move(content),
        std::move(raw),
        font,
        std::move(sender)};
  if (text(d, "message_type") != "group")
    throw std::invalid_argument("unsupported event message kind");
  auto subtype = enum_value<GroupMessageSubtype>(
      d, "sub_type",
      {{"normal", GroupMessageSubtype::Normal},
       {"anonymous", GroupMessageSubtype::Anonymous},
       {"notice", GroupMessageSubtype::Notice}});
  const auto &anon = field(d, "anonymous");
  std::optional<EventAnonymous> anonymous;
  if (!anon.is_null()) {
    if (!anon.is_object())
      throw std::invalid_argument("event anonymous must be object or null");
    (void)text(anon, "flag");
    anonymous.emplace(
        EventAnonymous{integer(anon, "id"), event_string(anon, "name", b)});
  }
  if ((subtype == GroupMessageSubtype::Anonymous) != anonymous.has_value())
    throw std::invalid_argument("event anonymous subtype conflict");
  return GroupMessageObservation{header,
                                 subtype,
                                 id,
                                 integer(d, "group_id"),
                                 user,
                                 std::move(anonymous),
                                 std::move(content),
                                 std::move(raw),
                                 font,
                                 std::move(sender)};
}

} // namespace obcx::onebot11::bot::standard::detail
