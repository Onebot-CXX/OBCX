#include "onebot11/bot/platform_adapter.hpp"
#include "standard_composition_codec.hpp"
#include "standard_content_budget.hpp"
#include "standard_event_action_codec.hpp"
#include <cmath>
#include <type_traits>

namespace obcx::onebot11::bot::standard::detail {
namespace {
using Json = common::json;
void require(bool ok) {
  if (!ok)
    throw std::invalid_argument("invalid standard event action");
}
void validate_duration(const DurationSeconds &duration) {
  std::visit(
      [](auto value) {
        if constexpr (std::is_floating_point_v<decltype(value)>)
          require(std::isfinite(value));
        require(value >= 0);
      },
      duration.value);
}
auto duration_value(const DurationSeconds &duration) -> Json {
  return std::visit([](auto value) -> Json { return value; }, duration.value);
}
void inspect_event_action(ContentBudget &b, const Json &j, std::size_t depth) {
  b.node(depth);
  if (j.is_string())
    b.string(j.get_ref<const std::string &>());
  else if (j.is_object() || j.is_array()) {
    for (auto it = j.begin(); it != j.end(); ++it) {
      if (j.is_object())
        b.string(it.key());
      inspect_event_action(b, it.value(), depth + 1);
    }
  }
}
void source_kind(const EventActionHandle &h, EventActionSource kind) {
  require(StandardEventAccess::context(h).source == kind);
}
auto source_number(const EventActionHandle &h, std::string_view key)
    -> std::int64_t {
  return StandardEventAccess::context(h).native.at(key).get<std::int64_t>();
}
auto source_target(const EventActionHandle &h) -> obcx::bot::ConversationRef {
  const auto &c = StandardEventAccess::context(h);
  const bool group = c.source == EventActionSource::GroupMessage;
  require(group || c.source == EventActionSource::PrivateMessage);
  auto id = source_number(h, group ? "group_id" : "user_id");
  require(id > 0);
  QqPlatformAdapter binding;
  binding.codec().validate(c.installation);
  return {c.installation,
          binding.codec().make<obcx::bot::ConversationId>(
              group ? "group" : "private", {std::to_string(id)})};
}
template <typename Request>
void preflight_event_action(const Request &r, ContentBudget &b) {
  r.control.validate();
  r.value_limits.validate();
  inspect_installation(b, r.parameters.source.installation(), 0);
  if (r.control.correlation)
    b.string(r.control.correlation->attempt_id);
  // Borrowed authentic source and its possible native copy are bounded BEFORE
  // materialization. These local value bounds are not aggregate memory claims.
  const auto &native = StandardEventAccess::context(r.parameters.source).native;
  inspect_event_action(b, native, 0);
  inspect_event_action(b, native, 1);
}
void boolean_field(Json *out, const char *key, std::optional<bool> v,
                   ContentBudget &b) {
  if (v) {
    b.string(key);
    b.node(2);
    if (out)
      (*out)[key] = *v;
  }
}
void text_field(Json *out, const char *key, const std::optional<std::string> &v,
                ContentBudget &b) {
  if (v) {
    b.string(key);
    b.string_value(*v, 2);
    if (out)
      (*out)[key] = *v;
  }
}
template <typename Operation>
void quick_reply(Json *out, const Operation &op, const EventActionHandle &h,
                 ContentBudget &b) {
  boolean_field(out, "auto_escape", op.auto_escape, b);
  if (!op.reply)
    return;
  const auto target = source_target(h);
  QqPlatformAdapter binding;
  // operation.reply is at depth two, unlike ordinary message parameters.
  // Both paths use the same ledger and authoritative composition preflight.
  b.string("reply");
  if (out) {
    auto encoded = encode_composition(*op.reply, target, binding.codec(), b, 2);
    if (encoded.message.is_string())
      require(op.auto_escape.value_or(false) == encoded.auto_escape);
    (*out)["reply"] = std::move(encoded.message);
  } else {
    const bool auto_escape =
        validate_composition(*op.reply, target, binding.codec(), b, 2);
    const bool string_reply =
        auto_escape ||
        std::get<Composition>(*op.reply).sequences.front().encoding ==
            ContentEncoding::CqString;
    if (string_reply)
      require(op.auto_escape.value_or(false) == auto_escape);
  }
}
} // namespace

void validate_event_parameters(const SetGroupAnonymousBanParameters &p) {
  source_kind(p.source, EventActionSource::GroupMessage);
  const auto &d = StandardEventAccess::context(p.source).native;
  require(d.at("sub_type") == "anonymous" && d.at("anonymous").is_object());
  require(source_number(p.source, "group_id") > 0);
  validate_duration(p.duration);
  require(p.form == AnonymousBanForm::Object ||
          p.form == AnonymousBanForm::AnonymousFlag ||
          p.form == AnonymousBanForm::Flag);
}
void validate_event_parameters(const SetFriendAddRequestParameters &p) {
  source_kind(p.source, EventActionSource::FriendRequest);
  require(source_number(p.source, "user_id") > 0);
}
void validate_event_parameters(const SetGroupAddRequestParameters &p) {
  source_kind(p.source, EventActionSource::GroupRequest);
  require(source_number(p.source, "group_id") > 0 &&
          source_number(p.source, "user_id") > 0);
}
void validate_event_parameters(const HandleQuickOperationParameters &p) {
  std::visit(
      [&](const auto &op) {
        using T = std::decay_t<decltype(op)>;
        if constexpr (std::same_as<T, PrivateMessageQuickOperation>) {
          source_kind(p.source, EventActionSource::PrivateMessage);
          require(source_number(p.source, "user_id") > 0);
        } else if constexpr (std::same_as<T, GroupMessageQuickOperation>) {
          source_kind(p.source, EventActionSource::GroupMessage);
          require(source_number(p.source, "group_id") > 0);
          const auto &d = StandardEventAccess::context(p.source).native;
          if (op.reply && op.at_sender.value_or(true) &&
              d.at("sub_type") != "anonymous")
            require(source_number(p.source, "user_id") > 0);
          if (op.kick.value_or(false))
            require(d.at("sub_type") == "normal" &&
                    source_number(p.source, "user_id") > 0);
          if (op.ban.value_or(false))
            require(d.at("sub_type") != "notice" &&
                    (d.at("sub_type") == "anonymous" ||
                     source_number(p.source, "user_id") > 0));
          if (op.delete_message.value_or(false))
            require(source_number(p.source, "message_id") != -1);
          if (op.ban_duration)
            validate_duration(*op.ban_duration);
        } else if constexpr (std::same_as<T, FriendRequestQuickOperation>) {
          source_kind(p.source, EventActionSource::FriendRequest);
          require(source_number(p.source, "user_id") > 0);
        } else {
          source_kind(p.source, EventActionSource::GroupRequest);
          require(source_number(p.source, "group_id") > 0 &&
                  source_number(p.source, "user_id") > 0);
        }
      },
      p.operation);
}

namespace {
// A null sink performs the identical borrowed bounds/field checks, without
// constructing native parameters. Only Endpoint encoding supplies a sink.
void project_event_action(const SetGroupAnonymousBanRequest &r, Json *out) {
  validate_event_parameters(r.parameters);
  ContentBudget b{r.value_limits};
  preflight_event_action(r, b);
  const auto &p = r.parameters;
  const auto &native = StandardEventAccess::context(p.source).native;
  b.node(0);
  b.string("group_id");
  b.node(1);
  b.string("duration");
  b.node(1);
  if (out)
    *out = {{"group_id", source_number(p.source, "group_id")},
            {"duration", duration_value(p.duration)}};
  if (p.form == AnonymousBanForm::Object) {
    b.string("anonymous");
    if (out)
      (*out)["anonymous"] = native.at("anonymous");
  } else {
    const char *key =
        p.form == AnonymousBanForm::Flag ? "flag" : "anonymous_flag";
    b.string(key);
    const auto &flag = native.at("anonymous").at("flag");
    if (out)
      (*out)[key] = flag;
  }
}
void project_event_action(const SetFriendAddRequestRequest &r, Json *out) {
  validate_event_parameters(r.parameters);
  ContentBudget b{r.value_limits};
  preflight_event_action(r, b);
  const auto &p = r.parameters;
  b.node(0);
  b.string("flag");
  b.string("approve");
  b.node(1);
  b.string("remark");
  b.string_value(p.remark, 1);
  const auto &flag = StandardEventAccess::context(p.source).native.at("flag");
  if (out)
    *out = {{"flag", flag}, {"approve", p.approve}, {"remark", p.remark}};
}
void project_event_action(const SetGroupAddRequestRequest &r, Json *out) {
  validate_event_parameters(r.parameters);
  ContentBudget b{r.value_limits};
  preflight_event_action(r, b);
  const auto &p = r.parameters;
  const auto &native = StandardEventAccess::context(p.source).native;
  b.node(0);
  b.string("flag");
  b.string("sub_type");
  b.string("approve");
  b.node(1);
  b.string("reason");
  b.string_value(p.reason, 1);
  const auto &flag = native.at("flag");
  const auto &sub_type = native.at("sub_type");
  if (out)
    *out = {{"flag", flag},
            {"sub_type", sub_type},
            {"approve", p.approve},
            {"reason", p.reason}};
}
void project_event_action(const HandleQuickOperationRequest &r, Json *out) {
  validate_event_parameters(r.parameters);
  ContentBudget b{r.value_limits};
  preflight_event_action(r, b);
  b.node(0);
  b.string("context");
  b.string("operation");
  b.node(1);
  Json operation;
  Json *fields = nullptr;
  if (out) {
    operation = Json::object();
    fields = &operation;
  }
  std::visit(
      [&](const auto &op) {
        using T = std::decay_t<decltype(op)>;
        if constexpr (std::same_as<T, PrivateMessageQuickOperation> ||
                      std::same_as<T, GroupMessageQuickOperation>) {
          quick_reply(fields, op, r.parameters.source, b);
          if constexpr (std::same_as<T, GroupMessageQuickOperation>) {
            boolean_field(fields, "at_sender", op.at_sender, b);
            boolean_field(fields, "delete", op.delete_message, b);
            boolean_field(fields, "kick", op.kick, b);
            boolean_field(fields, "ban", op.ban, b);
            if (op.ban_duration) {
              b.string("ban_duration");
              b.node(2);
              if (fields)
                (*fields)["ban_duration"] = duration_value(*op.ban_duration);
            }
          }
        } else {
          boolean_field(fields, "approve", op.approve, b);
          if constexpr (std::same_as<T, FriendRequestQuickOperation>)
            text_field(fields, "remark", op.remark, b);
          else
            text_field(fields, "reason", op.reason, b);
        }
      },
      r.parameters.operation);
  if (out) {
    *out = Json::object();
    (*out)["context"] =
        StandardEventAccess::context(r.parameters.source).native;
    (*out)["operation"] = std::move(operation);
  }
}
} // namespace
void validate_event_action(const SetGroupAnonymousBanRequest &r) {
  project_event_action(r, nullptr);
}
auto encode_event_action(const SetGroupAnonymousBanRequest &r) -> Json {
  validate_event_action(r);
  Json result;
  project_event_action(r, &result);
  return result;
}
void validate_event_action(const SetFriendAddRequestRequest &r) {
  project_event_action(r, nullptr);
}
auto encode_event_action(const SetFriendAddRequestRequest &r) -> Json {
  validate_event_action(r);
  Json result;
  project_event_action(r, &result);
  return result;
}
void validate_event_action(const SetGroupAddRequestRequest &r) {
  project_event_action(r, nullptr);
}
auto encode_event_action(const SetGroupAddRequestRequest &r) -> Json {
  validate_event_action(r);
  Json result;
  project_event_action(r, &result);
  return result;
}
void validate_event_action(const HandleQuickOperationRequest &r) {
  project_event_action(r, nullptr);
}
auto encode_event_action(const HandleQuickOperationRequest &r) -> Json {
  validate_event_action(r);
  Json result;
  project_event_action(r, &result);
  return result;
}
auto quick_operation_target(const HandleQuickOperationRequest &r)
    -> std::optional<obcx::bot::ConversationRef> {
  return std::visit(
      [&](const auto &op) -> std::optional<obcx::bot::ConversationRef> {
        if constexpr (requires { op.reply; }) {
          if (op.reply)
            return source_target(r.parameters.source);
        }
        return std::nullopt;
      },
      r.parameters.operation);
}
auto quick_operation_effect(const HandleQuickOperationRequest &r,
                            const HandleQuickOperationOutcome &outcome)
    -> std::optional<obcx::bot::SendOutcome> {
  if (!quick_operation_target(r))
    return std::nullopt;
  outcome.validate();
  const bool possible = outcome.evidence.submission != Submission::NotSubmitted;
  // Even Completed contains no message ID or confirmed output. Preserve that
  // uncertainty separately from the genuine native no-data acknowledgment.
  return obcx::bot::send_failure(
      {.code = possible ? obcx::bot::BotOperationErrorCode::OutcomeUnknown
                        : obcx::bot::BotOperationErrorCode::TransportFailure,
       .message = "quick operation reply has no confirmed message output",
       .retryable = false,
       .submission_safety =
           possible ? obcx::bot::SubmissionSafety::PossiblySubmitted
                    : obcx::bot::SubmissionSafety::DefinitelyNotSubmitted},
      {}, possible);
}
} // namespace obcx::onebot11::bot::standard::detail
