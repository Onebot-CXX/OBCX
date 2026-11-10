#include "standard_mutation_codec.hpp"

#include "onebot11/bot/platform_adapter.hpp"

#include <charconv>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace obcx::onebot11::bot::standard {
namespace {
namespace bot_model = obcx::bot;

// Always use the genuine QQ codec, including its canonical spelling/ranges.
// These helpers only inspect existing references; they never mint new ones.
auto native_id(const auto &id) -> std::int64_t {
  const auto parts = QqNativeReferenceCodec{}.unpack(id);
  std::int64_t result;
  const auto &text = parts.components.front();
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), result);
  if (error != std::errc{} || end != text.data() + text.size())
    throw std::invalid_argument("invalid mutation reference identity");
  return result;
}

void validate_group(const bot_model::ConversationRef &group) {
  const QqNativeReferenceCodec codec;
  codec.validate(group);
  if (codec.unpack(group.id).kind != "group")
    throw std::invalid_argument("mutation requires group scope");
}

void validate_group_user(const bot_model::ConversationRef &group,
                         const bot_model::UserRef &user) {
  validate_group(group);
  QqNativeReferenceCodec{}.validate(user);
  if (group.installation != user.installation)
    throw std::invalid_argument("mutation reference installation mismatch");
}

void validate_installation(const bot_model::BotInstallationRef &expected,
                           const bot_model::BotInstallationRef &actual) {
  QqNativeReferenceCodec{}.validate(expected);
  if (expected != actual)
    throw std::invalid_argument("mutation target installation mismatch");
}

void validate_text(std::string_view text) {
  if (text.size() > bot_model::detail::bot_maximum_bytes)
    throw std::invalid_argument("mutation text exceeds hard bound");
  bot_model::detail::validate_utf8(text);
}

void validate_duration(const DurationSeconds &duration, bool title) {
  std::visit(
      [title](auto seconds) {
        if constexpr (std::is_same_v<decltype(seconds), double>) {
          if (!std::isfinite(seconds))
            throw std::invalid_argument("duration must be finite");
        }
        if (seconds < 0 && !(title && seconds == -1))
          throw std::invalid_argument("invalid duration");
      },
      duration.value);
}

using ParameterValue =
    std::variant<std::int64_t, double, bool, std::string_view>;
struct Parameter {
  std::string_view key;
  ParameterValue value;
};

auto duration_value(const DurationSeconds &duration) -> ParameterValue {
  return std::visit([](auto seconds) -> ParameterValue { return seconds; },
                    duration.value);
}

// One ledger covers the actual borrowed DTOs and generated JSON. DTO member
// names are not strings; only generated JSON keys consume key bytes. Opaque
// references are charged before existing validation can decode them. Internal
// bounded codec scratch is not a ValueLimits projection: total preparation,
// residency and escaped envelope accounting remain CallLimits integration work.
class MutationValueBudget {
public:
  explicit MutationValueBudget(const ValueLimits &limits) : limits_(limits) {
    limits_.validate();
  }

  void value(std::size_t depth) {
    if (depth > limits_.maximum_depth || depth > 32 ||
        nodes_ >= limits_.maximum_nodes)
      throw std::invalid_argument("mutation values exceed shape limits");
    hard_budget_.nodes();
    ++nodes_;
  }

  void text(std::string_view text) {
    if (text.size() > limits_.maximum_text_bytes ||
        text.size() > limits_.maximum_bytes - bytes_)
      throw std::invalid_argument("mutation values exceed text or byte limit");
    hard_budget_.bytes(text.size());
    bytes_ += text.size();
  }

  void string(std::string_view text, std::size_t depth) {
    value(depth);
    this->text(text);
  }

  void installation(const bot_model::BotInstallationRef &installation,
                    std::size_t depth) {
    value(depth);
    string(installation.installation_id, depth + 1);
    string(installation.surface.value(), depth + 1);
  }

  void reference(const bot_model::UserRef &user, std::size_t depth) {
    value(depth);
    installation(user.installation, depth + 1);
    string(user.id.serialized(), depth + 1);
  }

  void reference(const bot_model::ConversationRef &conversation,
                 std::size_t depth) {
    value(depth);
    installation(conversation.installation, depth + 1);
    string(conversation.id.serialized(), depth + 1);
  }

  void reference(const bot_model::MessageRef &message, std::size_t depth) {
    value(depth);
    reference(message.conversation, depth + 1);
    string(message.id.serialized(), depth + 1);
  }

  void duration(std::size_t depth) {
    value(depth);     // DurationSeconds wrapper.
    value(depth + 1); // The active numeric alternative, without a variant node.
  }

private:
  const ValueLimits &limits_;
  bot_model::detail::BotWireBudget hard_budget_;
  std::size_t nodes_{};
  std::size_t bytes_{};
};

auto begin_parameters(const ValueLimits &limits,
                      const bot_model::BotInstallationRef &installation)
    -> MutationValueBudget {
  MutationValueBudget budget(limits);
  // Two borrowed roots: selected installation and parameter DTO. Neither
  // invents an enclosing serialized call object or hypothetical field keys.
  budget.installation(installation, 0);
  budget.value(0);
  return budget;
}

auto encode_fields(MutationValueBudget &budget,
                   std::initializer_list<Parameter> fields) -> common::json {
  budget.value(0);
  for (const auto &field : fields) {
    budget.text(field.key);
    budget.value(1);
    if (const auto *text = std::get_if<std::string_view>(&field.value))
      budget.text(*text);
  }
  auto result = common::json::object();
  for (const auto &field : fields)
    std::visit(
        [&](const auto &value) { result[std::string(field.key)] = value; },
        field.value);
  return result;
}
} // namespace

void SendLikeParameters::validate() const {
  QqNativeReferenceCodec{}.validate(user);
  if (times < 0)
    throw std::invalid_argument("times must be nonnegative");
}

void DeleteMsgParameters::validate() const {
  QqNativeReferenceCodec{}.validate(message);
  const auto id = native_id(message.id);
  if (id < std::numeric_limits<std::int32_t>::min() ||
      id > std::numeric_limits<std::int32_t>::max())
    throw std::invalid_argument("message_id exceeds int32 range");
}

void SetGroupKickParameters::validate() const {
  validate_group_user(group, user);
}

void SetGroupBanParameters::validate() const {
  validate_group_user(group, user);
  validate_duration(duration, false);
}

void SetGroupWholeBanParameters::validate() const { validate_group(group); }

void SetGroupAdminParameters::validate() const {
  validate_group_user(group, user);
}

void SetGroupAnonymousParameters::validate() const { validate_group(group); }

void SetGroupCardParameters::validate() const {
  validate_group_user(group, user);
  validate_text(card);
}

void SetGroupNameParameters::validate() const {
  validate_group(group);
  validate_text(group_name);
}

void SetGroupLeaveParameters::validate() const { validate_group(group); }

void SetGroupSpecialTitleParameters::validate() const {
  validate_group_user(group, user);
  validate_text(special_title);
  validate_duration(duration, true);
}

namespace detail {
auto encode_parameters(const SendLikeParameters &parameters,
                       const bot_model::BotInstallationRef &installation,
                       const ValueLimits &limits) -> common::json {
  auto budget = begin_parameters(limits, installation);
  budget.reference(parameters.user, 1);
  budget.value(1); // times
  parameters.validate();
  validate_installation(installation, parameters.user.installation);
  return encode_fields(budget, {{"user_id", native_id(parameters.user.id)},
                                {"times", parameters.times}});
}

auto encode_parameters(const DeleteMsgParameters &parameters,
                       const bot_model::BotInstallationRef &installation,
                       const ValueLimits &limits) -> common::json {
  auto budget = begin_parameters(limits, installation);
  budget.reference(parameters.message, 1);
  parameters.validate();
  validate_installation(installation,
                        parameters.message.conversation.installation);
  return encode_fields(budget,
                       {{"message_id", native_id(parameters.message.id)}});
}

auto encode_parameters(const SetGroupKickParameters &parameters,
                       const bot_model::BotInstallationRef &installation,
                       const ValueLimits &limits) -> common::json {
  auto budget = begin_parameters(limits, installation);
  budget.reference(parameters.group, 1);
  budget.reference(parameters.user, 1);
  budget.value(1); // reject_add_request
  parameters.validate();
  validate_installation(installation, parameters.group.installation);
  return encode_fields(budget,
                       {{"group_id", native_id(parameters.group.id)},
                        {"user_id", native_id(parameters.user.id)},
                        {"reject_add_request", parameters.reject_add_request}});
}

auto encode_parameters(const SetGroupBanParameters &parameters,
                       const bot_model::BotInstallationRef &installation,
                       const ValueLimits &limits) -> common::json {
  auto budget = begin_parameters(limits, installation);
  budget.reference(parameters.group, 1);
  budget.reference(parameters.user, 1);
  budget.duration(1);
  parameters.validate();
  validate_installation(installation, parameters.group.installation);
  return encode_fields(budget,
                       {{"group_id", native_id(parameters.group.id)},
                        {"user_id", native_id(parameters.user.id)},
                        {"duration", duration_value(parameters.duration)}});
}

auto encode_parameters(const SetGroupWholeBanParameters &parameters,
                       const bot_model::BotInstallationRef &installation,
                       const ValueLimits &limits) -> common::json {
  auto budget = begin_parameters(limits, installation);
  budget.reference(parameters.group, 1);
  budget.value(1); // enable
  parameters.validate();
  validate_installation(installation, parameters.group.installation);
  return encode_fields(budget, {{"group_id", native_id(parameters.group.id)},
                                {"enable", parameters.enable}});
}

auto encode_parameters(const SetGroupAdminParameters &parameters,
                       const bot_model::BotInstallationRef &installation,
                       const ValueLimits &limits) -> common::json {
  auto budget = begin_parameters(limits, installation);
  budget.reference(parameters.group, 1);
  budget.reference(parameters.user, 1);
  budget.value(1); // enable
  parameters.validate();
  validate_installation(installation, parameters.group.installation);
  return encode_fields(budget, {{"group_id", native_id(parameters.group.id)},
                                {"user_id", native_id(parameters.user.id)},
                                {"enable", parameters.enable}});
}

auto encode_parameters(const SetGroupAnonymousParameters &parameters,
                       const bot_model::BotInstallationRef &installation,
                       const ValueLimits &limits) -> common::json {
  auto budget = begin_parameters(limits, installation);
  budget.reference(parameters.group, 1);
  budget.value(1); // enable
  parameters.validate();
  validate_installation(installation, parameters.group.installation);
  return encode_fields(budget, {{"group_id", native_id(parameters.group.id)},
                                {"enable", parameters.enable}});
}

auto encode_parameters(const SetGroupCardParameters &parameters,
                       const bot_model::BotInstallationRef &installation,
                       const ValueLimits &limits) -> common::json {
  auto budget = begin_parameters(limits, installation);
  budget.reference(parameters.group, 1);
  budget.reference(parameters.user, 1);
  budget.string(parameters.card, 1);
  parameters.validate();
  validate_installation(installation, parameters.group.installation);
  return encode_fields(budget, {{"group_id", native_id(parameters.group.id)},
                                {"user_id", native_id(parameters.user.id)},
                                {"card", std::string_view(parameters.card)}});
}

auto encode_parameters(const SetGroupNameParameters &parameters,
                       const bot_model::BotInstallationRef &installation,
                       const ValueLimits &limits) -> common::json {
  auto budget = begin_parameters(limits, installation);
  budget.reference(parameters.group, 1);
  budget.string(parameters.group_name, 1);
  parameters.validate();
  validate_installation(installation, parameters.group.installation);
  return encode_fields(
      budget, {{"group_id", native_id(parameters.group.id)},
               {"group_name", std::string_view(parameters.group_name)}});
}

auto encode_parameters(const SetGroupLeaveParameters &parameters,
                       const bot_model::BotInstallationRef &installation,
                       const ValueLimits &limits) -> common::json {
  auto budget = begin_parameters(limits, installation);
  budget.reference(parameters.group, 1);
  budget.value(1); // is_dismiss
  parameters.validate();
  validate_installation(installation, parameters.group.installation);
  return encode_fields(budget, {{"group_id", native_id(parameters.group.id)},
                                {"is_dismiss", parameters.is_dismiss}});
}

auto encode_parameters(const SetGroupSpecialTitleParameters &parameters,
                       const bot_model::BotInstallationRef &installation,
                       const ValueLimits &limits) -> common::json {
  auto budget = begin_parameters(limits, installation);
  budget.reference(parameters.group, 1);
  budget.reference(parameters.user, 1);
  budget.string(parameters.special_title, 1);
  budget.duration(1);
  parameters.validate();
  validate_installation(installation, parameters.group.installation);
  return encode_fields(
      budget, {{"group_id", native_id(parameters.group.id)},
               {"user_id", native_id(parameters.user.id)},
               {"special_title", std::string_view(parameters.special_title)},
               {"duration", duration_value(parameters.duration)}});
}
} // namespace detail
} // namespace obcx::onebot11::bot::standard
