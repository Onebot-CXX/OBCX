#include "onebot11/bot/standard_operations.hpp"
#include "standard_event_action_codec.hpp"
#include "standard_execution_codec.hpp"
#include "standard_send_codec.hpp"

#include "onebot11/bot/actions.hpp"
#include "onebot11/bot/platform_adapter.hpp"
#include "standard_lookup_codec.hpp"
#include "standard_operation_binding.hpp"

#include <cmath>
#include <limits>
#include <ratio>
#include <type_traits>
#include <utility>

namespace obcx::onebot11::bot::standard {
namespace {
namespace bot_model = obcx::bot;
using Clock = std::chrono::steady_clock;

class RequestBudget {
public:
  explicit RequestBudget(const ValueLimits &limits) : limits_(limits) {
    limits_.validate();
  }
  void value(std::size_t depth) {
    if (depth > limits_.maximum_depth || nodes_ >= limits_.maximum_nodes)
      throw std::invalid_argument(
          "standard request exceeds value shape limits");
    ++nodes_;
  }
  void bytes(std::size_t count) {
    if (count > limits_.maximum_bytes - bytes_)
      throw std::invalid_argument("standard request exceeds value byte limit");
    bytes_ += count;
  }
  void text(std::string_view text) {
    if (text.size() > limits_.maximum_text_bytes)
      throw std::invalid_argument("standard request exceeds text limit");
    bytes(text.size());
    bot_model::detail::validate_utf8(text);
  }
  void inspect(const bot_model::Json &wire, std::size_t depth) {
    value(depth);
    if (wire.is_string())
      text(wire.get_ref<const std::string &>());
    else if (wire.is_binary())
      bytes(wire.get_binary().size());
    else if (wire.is_structured()) {
      for (auto it = wire.begin(); it != wire.end(); ++it) {
        if (wire.is_object())
          text(it.key());
        inspect(it.value(), depth + 1);
      }
    } else if (wire.is_discarded() ||
               (wire.is_number_float() && !std::isfinite(wire.get<double>())))
      throw std::invalid_argument("invalid standard request value");
  }

private:
  const ValueLimits &limits_;
  std::size_t nodes_{};
  std::size_t bytes_{};
};

struct RequestText {
  std::string_view installation;
  std::string_view surface;
  std::string_view user;
  std::optional<std::string_view> correlation;
};

auto request_text(const SendLikeRequest &request) -> RequestText {
  return {request.parameters.user.installation.installation_id,
          request.parameters.user.installation.surface.value(),
          request.parameters.user.id.serialized(),
          request.control.correlation
              ? std::optional<std::string_view>{request.control.correlation
                                                    ->attempt_id}
              : std::nullopt};
}

auto execution_name(ExecutionMode mode) -> std::string_view {
  switch (mode) {
  case ExecutionMode::Direct:
    return "direct";
  case ExecutionMode::Async:
    return "async";
  case ExecutionMode::RateLimited:
    return "rate_limited";
  }
  throw std::invalid_argument("invalid standard execution mode");
}

template <typename Result>
void validate_outcome_mode(ExecutionMode mode,
                           const ExecutionOutcome<Result> &outcome) {
  (void)execution_name(mode);
  outcome.validate();
  // Native completion must match the request even in a recovered failure.
  if (outcome.evidence.completion ==
          NativeCompletion::ProviderReportedCompleted &&
      mode != ExecutionMode::Direct)
    throw std::invalid_argument("standard completion mode mismatch");
  if (const auto *pending = std::get_if<AcceptedPending>(&outcome.state);
      pending && pending->requested_mode != mode)
    throw std::invalid_argument("standard pending mode mismatch");
}

// DTO projections have logical containers/scalars and retained strings, not
// C++ field-name bytes or variant tags. Only generated JSON pays for keys and
// the execution spelling. One ledger spans both sides of each codec call.
void projection(RequestBudget &budget, const RequestText &text,
                ExecutionMode mode, bool json) {
  const auto field = [&](std::string_view key, std::size_t depth) {
    if (json)
      budget.text(key);
    budget.value(depth);
  };
  budget.value(0);
  field("parameters", 1);
  field("user", 2);
  field("installation", 3);
  field("installation_id", 4);
  budget.text(text.installation);
  field("surface", 4);
  budget.text(text.surface);
  field("id", 3);
  budget.text(text.user);
  field("times", 2);
  field("control", 1);
  field("execution", 2);
  if (json)
    budget.text(execution_name(mode));
  field("limits", 2);
  field("maximum_parameter_bytes", 3);
  field("maximum_response_bytes", 3);
  field("timeout_milliseconds", 3);
  field("correlation", 2);
  if (text.correlation) {
    field("attempt_id", 3);
    budget.text(*text.correlation);
  }
  field("value_limits", 1);
  field("maximum_bytes", 2);
  field("maximum_nodes", 2);
  field("maximum_depth", 2);
  field("maximum_text_bytes", 2);
  field("deadline_ticks", 1);
}

void validate_fields(const SendLikeRequest &request) {
  request.control.validate();
  request.parameters.validate();
}

auto integer(const bot_model::Json &value) -> std::int64_t {
  if (value.is_number_unsigned()) {
    const auto n = value.get<std::uint64_t>();
    if (n > static_cast<std::uint64_t>(INT64_MAX))
      throw std::invalid_argument(
          "standard integer exceeds signed representation");
    return static_cast<std::int64_t>(n);
  }
  if (!value.is_number_integer())
    throw std::invalid_argument("standard field requires an integer");
  return value.get<std::int64_t>();
}

auto size_value(const bot_model::Json &value) -> std::size_t {
  const auto n = integer(value);
  if (n < 0 || !std::in_range<std::size_t>(n))
    throw std::invalid_argument("standard size exceeds representation");
  return static_cast<std::size_t>(n);
}

auto string(const bot_model::Json &value) -> const std::string & {
  if (!value.is_string())
    throw std::invalid_argument("standard field requires a string");
  return value.get_ref<const std::string &>();
}

auto value_limits(const bot_model::Json &value) -> ValueLimits {
  bot_model::detail::require_object(value, "standard value limits");
  ValueLimits limits{size_value(value.at("maximum_bytes")),
                     size_value(value.at("maximum_nodes")),
                     size_value(value.at("maximum_depth")),
                     size_value(value.at("maximum_text_bytes"))};
  limits.validate();
  return limits;
}

auto execution_mode(const bot_model::Json &value) -> ExecutionMode {
  const auto &name = string(value);
  if (name == "direct")
    return ExecutionMode::Direct;
  if (name == "async")
    return ExecutionMode::Async;
  if (name == "rate_limited")
    return ExecutionMode::RateLimited;
  throw std::invalid_argument("invalid standard execution spelling");
}

// Use checked wide integer intermediates before duration conversion/addition.
// The wire is exact steady-clock ticks in this process, including negative
// epochs. No saturation, wall-clock conversion or elapsed-deadline renewal.
using Tick = Clock::duration::rep;
static_assert(std::is_integral_v<Tick> && std::is_signed_v<Tick> &&
              sizeof(Tick) <= sizeof(std::int64_t));
auto timeout_ticks(std::chrono::milliseconds timeout) -> Tick {
  using Ratio = std::ratio_divide<std::chrono::milliseconds::period,
                                  Clock::duration::period>;
  const __int128 numerator =
      static_cast<__int128>(timeout.count()) * Ratio::num;
  const __int128 ticks = numerator / Ratio::den;
  if (timeout.count() <= 0 || numerator % Ratio::den != 0 || ticks <= 0 ||
      ticks > std::numeric_limits<Tick>::max())
    throw std::invalid_argument(
        "standard timeout exceeds clock representation");
  return static_cast<Tick>(ticks);
}

auto deadline_upper_bound(std::chrono::milliseconds timeout) -> __int128 {
  const auto ticks = timeout_ticks(timeout);
  return static_cast<__int128>(Clock::now().time_since_epoch().count()) + ticks;
}

void validate_preserved_deadline(Clock::time_point deadline,
                                 std::chrono::milliseconds timeout) {
  // A comparison bound need not fit Tick. Queue delay must not invalidate an
  // already representable deadline merely because a new deadline would
  // overflow.
  if (static_cast<__int128>(deadline.time_since_epoch().count()) >
      deadline_upper_bound(timeout))
    throw std::invalid_argument("standard deadline exceeds requested timeout");
}

auto deadline_after(std::chrono::milliseconds timeout) -> Clock::time_point {
  const auto absolute = deadline_upper_bound(timeout);
  if (absolute < std::numeric_limits<Tick>::min() ||
      absolute > std::numeric_limits<Tick>::max())
    throw std::invalid_argument(
        "standard deadline exceeds clock representation");
  return Clock::time_point{Clock::duration{static_cast<Tick>(absolute)}};
}

auto deadline_from_ticks(std::int64_t ticks) -> Clock::time_point {
  if (!std::in_range<Tick>(ticks))
    throw std::invalid_argument(
        "standard deadline exceeds clock representation");
  return Clock::time_point{Clock::duration{static_cast<Tick>(ticks)}};
}

// Borrowed reference views let decode account retained DTOs before invoking
// any reference codec or constructing owning text. Each occurrence is counted.
struct ReferenceText {
  std::string_view installation;
  std::string_view surface;
  std::string_view id;
};
auto reference_text(const bot_model::UserRef &ref) -> ReferenceText {
  return {ref.installation.installation_id, ref.installation.surface.value(),
          ref.id.serialized()};
}
auto reference_text(const bot_model::ConversationRef &ref) -> ReferenceText {
  return {ref.installation.installation_id, ref.installation.surface.value(),
          ref.id.serialized()};
}
auto reference_text(const bot_model::Json &ref) -> ReferenceText {
  const auto &installation = ref.at("installation");
  return {string(installation.at("installation_id")),
          string(installation.at("surface")), string(ref.at("id"))};
}
class MutationProjection {
public:
  MutationProjection(RequestBudget &budget, bool json)
      : budget_(budget), json_(json) {}
  void field(std::string_view key, std::size_t depth) {
    if (json_)
      budget_.text(key);
    budget_.value(depth);
  }
  void text(std::string_view key, std::string_view value, std::size_t depth) {
    field(key, depth);
    budget_.text(value);
  }
  void reference(std::string_view key, ReferenceText ref, std::size_t depth) {
    field(key, depth);
    field("installation", depth + 1);
    text("installation_id", ref.installation, depth + 2);
    text("surface", ref.surface, depth + 2);
    text("id", ref.id, depth + 1);
  }
  void message(const bot_model::MessageRef &ref) {
    field("message", 2);
    reference("conversation", reference_text(ref.conversation), 3);
    text("id", ref.id.serialized(), 3);
  }
  void message(const bot_model::Json &ref) {
    field("message", 2);
    reference("conversation", reference_text(ref.at("conversation")), 3);
    text("id", string(ref.at("id")), 3);
  }
  void envelope(ExecutionMode mode,
                std::optional<std::string_view> correlation) {
    field("control", 1);
    field("execution", 2);
    if (json_)
      budget_.text(execution_name(mode));
    field("limits", 2);
    field("maximum_parameter_bytes", 3);
    field("maximum_response_bytes", 3);
    field("timeout_milliseconds", 3);
    field("correlation", 2);
    if (correlation)
      text("attempt_id", *correlation, 3);
    field("value_limits", 1);
    field("maximum_bytes", 2);
    field("maximum_nodes", 2);
    field("maximum_depth", 2);
    field("maximum_text_bytes", 2);
    field("deadline_ticks", 1);
  }

private:
  RequestBudget &budget_;
  bool json_;
};
auto boolean(const bot_model::Json &value) -> bool {
  if (!value.is_boolean())
    throw std::invalid_argument("standard field requires a boolean");
  return value.get<bool>();
}
auto duration(const bot_model::Json &value) -> DurationSeconds {
  if (value.is_number_integer())
    return {integer(value)};
  if (!value.is_number_float() || !std::isfinite(value.get<double>()))
    throw std::invalid_argument("standard duration requires a finite number");
  return {value.get<double>()};
}
auto duration_json(const DurationSeconds &value) -> bot_model::Json {
  return std::visit([](auto seconds) -> bot_model::Json { return seconds; },
                    value.value);
}

void project_parameters(MutationProjection &projection,
                        const DeleteMsgParameters &parameters) {
  projection.message(parameters.message);
}

void project_parameters(MutationProjection &projection,
                        std::type_identity<DeleteMsgParameters>,
                        const bot_model::Json &parameters) {
  projection.message(parameters.at("message"));
}

void project_parameters(MutationProjection &projection,
                        const SetGroupKickParameters &parameters) {
  projection.reference("group", reference_text(parameters.group), 2);
  projection.reference("user", reference_text(parameters.user), 2);
  projection.field("reject_add_request", 2);
}

void project_parameters(MutationProjection &projection,
                        std::type_identity<SetGroupKickParameters>,
                        const bot_model::Json &parameters) {
  projection.reference("group", reference_text(parameters.at("group")), 2);
  projection.reference("user", reference_text(parameters.at("user")), 2);
  projection.field("reject_add_request", 2);
}

void project_parameters(MutationProjection &projection,
                        const SetGroupBanParameters &parameters) {
  projection.reference("group", reference_text(parameters.group), 2);
  projection.reference("user", reference_text(parameters.user), 2);
  projection.field("duration", 2);
}

void project_parameters(MutationProjection &projection,
                        std::type_identity<SetGroupBanParameters>,
                        const bot_model::Json &parameters) {
  projection.reference("group", reference_text(parameters.at("group")), 2);
  projection.reference("user", reference_text(parameters.at("user")), 2);
  projection.field("duration", 2);
}

void project_parameters(MutationProjection &projection,
                        const SetGroupWholeBanParameters &parameters) {
  projection.reference("group", reference_text(parameters.group), 2);
  projection.field("enable", 2);
}

void project_parameters(MutationProjection &projection,
                        std::type_identity<SetGroupWholeBanParameters>,
                        const bot_model::Json &parameters) {
  projection.reference("group", reference_text(parameters.at("group")), 2);
  projection.field("enable", 2);
}

void project_parameters(MutationProjection &projection,
                        const SetGroupAdminParameters &parameters) {
  projection.reference("group", reference_text(parameters.group), 2);
  projection.reference("user", reference_text(parameters.user), 2);
  projection.field("enable", 2);
}

void project_parameters(MutationProjection &projection,
                        std::type_identity<SetGroupAdminParameters>,
                        const bot_model::Json &parameters) {
  projection.reference("group", reference_text(parameters.at("group")), 2);
  projection.reference("user", reference_text(parameters.at("user")), 2);
  projection.field("enable", 2);
}

void project_parameters(MutationProjection &projection,
                        const SetGroupAnonymousParameters &parameters) {
  projection.reference("group", reference_text(parameters.group), 2);
  projection.field("enable", 2);
}

void project_parameters(MutationProjection &projection,
                        std::type_identity<SetGroupAnonymousParameters>,
                        const bot_model::Json &parameters) {
  projection.reference("group", reference_text(parameters.at("group")), 2);
  projection.field("enable", 2);
}

void project_parameters(MutationProjection &projection,
                        const SetGroupCardParameters &parameters) {
  projection.reference("group", reference_text(parameters.group), 2);
  projection.reference("user", reference_text(parameters.user), 2);
  projection.text("card", parameters.card, 2);
}

void project_parameters(MutationProjection &projection,
                        std::type_identity<SetGroupCardParameters>,
                        const bot_model::Json &parameters) {
  projection.reference("group", reference_text(parameters.at("group")), 2);
  projection.reference("user", reference_text(parameters.at("user")), 2);
  projection.text("card", string(parameters.at("card")), 2);
}

void project_parameters(MutationProjection &projection,
                        const SetGroupNameParameters &parameters) {
  projection.reference("group", reference_text(parameters.group), 2);
  projection.text("group_name", parameters.group_name, 2);
}

void project_parameters(MutationProjection &projection,
                        std::type_identity<SetGroupNameParameters>,
                        const bot_model::Json &parameters) {
  projection.reference("group", reference_text(parameters.at("group")), 2);
  projection.text("group_name", string(parameters.at("group_name")), 2);
}

void project_parameters(MutationProjection &projection,
                        const SetGroupLeaveParameters &parameters) {
  projection.reference("group", reference_text(parameters.group), 2);
  projection.field("is_dismiss", 2);
}

void project_parameters(MutationProjection &projection,
                        std::type_identity<SetGroupLeaveParameters>,
                        const bot_model::Json &parameters) {
  projection.reference("group", reference_text(parameters.at("group")), 2);
  projection.field("is_dismiss", 2);
}

void project_parameters(MutationProjection &projection,
                        const SetGroupSpecialTitleParameters &parameters) {
  projection.reference("group", reference_text(parameters.group), 2);
  projection.reference("user", reference_text(parameters.user), 2);
  projection.text("special_title", parameters.special_title, 2);
  projection.field("duration", 2);
}

void project_parameters(MutationProjection &projection,
                        std::type_identity<SetGroupSpecialTitleParameters>,
                        const bot_model::Json &parameters) {
  projection.reference("group", reference_text(parameters.at("group")), 2);
  projection.reference("user", reference_text(parameters.at("user")), 2);
  projection.text("special_title", string(parameters.at("special_title")), 2);
  projection.field("duration", 2);
}

template <typename Request>
void mutation_projection(RequestBudget &budget, const Request &request,
                         bool json) {
  budget.value(0);
  MutationProjection projection(budget, json);
  projection.field("parameters", 1);
  project_parameters(projection, request.parameters);
  projection.envelope(
      request.control.execution,
      request.control.correlation
          ? std::optional<std::string_view>{request.control.correlation
                                                ->attempt_id}
          : std::nullopt);
}
template <typename Request>
void validate_mutation_fields(const Request &request) {
  request.control.validate();
  request.parameters.validate();
}

auto query_honor_name(HonorKind kind) -> std::string_view {
  switch (kind) {
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
  throw std::invalid_argument("invalid standard honor kind");
}
auto query_honor_kind(const bot_model::Json &value) -> HonorKind {
  const auto &name = string(value);
  if (name == "talkative")
    return HonorKind::Talkative;
  if (name == "performer")
    return HonorKind::Performer;
  if (name == "legend")
    return HonorKind::Legend;
  if (name == "strong_newbie")
    return HonorKind::StrongNewbie;
  if (name == "emotion")
    return HonorKind::Emotion;
  if (name == "all")
    return HonorKind::All;
  throw std::invalid_argument("invalid standard honor spelling");
}
void query_scope_projection(MutationProjection &projection,
                            std::string_view installation,
                            std::string_view surface) {
  projection.field("installation", 2);
  projection.text("installation_id", installation, 3);
  projection.text("surface", surface, 3);
}
void validate_query_reference(const bot_model::UserRef &ref) {
  QqNativeReferenceCodec{}.validate(ref);
}
void validate_query_reference(const bot_model::ConversationRef &ref) {
  const QqNativeReferenceCodec codec;
  codec.validate(ref);
  if (codec.unpack(ref.id).kind != "group")
    throw std::invalid_argument("standard query requires group scope");
}
void project_query_parameters(MutationProjection &projection,
                              const GetStrangerInfoParameters &parameters,
                              bool json) {
  projection.reference("user", reference_text(parameters.user), 2);
  projection.field("no_cache", 2);
}
void project_query_parameters(MutationProjection &projection,
                              std::type_identity<GetStrangerInfoParameters>,
                              const bot_model::Json &parameters, bool json) {
  projection.reference("user", reference_text(parameters.at("user")), 2);
  projection.field("no_cache", 2);
}
void validate_query_parameters(const GetStrangerInfoParameters &p) {
  validate_query_reference(p.user);
}
void project_query_parameters(MutationProjection &projection,
                              const GetLoginInfoParameters &parameters,
                              bool json) {
  query_scope_projection(projection, parameters.installation.installation_id,
                         parameters.installation.surface.value());
}
void project_query_parameters(MutationProjection &projection,
                              std::type_identity<GetLoginInfoParameters>,
                              const bot_model::Json &parameters, bool json) {
  query_scope_projection(
      projection, string(parameters.at("installation").at("installation_id")),
      string(parameters.at("installation").at("surface")));
}
void validate_query_parameters(const GetLoginInfoParameters &p) {
  QqNativeReferenceCodec{}.validate(p.installation);
}
void project_query_parameters(MutationProjection &projection,
                              const GetFriendListParameters &parameters,
                              bool json) {
  query_scope_projection(projection, parameters.installation.installation_id,
                         parameters.installation.surface.value());
}
void project_query_parameters(MutationProjection &projection,
                              std::type_identity<GetFriendListParameters>,
                              const bot_model::Json &parameters, bool json) {
  query_scope_projection(
      projection, string(parameters.at("installation").at("installation_id")),
      string(parameters.at("installation").at("surface")));
}
void validate_query_parameters(const GetFriendListParameters &p) {
  QqNativeReferenceCodec{}.validate(p.installation);
}
void project_query_parameters(MutationProjection &projection,
                              const GetGroupInfoParameters &parameters,
                              bool json) {
  projection.reference("group", reference_text(parameters.group), 2);
  projection.field("no_cache", 2);
}
void project_query_parameters(MutationProjection &projection,
                              std::type_identity<GetGroupInfoParameters>,
                              const bot_model::Json &parameters, bool json) {
  projection.reference("group", reference_text(parameters.at("group")), 2);
  projection.field("no_cache", 2);
}
void validate_query_parameters(const GetGroupInfoParameters &p) {
  validate_query_reference(p.group);
}
void project_query_parameters(MutationProjection &projection,
                              const GetGroupListParameters &parameters,
                              bool json) {
  query_scope_projection(projection, parameters.installation.installation_id,
                         parameters.installation.surface.value());
}
void project_query_parameters(MutationProjection &projection,
                              std::type_identity<GetGroupListParameters>,
                              const bot_model::Json &parameters, bool json) {
  query_scope_projection(
      projection, string(parameters.at("installation").at("installation_id")),
      string(parameters.at("installation").at("surface")));
}
void validate_query_parameters(const GetGroupListParameters &p) {
  QqNativeReferenceCodec{}.validate(p.installation);
}
void project_query_parameters(MutationProjection &projection,
                              const GetGroupMemberInfoParameters &parameters,
                              bool json) {
  projection.reference("group", reference_text(parameters.group), 2);
  projection.reference("user", reference_text(parameters.user), 2);
  projection.field("no_cache", 2);
}
void project_query_parameters(MutationProjection &projection,
                              std::type_identity<GetGroupMemberInfoParameters>,
                              const bot_model::Json &parameters, bool json) {
  projection.reference("group", reference_text(parameters.at("group")), 2);
  projection.reference("user", reference_text(parameters.at("user")), 2);
  projection.field("no_cache", 2);
}
void validate_query_parameters(const GetGroupMemberInfoParameters &p) {
  validate_query_reference(p.group);
  validate_query_reference(p.user);
  if (p.group.installation != p.user.installation)
    throw std::invalid_argument("standard query installation mismatch");
}
void project_query_parameters(MutationProjection &projection,
                              const GetGroupMemberListParameters &parameters,
                              bool json) {
  projection.reference("group", reference_text(parameters.group), 2);
}
void project_query_parameters(MutationProjection &projection,
                              std::type_identity<GetGroupMemberListParameters>,
                              const bot_model::Json &parameters, bool json) {
  projection.reference("group", reference_text(parameters.at("group")), 2);
}
void validate_query_parameters(const GetGroupMemberListParameters &p) {
  validate_query_reference(p.group);
}
void project_query_parameters(MutationProjection &projection,
                              const GetGroupHonorInfoParameters &parameters,
                              bool json) {
  projection.reference("group", reference_text(parameters.group), 2);
  if (json)
    projection.text("type", query_honor_name(parameters.type), 2);
  else
    projection.field("type", 2);
}
void project_query_parameters(MutationProjection &projection,
                              std::type_identity<GetGroupHonorInfoParameters>,
                              const bot_model::Json &parameters, bool json) {
  projection.reference("group", reference_text(parameters.at("group")), 2);
  if (json)
    projection.text(
        "type", query_honor_name(query_honor_kind(parameters.at("type"))), 2);
  else
    projection.field("type", 2);
}
void validate_query_parameters(const GetGroupHonorInfoParameters &p) {
  validate_query_reference(p.group);
  (void)query_honor_name(p.type);
}
template <typename Request>
void query_projection(RequestBudget &budget, const Request &request,
                      bool json) {
  budget.value(0);
  MutationProjection projection(budget, json);
  projection.field("parameters", 1);
  project_query_parameters(projection, request.parameters, json);
  projection.envelope(
      request.control.execution,
      request.control.correlation
          ? std::optional<std::string_view>{request.control.correlation
                                                ->attempt_id}
          : std::nullopt);
}
template <typename Request> void validate_query_fields(const Request &request) {
  request.control.validate();
  validate_query_parameters(request.parameters);
}
// Borrowed typed projections mirror the native query decoder's retained DTO
// shape: root zero, no field-name bytes, absent optionals remain absent.
void query_result_scope(RequestBudget &budget,
                        const bot_model::BotInstallationRef &scope,
                        std::size_t depth) {
  budget.value(depth);
  budget.value(depth + 1);
  budget.text(scope.installation_id);
  budget.value(depth + 1);
  budget.text(scope.surface.value());
}
void query_result_reference(RequestBudget &budget, const auto &ref,
                            std::size_t depth) {
  budget.value(depth);
  query_result_scope(budget, ref.installation, depth + 1);
  budget.value(depth + 1);
  budget.text(ref.id.serialized());
}
void query_result_text(RequestBudget &budget, std::string_view text,
                       std::size_t depth) {
  budget.value(depth);
  budget.text(text);
}
void query_result_sex(Sex sex) {
  switch (sex) {
  case Sex::Male:
  case Sex::Female:
  case Sex::Unknown:
    return;
  }
  throw std::invalid_argument("invalid standard query sex");
}
void query_result_role(GroupRole role) {
  switch (role) {
  case GroupRole::Owner:
  case GroupRole::Administrator:
  case GroupRole::Member:
    return;
  }
  throw std::invalid_argument("invalid standard query role");
}
void project_query_result(RequestBudget &budget, const StrangerInfo &value,
                          std::size_t depth) {
  budget.value(depth);
  query_result_reference(budget, value.user, depth + 1);
  query_result_text(budget, value.nickname, depth + 1);
  budget.value(depth + 1);
  query_result_sex(value.sex);
  budget.value(depth + 1);
}
void project_query_result(RequestBudget &budget, const FriendInfo &value,
                          std::size_t depth) {
  budget.value(depth);
  query_result_reference(budget, value.user, depth + 1);
  query_result_text(budget, value.nickname, depth + 1);
  query_result_text(budget, value.remark, depth + 1);
}
void project_query_result(RequestBudget &budget, const GroupInfo &value,
                          std::size_t depth) {
  budget.value(depth);
  query_result_reference(budget, value.group, depth + 1);
  query_result_text(budget, value.group_name, depth + 1);
  budget.value(depth + 1);
  budget.value(depth + 1);
}
void project_query_fields(RequestBudget &budget, const MemberFields &value,
                          std::size_t depth) {
  budget.value(depth);
  query_result_text(budget, value.nickname, depth + 1);
  query_result_text(budget, value.card, depth + 1);
  budget.value(depth + 1);
  query_result_sex(value.sex);
  budget.value(depth + 1);
  query_result_text(budget, value.area, depth + 1);
  budget.value(depth + 1);
  budget.value(depth + 1);
  query_result_text(budget, value.level, depth + 1);
  budget.value(depth + 1);
  query_result_role(value.role);
  budget.value(depth + 1);
  query_result_text(budget, value.title, depth + 1);
  budget.value(depth + 1);
  budget.value(depth + 1);
}
void project_query_fields(RequestBudget &budget,
                          const ListedMemberFields &value, std::size_t depth) {
  budget.value(depth);
  if (value.nickname) {
    query_result_text(budget, *value.nickname, depth + 1);
  }
  if (value.card) {
    query_result_text(budget, *value.card, depth + 1);
  }
  if (value.sex) {
    budget.value(depth + 1);
    query_result_sex(*value.sex);
  }
  if (value.age) {
    budget.value(depth + 1);
  }
  if (value.area) {
    query_result_text(budget, *value.area, depth + 1);
  }
  if (value.join_time) {
    budget.value(depth + 1);
  }
  if (value.last_sent_time) {
    budget.value(depth + 1);
  }
  if (value.level) {
    query_result_text(budget, *value.level, depth + 1);
  }
  if (value.role) {
    budget.value(depth + 1);
    query_result_role(*value.role);
  }
  if (value.unfriendly) {
    budget.value(depth + 1);
  }
  if (value.title) {
    query_result_text(budget, *value.title, depth + 1);
  }
  if (value.title_expire_time) {
    budget.value(depth + 1);
  }
  if (value.card_changeable) {
    budget.value(depth + 1);
  }
}
void project_query_result(RequestBudget &budget, const GroupMemberInfo &value,
                          std::size_t depth) {
  budget.value(depth);
  query_result_reference(budget, value.group, depth + 1);
  query_result_reference(budget, value.user, depth + 1);
  project_query_fields(budget, value.fields, depth + 1);
}
void project_query_result(RequestBudget &budget, const ListedGroupMember &value,
                          std::size_t depth) {
  budget.value(depth);
  query_result_reference(budget, value.group, depth + 1);
  query_result_reference(budget, value.user, depth + 1);
  project_query_fields(budget, value.fields, depth + 1);
}
void project_query_result(RequestBudget &budget, const FriendList &value,
                          std::size_t depth) {
  budget.value(depth);
  query_result_scope(budget, value.installation, depth + 1);
  budget.value(depth + 1);
  for (const auto &entry : value.entries)
    project_query_result(budget, entry, depth + 2);
}
void project_query_result(RequestBudget &budget, const GroupList &value,
                          std::size_t depth) {
  budget.value(depth);
  query_result_scope(budget, value.installation, depth + 1);
  budget.value(depth + 1);
  for (const auto &entry : value.entries)
    project_query_result(budget, entry, depth + 2);
}
void project_query_result(RequestBudget &budget, const GroupMemberList &value,
                          std::size_t depth) {
  budget.value(depth);
  query_result_reference(budget, value.group, depth + 1);
  budget.value(depth + 1);
  for (const auto &entry : value.entries)
    project_query_result(budget, entry, depth + 2);
}
void project_query_result(RequestBudget &budget, const CurrentTalkative &value,
                          std::size_t depth) {
  budget.value(depth);
  query_result_reference(budget, value.user, depth + 1);
  query_result_text(budget, value.nickname, depth + 1);
  // Intentional internal validation only: the sensitive view never escapes.
  value.avatar.with_value(SensitiveUse::ExplicitAccess,
                          [&](std::string_view avatar) {
                            query_result_text(budget, avatar, depth + 1);
                          });
  budget.value(depth + 1);
}
void project_query_result(RequestBudget &budget, const HonorEntry &value,
                          std::size_t depth) {
  budget.value(depth);
  query_result_reference(budget, value.user, depth + 1);
  query_result_text(budget, value.nickname, depth + 1);
  // Intentional internal validation only: the sensitive view never escapes.
  value.avatar.with_value(SensitiveUse::ExplicitAccess,
                          [&](std::string_view avatar) {
                            query_result_text(budget, avatar, depth + 1);
                          });
  query_result_text(budget, value.description, depth + 1);
}
void project_query_result(RequestBudget &budget, const GroupHonorInfo &value,
                          std::size_t depth) {
  budget.value(depth);
  query_result_reference(budget, value.group, depth + 1);
  budget.value(depth + 1);
  (void)query_honor_name(value.requested_type);
  if (value.current_talkative)
    project_query_result(budget, *value.current_talkative, depth + 1);
  for (const auto *category :
       {&value.talkative_list, &value.performer_list, &value.legend_list,
        &value.strong_newbie_list, &value.emotion_list}) {
    if (!*category)
      continue;
    budget.value(depth + 1);
    for (const auto &entry : **category)
      project_query_result(budget, entry, depth + 2);
  }
}
void query_result_installation(const bot_model::BotInstallationRef &actual,
                               const bot_model::BotInstallationRef &expected) {
  QqNativeReferenceCodec{}.validate(actual);
  if (actual != expected)
    throw std::invalid_argument("standard query result installation mismatch");
}
void query_result_target(const auto &actual, const auto &expected) {
  if (actual != expected)
    throw std::invalid_argument("standard query result target mismatch");
}
void query_result_scoped_reference(const auto &ref,
                                   const bot_model::BotInstallationRef &scope) {
  validate_query_reference(ref);
  query_result_installation(ref.installation, scope);
}
void validate_query_result(const GetStrangerInfoRequest &request,
                           const StrangerInfo &value) {
  RequestBudget budget(request.value_limits);
  project_query_result(budget, value, 0);
  // Only after the entire borrowed result is bounded may reference validation
  // allocate decoding scratch. This is shape/canonical validation, not
  // authority.
  query_result_scoped_reference(value.user,
                                request.parameters.user.installation);
  query_result_target(value.user, request.parameters.user);
}
void validate_query_result(const GetGroupInfoRequest &request,
                           const GroupInfo &value) {
  RequestBudget budget(request.value_limits);
  project_query_result(budget, value, 0);
  // Only after the entire borrowed result is bounded may reference validation
  // allocate decoding scratch. This is shape/canonical validation, not
  // authority.
  query_result_scoped_reference(value.group,
                                request.parameters.group.installation);
  query_result_target(value.group, request.parameters.group);
}
void validate_query_result(const GetGroupMemberInfoRequest &request,
                           const GroupMemberInfo &value) {
  RequestBudget budget(request.value_limits);
  project_query_result(budget, value, 0);
  // Only after the entire borrowed result is bounded may reference validation
  // allocate decoding scratch. This is shape/canonical validation, not
  // authority.
  query_result_scoped_reference(value.group,
                                request.parameters.group.installation);
  query_result_scoped_reference(value.user,
                                request.parameters.group.installation);
  query_result_target(value.group, request.parameters.group);
  query_result_target(value.user, request.parameters.user);
}
void validate_login_result(const GetLoginInfoRequest &request,
                           const LoginInfo &value) {
  RequestBudget budget(request.value_limits);
  budget.value(0);
  query_result_scope(budget, value.installation, 1);
  budget.value(1);
  query_result_text(budget, value.nickname, 1);
  query_result_installation(value.installation,
                            request.parameters.installation);
  if (value.user_id <= 0)
    throw std::invalid_argument("invalid standard login identity");
}
void validate_query_result(const GetFriendListRequest &request,
                           const FriendList &value) {
  RequestBudget budget(request.value_limits);
  project_query_result(budget, value, 0);
  // Only after the entire borrowed result is bounded may reference validation
  // allocate decoding scratch. This is shape/canonical validation, not
  // authority.
  query_result_installation(value.installation,
                            request.parameters.installation);
  for (const auto &entry : value.entries)
    query_result_scoped_reference(entry.user, request.parameters.installation);
}
void validate_query_result(const GetGroupListRequest &request,
                           const GroupList &value) {
  RequestBudget budget(request.value_limits);
  project_query_result(budget, value, 0);
  // Only after the entire borrowed result is bounded may reference validation
  // allocate decoding scratch. This is shape/canonical validation, not
  // authority.
  query_result_installation(value.installation,
                            request.parameters.installation);
  for (const auto &entry : value.entries)
    query_result_scoped_reference(entry.group, request.parameters.installation);
}
void validate_query_result(const GetGroupMemberListRequest &request,
                           const GroupMemberList &value) {
  RequestBudget budget(request.value_limits);
  project_query_result(budget, value, 0);
  // Only after the entire borrowed result is bounded may reference validation
  // allocate decoding scratch. This is shape/canonical validation, not
  // authority.
  query_result_scoped_reference(value.group,
                                request.parameters.group.installation);
  query_result_target(value.group, request.parameters.group);
  for (const auto &entry : value.entries) {
    query_result_scoped_reference(entry.group,
                                  request.parameters.group.installation);
    query_result_scoped_reference(entry.user,
                                  request.parameters.group.installation);
    query_result_target(entry.group, request.parameters.group);
  }
}
void validate_query_result(const GetGroupHonorInfoRequest &request,
                           const GroupHonorInfo &value) {
  RequestBudget budget(request.value_limits);
  project_query_result(budget, value, 0);
  // Only after the entire borrowed result is bounded may reference validation
  // allocate decoding scratch. This is shape/canonical validation, not
  // authority.
  query_result_scoped_reference(value.group,
                                request.parameters.group.installation);
  query_result_target(value.group, request.parameters.group);
  query_result_target(value.requested_type, request.parameters.type);
  if (value.current_talkative)
    query_result_scoped_reference(value.current_talkative->user,
                                  request.parameters.group.installation);
  for (const auto *category :
       {&value.talkative_list, &value.performer_list, &value.legend_list,
        &value.strong_newbie_list, &value.emotion_list})
    if (*category)
      for (const auto &entry : **category)
        query_result_scoped_reference(entry.user,
                                      request.parameters.group.installation);
}
// Closed service request shapes use the same local projection/deadline rules.
template <typename Request>
concept ServiceRequest = std::same_as<Request, GetCookiesRequest> ||
                         std::same_as<Request, GetCsrfTokenRequest> ||
                         std::same_as<Request, GetCredentialsRequest> ||
                         std::same_as<Request, GetStatusRequest> ||
                         std::same_as<Request, GetVersionInfoRequest> ||
                         std::same_as<Request, CanSendImageRequest> ||
                         std::same_as<Request, CanSendRecordRequest> ||
                         std::same_as<Request, SetRestartRequest> ||
                         std::same_as<Request, CleanCacheRequest>;

template <ServiceRequest Request>
void service_projection(RequestBudget &budget, const Request &request,
                        bool json) {
  budget.value(0);
  MutationProjection projection(budget, json);
  projection.field("installation", 1);
  projection.text("installation_id", request.installation.installation_id, 2);
  projection.text("surface", request.installation.surface.value(), 2);
  projection.field("parameters", 1);
  if constexpr (std::same_as<Request, GetCookiesRequest> ||
                std::same_as<Request, GetCredentialsRequest>) {
    projection.field("domain", 2);
    if (request.parameters.domain)
      budget.text(*request.parameters.domain);
  } else if constexpr (std::same_as<Request, SetRestartRequest>) {
    projection.field("delay", 2);
  }
  projection.envelope(
      request.control.execution,
      request.control.correlation
          ? std::optional<std::string_view>{request.control.correlation
                                                ->attempt_id}
          : std::nullopt);
}

template <ServiceRequest Request>
void service_borrowed_projection(RequestBudget &budget,
                                 const bot_model::Json &payload,
                                 ExecutionMode mode,
                                 std::optional<std::string_view> correlation) {
  budget.value(0);
  MutationProjection projection(budget, false);
  const auto &installation = payload.at("installation");
  bot_model::detail::require_object(installation, "standard installation");
  projection.field("installation", 1);
  projection.text("installation_id", string(installation.at("installation_id")),
                  2);
  projection.text("surface", string(installation.at("surface")), 2);
  projection.field("parameters", 1);
  const auto &parameters = payload.at("parameters");
  if constexpr (std::same_as<Request, GetCookiesRequest> ||
                std::same_as<Request, GetCredentialsRequest>) {
    const auto &value = parameters.at("domain");
    projection.field("domain", 2);
    if (!value.is_null())
      budget.text(string(value));
  } else if constexpr (std::same_as<Request, SetRestartRequest>) {
    (void)parameters.at("delay"); // Missing is not an omission choice.
    projection.field("delay", 2);
  }
  projection.envelope(mode, correlation);
}

void validate_service_delay(const RestartDelayMilliseconds &delay) {
  std::visit(
      [](auto number) {
        if constexpr (std::same_as<decltype(number), double>)
          if (!std::isfinite(number))
            throw std::invalid_argument("invalid standard service delay");
      },
      delay.value());
}
auto service_delay(const bot_model::Json &value)
    -> std::optional<RestartDelayMilliseconds> {
  if (value.is_null())
    return std::nullopt;
  // Unsigned first: is_number_integer also includes uint64 in this JSON type.
  if (value.is_number_unsigned())
    return RestartDelayMilliseconds{value.get<std::uint64_t>()};
  if (value.is_number_integer())
    return RestartDelayMilliseconds{value.get<std::int64_t>()};
  if (value.is_number_float() && std::isfinite(value.get<double>()))
    return RestartDelayMilliseconds{value.get<double>()};
  throw std::invalid_argument("invalid standard service delay");
}
template <ServiceRequest Request>
void validate_service_fields(const Request &request) {
  request.control.validate();
  QqNativeReferenceCodec{}.validate(request.installation);
  if constexpr (std::same_as<Request, SetRestartRequest>)
    if (request.parameters.delay)
      validate_service_delay(*request.parameters.delay);
}
template <ServiceRequest Request>
auto service_parameters_json(const Request &request) -> bot_model::Json {
  if constexpr (std::same_as<Request, GetCookiesRequest> ||
                std::same_as<Request, GetCredentialsRequest>) {
    return {{"domain", request.parameters.domain
                           ? bot_model::Json(*request.parameters.domain)
                           : bot_model::Json(nullptr)}};
  } else if constexpr (std::same_as<Request, SetRestartRequest>) {
    return {{"delay",
             request.parameters.delay
                 ? std::visit([](auto value) { return bot_model::Json(value); },
                              request.parameters.delay->value())
                 : bot_model::Json(nullptr)}};
  } else {
    return bot_model::Json::object();
  }
}

// Restricted views never escape these bounded borrowed checks. Known DTO
// names and variant tags are not retained text; extra names really are.
void service_result_text(RequestBudget &budget, const SensitiveText &value) {
  budget.value(1);
  value.with_value(SensitiveUse::ExplicitAccess,
                   [&](std::string_view text) { budget.text(text); });
}
void service_result_token(RequestBudget &budget, const SensitiveInt32 &value) {
  budget.value(1);
  value.with_value(SensitiveUse::ExplicitAccess, [](std::int32_t) {});
}
void service_result_view(RequestBudget &budget, const ServiceValueView &value,
                         std::size_t depth) {
  budget.value(depth);
  switch (value.kind()) {
  case ServiceValueKind::Null:
  case ServiceValueKind::Boolean:
  case ServiceValueKind::SignedInteger:
  case ServiceValueKind::UnsignedInteger:
    return;
  case ServiceValueKind::Floating:
    if (!std::isfinite(value.floating()))
      throw std::invalid_argument("invalid standard service result number");
    return;
  case ServiceValueKind::Text:
    value.with_text([&](std::string_view text) { budget.text(text); });
    return;
  case ServiceValueKind::Array:
    value.for_each_element([&](const ServiceValueView &element) {
      service_result_view(budget, element, depth + 1);
    });
    return;
  case ServiceValueKind::Object:
    value.for_each_member(
        [&](std::string_view name, const ServiceValueView &member) {
          budget.text(name);
          service_result_view(budget, member, depth + 1);
        });
    return;
  }
  throw std::invalid_argument("invalid standard service result kind");
}
void service_result_extras(RequestBudget &budget,
                           const ServiceExtraFields &extra,
                           std::initializer_list<std::string_view> known) {
  extra.with_value(
      SensitiveUse::ExplicitAccess, [&](const ServiceValueView &root) {
        budget.value(1);
        if (root.kind() != ServiceValueKind::Object)
          throw std::invalid_argument("invalid standard service extra shape");
        root.for_each_member(
            [&](std::string_view name, const ServiceValueView &member) {
              budget.text(name);
              for (const auto field : known)
                if (name == field)
                  throw std::invalid_argument(
                      "invalid standard service extra field");
              service_result_view(budget, member, 2);
            });
      });
}
void project_service_result(RequestBudget &budget, const CookiesData &value) {
  budget.value(0);
  service_result_text(budget, value.cookies);
}
void project_service_result(RequestBudget &budget, const CsrfTokenData &value) {
  budget.value(0);
  service_result_token(budget, value.token);
}
void project_service_result(RequestBudget &budget,
                            const CredentialsData &value) {
  budget.value(0);
  service_result_text(budget, value.cookies);
  service_result_token(budget, value.csrf_token);
}
void project_service_result(RequestBudget &budget, const StatusData &value) {
  budget.value(0);
  budget.value(1); // online null is a real scalar, not an absent member.
  budget.value(1);
  service_result_extras(budget, value.extra, {"online", "good"});
}
void project_service_result(RequestBudget &budget,
                            const VersionInfoData &value) {
  budget.value(0);
  query_result_text(budget, value.app_name, 1);
  query_result_text(budget, value.app_version, 1);
  query_result_text(budget, value.protocol_version, 1);
  service_result_extras(budget, value.extra,
                        {"app_name", "app_version", "protocol_version"});
}
void project_service_result(RequestBudget &budget, const CanSendImageData &) {
  budget.value(0);
  budget.value(1);
}
void project_service_result(RequestBudget &budget, const CanSendRecordData &) {
  budget.value(0);
  budget.value(1);
}
void project_service_result(RequestBudget &budget, const ServiceNoData &) {
  budget.value(0);
}
auto lookup_message_id(const bot_model::Json &value) -> std::int32_t {
  const auto id = integer(value);
  if (!std::in_range<std::int32_t>(id))
    throw std::invalid_argument("invalid standard lookup message ID");
  return static_cast<std::int32_t>(id);
}
void lookup_project_scope(MutationProjection &projection,
                          const bot_model::BotInstallationRef &scope) {
  projection.field("installation", 2);
  projection.text("installation_id", scope.installation_id, 3);
  projection.text("surface", scope.surface.value(), 3);
}
void lookup_project_scope(MutationProjection &projection,
                          const bot_model::Json &scope) {
  projection.field("installation", 2);
  projection.text("installation_id", string(scope.at("installation_id")), 3);
  projection.text("surface", string(scope.at("surface")), 3);
}
void lookup_project_parameters(MutationProjection &projection,
                               const GetMessageParameters &p,
                               const ValueLimits &limits) {
  lookup_project_scope(projection, p.message.installation());
  (void)QuerySelectors::export_message(p.message, limits);
  projection.field("message_id", 2);
}
void lookup_project_parameters(MutationProjection &projection,
                               const GetForwardMessageParameters &p,
                               const ValueLimits &limits) {
  lookup_project_scope(projection, p.forward.installation());
  // Selector export has its own pre-copy checks; it confers no authority.
  const auto id = QuerySelectors::export_forward(p.forward, limits);
  projection.text("id", id, 2);
}
void lookup_project_parameters(MutationProjection &projection,
                               std::type_identity<GetMessageParameters>,
                               const bot_model::Json &p) {
  lookup_project_scope(projection, p.at("installation"));
  (void)lookup_message_id(p.at("message_id"));
  projection.field("message_id", 2);
}
void lookup_project_parameters(MutationProjection &projection,
                               std::type_identity<GetForwardMessageParameters>,
                               const bot_model::Json &p) {
  lookup_project_scope(projection, p.at("installation"));
  projection.text("id", string(p.at("id")), 2);
}
template <typename Request>
void lookup_projection(RequestBudget &budget, const Request &request,
                       bool json) {
  budget.value(0);
  MutationProjection projection(budget, json);
  projection.field("parameters", 1);
  lookup_project_parameters(projection, request.parameters,
                            request.value_limits);
  projection.envelope(
      request.control.execution,
      request.control.correlation
          ? std::optional<std::string_view>{request.control.correlation
                                                ->attempt_id}
          : std::nullopt);
}
template <typename Request>
void lookup_validate_fields(const Request &request) {
  request.control.validate();
  QqNativeReferenceCodec{}.validate(
      bot_model::OperationTraits<Request>::installation(request));
}

auto media_format_name(RecordFormat format) -> std::string_view {
  switch (format) {
  case RecordFormat::Mp3:
    return "mp3";
  case RecordFormat::Amr:
    return "amr";
  case RecordFormat::Wma:
    return "wma";
  case RecordFormat::M4a:
    return "m4a";
  case RecordFormat::Spx:
    return "spx";
  case RecordFormat::Ogg:
    return "ogg";
  case RecordFormat::Wav:
    return "wav";
  case RecordFormat::Flac:
    return "flac";
  }
  throw std::invalid_argument("invalid standard media format");
}
auto media_format(const bot_model::Json &value) -> RecordFormat {
  const auto text = string(value);
  for (const auto format :
       {RecordFormat::Mp3, RecordFormat::Amr, RecordFormat::Wma,
        RecordFormat::M4a, RecordFormat::Spx, RecordFormat::Ogg,
        RecordFormat::Wav, RecordFormat::Flac})
    if (text == media_format_name(format))
      return format;
  throw std::invalid_argument("invalid standard media format");
}
template <typename Request>
requires(std::same_as<Request, GetImageRequest> ||
         std::same_as<Request, GetRecordRequest>)
auto media_selector(const Request &request) -> const auto & {
  if constexpr (std::same_as<Request, GetImageRequest>)
    return request.parameters.image;
  else
    return request.parameters.record;
}
void media_project_fields(RequestBudget &budget, std::string_view installation,
                          std::string_view surface, std::string_view filename,
                          std::optional<RecordFormat> format,
                          ExecutionMode mode,
                          std::optional<std::string_view> correlation,
                          bool json) {
  budget.value(0);
  MutationProjection projection(budget, json);
  // The SDK envelope flattens the selector; the DTO retains its nesting.
  if (json) {
    projection.field("installation", 1);
    projection.text("installation_id", installation, 2);
    projection.text("surface", surface, 2);
    projection.field("parameters", 1);
    projection.text("file", filename, 2);
  } else {
    budget.value(1); // parameters
    budget.value(2); // move-only selector
    budget.value(3); // selector installation
    budget.value(4);
    budget.text(installation);
    budget.value(4);
    budget.text(surface);
    budget.value(3);
    budget.text(filename);
  }
  if (format) {
    projection.field("out_format", 2);
    const auto name = media_format_name(*format);
    if (json)
      budget.text(name);
  }
  projection.envelope(mode, correlation);
}
template <typename Request>
void media_projection(RequestBudget &budget, const Request &request,
                      bool json) {
  const auto &selector = media_selector(request);
  selector.with_filename(
      SensitiveUse::ExplicitAccess, [&](std::string_view filename) {
        media_project_fields(
            budget, selector.installation().installation_id,
            selector.installation().surface.value(), filename,
            detail::media_result_context(request).requested_format,
            request.control.execution,
            request.control.correlation
                ? std::optional<std::string_view>{request.control.correlation
                                                      ->attempt_id}
                : std::nullopt,
            json);
      });
}
template <typename Request>
void media_borrowed_projection(RequestBudget &budget,
                               const bot_model::Json &payload,
                               ExecutionMode mode,
                               std::optional<std::string_view> correlation) {
  const auto &scope = payload.at("installation");
  bot_model::detail::require_object(scope, "standard media installation");
  const auto &parameters = payload.at("parameters");
  std::optional<RecordFormat> format;
  if constexpr (std::same_as<Request, GetRecordRequest>)
    format = media_format(parameters.at("out_format"));
  media_project_fields(
      budget, string(scope.at("installation_id")), string(scope.at("surface")),
      string(parameters.at("file")), format, mode, correlation, false);
}
template <typename Request> void media_validate_fields(const Request &request) {
  request.control.validate();
  QqNativeReferenceCodec{}.validate(media_selector(request).installation());
  if constexpr (std::same_as<Request, GetRecordRequest>)
    (void)media_format_name(request.parameters.out_format);
}
template <typename Request>
auto media_parameters_json(const Request &request) -> bot_model::Json {
  auto parameters = bot_model::Json::object();
  media_selector(request).with_filename(
      SensitiveUse::ExplicitAccess,
      [&](std::string_view filename) { parameters.emplace("file", filename); });
  if constexpr (std::same_as<Request, GetRecordRequest>)
    parameters.emplace("out_format",
                       media_format_name(request.parameters.out_format));
  return parameters;
}
template <typename Result>
void media_validate_outcome(const detail::MediaResultContext &context,
                            const ExecutionOutcome<Result> &outcome) {
  validate_outcome_mode(context.mode, outcome);
  context.values.validate();
  if constexpr (std::same_as<Result, ConvertedRecordData>) {
    if (!context.requested_format)
      throw std::invalid_argument("standard media format context missing");
    (void)media_format_name(*context.requested_format);
  } else if (context.requested_format) {
    throw std::invalid_argument("invalid standard media format context");
  }
  if (const auto *completed = std::get_if<Completed<Result>>(&outcome.state)) {
    RequestBudget budget(context.values);
    budget.value(0);
    if constexpr (std::same_as<Result, ConvertedRecordData>) {
      budget.value(1);
      if (completed->value.requested_format != *context.requested_format)
        throw std::invalid_argument("standard media requested format mismatch");
    }
    budget.value(1); // ProviderLocalFile, not host-local authority.
    budget.value(2); // Borrowed SensitiveText: no serialization or secret copy.
    completed->value.file.path.with_value(
        SensitiveUse::ExplicitAccess,
        [&](std::string_view path) { budget.text(path); });
  }
}

} // namespace

SendLikeRequest::SendLikeRequest(SendLikeParameters parameters,
                                 CallControl control, ValueLimits value_limits)
    : parameters(std::move(parameters)), control(std::move(control)),
      value_limits(value_limits) {}

void SendLikeRequest::validate() const {
  RequestBudget budget(value_limits);
  projection(budget, request_text(*this), control.execution, false);
  // All borrowed values are bounded before native-reference decode scratch.
  validate_fields(*this);
}
DeleteMsgRequest::DeleteMsgRequest(DeleteMsgParameters parameters,
                                   CallControl control,
                                   ValueLimits value_limits)
    : parameters(std::move(parameters)), control(std::move(control)),
      value_limits(value_limits) {}
void DeleteMsgRequest::validate() const {
  RequestBudget budget(value_limits);
  mutation_projection(budget, *this, false);
  validate_mutation_fields(*this);
}
SetGroupKickRequest::SetGroupKickRequest(SetGroupKickParameters parameters,
                                         CallControl control,
                                         ValueLimits value_limits)
    : parameters(std::move(parameters)), control(std::move(control)),
      value_limits(value_limits) {}
void SetGroupKickRequest::validate() const {
  RequestBudget budget(value_limits);
  mutation_projection(budget, *this, false);
  validate_mutation_fields(*this);
}
SetGroupBanRequest::SetGroupBanRequest(SetGroupBanParameters parameters,
                                       CallControl control,
                                       ValueLimits value_limits)
    : parameters(std::move(parameters)), control(std::move(control)),
      value_limits(value_limits) {}
void SetGroupBanRequest::validate() const {
  RequestBudget budget(value_limits);
  mutation_projection(budget, *this, false);
  validate_mutation_fields(*this);
}
SetGroupWholeBanRequest::SetGroupWholeBanRequest(
    SetGroupWholeBanParameters parameters, CallControl control,
    ValueLimits value_limits)
    : parameters(std::move(parameters)), control(std::move(control)),
      value_limits(value_limits) {}
void SetGroupWholeBanRequest::validate() const {
  RequestBudget budget(value_limits);
  mutation_projection(budget, *this, false);
  validate_mutation_fields(*this);
}
SetGroupAdminRequest::SetGroupAdminRequest(SetGroupAdminParameters parameters,
                                           CallControl control,
                                           ValueLimits value_limits)
    : parameters(std::move(parameters)), control(std::move(control)),
      value_limits(value_limits) {}
void SetGroupAdminRequest::validate() const {
  RequestBudget budget(value_limits);
  mutation_projection(budget, *this, false);
  validate_mutation_fields(*this);
}
SetGroupAnonymousRequest::SetGroupAnonymousRequest(
    SetGroupAnonymousParameters parameters, CallControl control,
    ValueLimits value_limits)
    : parameters(std::move(parameters)), control(std::move(control)),
      value_limits(value_limits) {}
void SetGroupAnonymousRequest::validate() const {
  RequestBudget budget(value_limits);
  mutation_projection(budget, *this, false);
  validate_mutation_fields(*this);
}
SetGroupCardRequest::SetGroupCardRequest(SetGroupCardParameters parameters,
                                         CallControl control,
                                         ValueLimits value_limits)
    : parameters(std::move(parameters)), control(std::move(control)),
      value_limits(value_limits) {}
void SetGroupCardRequest::validate() const {
  RequestBudget budget(value_limits);
  mutation_projection(budget, *this, false);
  validate_mutation_fields(*this);
}
SetGroupNameRequest::SetGroupNameRequest(SetGroupNameParameters parameters,
                                         CallControl control,
                                         ValueLimits value_limits)
    : parameters(std::move(parameters)), control(std::move(control)),
      value_limits(value_limits) {}
void SetGroupNameRequest::validate() const {
  RequestBudget budget(value_limits);
  mutation_projection(budget, *this, false);
  validate_mutation_fields(*this);
}
SetGroupLeaveRequest::SetGroupLeaveRequest(SetGroupLeaveParameters parameters,
                                           CallControl control,
                                           ValueLimits value_limits)
    : parameters(std::move(parameters)), control(std::move(control)),
      value_limits(value_limits) {}
void SetGroupLeaveRequest::validate() const {
  RequestBudget budget(value_limits);
  mutation_projection(budget, *this, false);
  validate_mutation_fields(*this);
}
SetGroupSpecialTitleRequest::SetGroupSpecialTitleRequest(
    SetGroupSpecialTitleParameters parameters, CallControl control,
    ValueLimits value_limits)
    : parameters(std::move(parameters)), control(std::move(control)),
      value_limits(value_limits) {}
void SetGroupSpecialTitleRequest::validate() const {
  RequestBudget budget(value_limits);
  mutation_projection(budget, *this, false);
  validate_mutation_fields(*this);
}
GetStrangerInfoRequest::GetStrangerInfoRequest(
    GetStrangerInfoParameters parameters, CallControl control,
    ValueLimits value_limits)
    : parameters(std::move(parameters)), control(std::move(control)),
      value_limits(value_limits) {}
void GetStrangerInfoRequest::validate() const {
  RequestBudget budget(value_limits);
  query_projection(budget, *this, false);
  validate_query_fields(*this);
}
GetLoginInfoRequest::GetLoginInfoRequest(GetLoginInfoParameters parameters,
                                         CallControl control,
                                         ValueLimits value_limits)
    : parameters(std::move(parameters)), control(std::move(control)),
      value_limits(value_limits) {}
void GetLoginInfoRequest::validate() const {
  RequestBudget budget(value_limits);
  query_projection(budget, *this, false);
  validate_query_fields(*this);
}
GetFriendListRequest::GetFriendListRequest(GetFriendListParameters parameters,
                                           CallControl control,
                                           ValueLimits value_limits)
    : parameters(std::move(parameters)), control(std::move(control)),
      value_limits(value_limits) {}
void GetFriendListRequest::validate() const {
  RequestBudget budget(value_limits);
  query_projection(budget, *this, false);
  validate_query_fields(*this);
}
GetGroupInfoRequest::GetGroupInfoRequest(GetGroupInfoParameters parameters,
                                         CallControl control,
                                         ValueLimits value_limits)
    : parameters(std::move(parameters)), control(std::move(control)),
      value_limits(value_limits) {}
void GetGroupInfoRequest::validate() const {
  RequestBudget budget(value_limits);
  query_projection(budget, *this, false);
  validate_query_fields(*this);
}
GetGroupListRequest::GetGroupListRequest(GetGroupListParameters parameters,
                                         CallControl control,
                                         ValueLimits value_limits)
    : parameters(std::move(parameters)), control(std::move(control)),
      value_limits(value_limits) {}
void GetGroupListRequest::validate() const {
  RequestBudget budget(value_limits);
  query_projection(budget, *this, false);
  validate_query_fields(*this);
}
GetGroupMemberInfoRequest::GetGroupMemberInfoRequest(
    GetGroupMemberInfoParameters parameters, CallControl control,
    ValueLimits value_limits)
    : parameters(std::move(parameters)), control(std::move(control)),
      value_limits(value_limits) {}
void GetGroupMemberInfoRequest::validate() const {
  RequestBudget budget(value_limits);
  query_projection(budget, *this, false);
  validate_query_fields(*this);
}
GetGroupMemberListRequest::GetGroupMemberListRequest(
    GetGroupMemberListParameters parameters, CallControl control,
    ValueLimits value_limits)
    : parameters(std::move(parameters)), control(std::move(control)),
      value_limits(value_limits) {}
void GetGroupMemberListRequest::validate() const {
  RequestBudget budget(value_limits);
  query_projection(budget, *this, false);
  validate_query_fields(*this);
}
GetGroupHonorInfoRequest::GetGroupHonorInfoRequest(
    GetGroupHonorInfoParameters parameters, CallControl control,
    ValueLimits value_limits)
    : parameters(std::move(parameters)), control(std::move(control)),
      value_limits(value_limits) {}
void GetGroupHonorInfoRequest::validate() const {
  RequestBudget budget(value_limits);
  query_projection(budget, *this, false);
  validate_query_fields(*this);
}
GetCookiesRequest::GetCookiesRequest(bot_model::BotInstallationRef installation,
                                     GetCookiesParameters parameters,
                                     CallControl control,
                                     ValueLimits value_limits)
    : installation(std::move(installation)), parameters(std::move(parameters)),
      control(std::move(control)), value_limits(value_limits) {}
void GetCookiesRequest::validate() const {
  RequestBudget budget(value_limits);
  service_projection(budget, *this, false);
  validate_service_fields(*this);
}
GetCsrfTokenRequest::GetCsrfTokenRequest(
    bot_model::BotInstallationRef installation,
    GetCsrfTokenParameters parameters, CallControl control,
    ValueLimits value_limits)
    : installation(std::move(installation)), parameters(std::move(parameters)),
      control(std::move(control)), value_limits(value_limits) {}
void GetCsrfTokenRequest::validate() const {
  RequestBudget budget(value_limits);
  service_projection(budget, *this, false);
  validate_service_fields(*this);
}
GetCredentialsRequest::GetCredentialsRequest(
    bot_model::BotInstallationRef installation,
    GetCredentialsParameters parameters, CallControl control,
    ValueLimits value_limits)
    : installation(std::move(installation)), parameters(std::move(parameters)),
      control(std::move(control)), value_limits(value_limits) {}
void GetCredentialsRequest::validate() const {
  RequestBudget budget(value_limits);
  service_projection(budget, *this, false);
  validate_service_fields(*this);
}
GetStatusRequest::GetStatusRequest(bot_model::BotInstallationRef installation,
                                   GetStatusParameters parameters,
                                   CallControl control,
                                   ValueLimits value_limits)
    : installation(std::move(installation)), parameters(std::move(parameters)),
      control(std::move(control)), value_limits(value_limits) {}
void GetStatusRequest::validate() const {
  RequestBudget budget(value_limits);
  service_projection(budget, *this, false);
  validate_service_fields(*this);
}
GetVersionInfoRequest::GetVersionInfoRequest(
    bot_model::BotInstallationRef installation,
    GetVersionInfoParameters parameters, CallControl control,
    ValueLimits value_limits)
    : installation(std::move(installation)), parameters(std::move(parameters)),
      control(std::move(control)), value_limits(value_limits) {}
void GetVersionInfoRequest::validate() const {
  RequestBudget budget(value_limits);
  service_projection(budget, *this, false);
  validate_service_fields(*this);
}
CanSendImageRequest::CanSendImageRequest(
    bot_model::BotInstallationRef installation,
    CanSendImageParameters parameters, CallControl control,
    ValueLimits value_limits)
    : installation(std::move(installation)), parameters(std::move(parameters)),
      control(std::move(control)), value_limits(value_limits) {}
void CanSendImageRequest::validate() const {
  RequestBudget budget(value_limits);
  service_projection(budget, *this, false);
  validate_service_fields(*this);
}
CanSendRecordRequest::CanSendRecordRequest(
    bot_model::BotInstallationRef installation,
    CanSendRecordParameters parameters, CallControl control,
    ValueLimits value_limits)
    : installation(std::move(installation)), parameters(std::move(parameters)),
      control(std::move(control)), value_limits(value_limits) {}
void CanSendRecordRequest::validate() const {
  RequestBudget budget(value_limits);
  service_projection(budget, *this, false);
  validate_service_fields(*this);
}
SetRestartRequest::SetRestartRequest(bot_model::BotInstallationRef installation,
                                     SetRestartParameters parameters,
                                     CallControl control,
                                     ValueLimits value_limits)
    : installation(std::move(installation)), parameters(std::move(parameters)),
      control(std::move(control)), value_limits(value_limits) {}
void SetRestartRequest::validate() const {
  RequestBudget budget(value_limits);
  service_projection(budget, *this, false);
  validate_service_fields(*this);
}
CleanCacheRequest::CleanCacheRequest(bot_model::BotInstallationRef installation,
                                     CleanCacheParameters parameters,
                                     CallControl control,
                                     ValueLimits value_limits)
    : installation(std::move(installation)), parameters(std::move(parameters)),
      control(std::move(control)), value_limits(value_limits) {}
void CleanCacheRequest::validate() const {
  RequestBudget budget(value_limits);
  service_projection(budget, *this, false);
  validate_service_fields(*this);
}
GetMsgRequest::GetMsgRequest(GetMessageParameters parameters,
                             CallControl control, ValueLimits value_limits)
    : parameters(std::move(parameters)), control(std::move(control)),
      value_limits(value_limits) {}
void GetMsgRequest::validate() const {
  RequestBudget budget(value_limits);
  lookup_projection(budget, *this, false);
  lookup_validate_fields(*this);
}
GetForwardMsgRequest::GetForwardMsgRequest(
    GetForwardMessageParameters parameters, CallControl control,
    ValueLimits value_limits)
    : parameters(std::move(parameters)), control(std::move(control)),
      value_limits(value_limits) {}
void GetForwardMsgRequest::validate() const {
  RequestBudget budget(value_limits);
  lookup_projection(budget, *this, false);
  lookup_validate_fields(*this);
}
GetImageRequest::GetImageRequest(GetImageParameters parameters,
                                 CallControl control, ValueLimits value_limits)
    : parameters(std::move(parameters)), control(std::move(control)),
      value_limits(value_limits) {}
void GetImageRequest::validate() const {
  RequestBudget budget(value_limits);
  media_projection(budget, *this, false);
  media_validate_fields(*this);
}
GetRecordRequest::GetRecordRequest(GetRecordParameters parameters,
                                   CallControl control,
                                   ValueLimits value_limits)
    : parameters(std::move(parameters)), control(std::move(control)),
      value_limits(value_limits) {}
void GetRecordRequest::validate() const {
  RequestBudget budget(value_limits);
  media_projection(budget, *this, false);
  media_validate_fields(*this);
}
namespace detail {
auto media_result_context(const GetImageRequest &request)
    -> MediaResultContext {
  return {request.control.execution, request.value_limits, std::nullopt};
}
auto media_result_context(const GetRecordRequest &request)
    -> MediaResultContext {
  return {request.control.execution, request.value_limits,
          request.parameters.out_format};
}
void validate_media_result(const MediaResultContext &context,
                           const GetImageOutcome &outcome) {
  media_validate_outcome(context, outcome);
}
void validate_media_result(const MediaResultContext &context,
                           const GetRecordOutcome &outcome) {
  media_validate_outcome(context, outcome);
}
} // namespace detail

} // namespace obcx::onebot11::bot::standard

namespace obcx::bot {
namespace standard = onebot11::bot::standard;
using SendLikeTraits = OperationTraits<standard::SendLikeRequest>;

auto SendLikeTraits::action() -> const ActionId & {
  static const ActionId id{"onebot11.standard.send_like"};
  return id;
}
auto SendLikeTraits::installation(const request_type &request)
    -> const BotInstallationRef & {
  return request.parameters.user.installation;
}
auto SendLikeTraits::supports_surface(const SurfaceId &surface) -> bool {
  return surface == onebot11::bot::surface;
}
void SendLikeTraits::validate_result(const request_type &request,
                                     const result_type &result) {
  standard::validate_outcome_mode(request.control.execution, result);
}

auto GatewayCodec<standard::SendLikeRequest>::encode(
    const standard::SendLikeRequest &request) -> Json {
  standard::RequestBudget budget(request.value_limits);
  const auto text = standard::request_text(request);
  standard::projection(budget, text, request.control.execution, false);
  standard::validate_fields(request);
  const auto deadline =
      request.deadline_
          ? *request.deadline_
          : standard::deadline_after(request.control.limits.timeout);
  if (request.deadline_)
    standard::validate_preserved_deadline(deadline,
                                          request.control.limits.timeout);
  const auto ticks = deadline.time_since_epoch().count();
  if (!std::in_range<std::int64_t>(ticks))
    throw std::invalid_argument(
        "standard deadline exceeds wire representation");
  standard::projection(budget, text, request.control.execution, true);
  const auto &limits = request.control.limits;
  const auto &values = request.value_limits;
  // Native parameter bytes do not bound this internal metadata envelope.
  return {{"parameters",
           {{"user", request.parameters.user},
            {"times", request.parameters.times}}},
          {"control",
           {{"execution", standard::execution_name(request.control.execution)},
            {"limits",
             {{"maximum_parameter_bytes", limits.maximum_parameter_bytes},
              {"maximum_response_bytes", limits.maximum_response_bytes},
              {"timeout_milliseconds", limits.timeout.count()}}},
            {"correlation",
             request.control.correlation
                 ? Json{{"attempt_id", request.control.correlation->attempt_id}}
                 : Json(nullptr)}}},
          {"value_limits",
           {{"maximum_bytes", values.maximum_bytes},
            {"maximum_nodes", values.maximum_nodes},
            {"maximum_depth", values.maximum_depth},
            {"maximum_text_bytes", values.maximum_text_bytes}}},
          {"deadline_ticks", static_cast<std::int64_t>(ticks)}};
}

auto GatewayCodec<standard::SendLikeRequest>::decode(const Json &payload)
    -> standard::SendLikeRequest {
  detail::require_object(payload, "standard send_like request");
  const auto values = standard::value_limits(payload.at("value_limits"));
  standard::RequestBudget budget(values);
  budget.inspect(payload,
                 0); // Includes unknown/discarded fields, before copies.
  const auto &parameters = payload.at("parameters");
  const auto &control = payload.at("control");
  detail::require_object(parameters, "standard parameters");
  detail::require_object(control, "standard control");
  const auto mode = standard::execution_mode(control.at("execution"));
  const auto &limits = control.at("limits");
  detail::require_object(limits, "standard call limits");
  const auto timeout = standard::integer(limits.at("timeout_milliseconds"));
  if (!std::in_range<std::chrono::milliseconds::rep>(timeout))
    throw std::invalid_argument("standard timeout exceeds representation");
  standard::CallLimits calls{
      standard::size_value(limits.at("maximum_parameter_bytes")),
      standard::size_value(limits.at("maximum_response_bytes")),
      std::chrono::milliseconds{timeout}};
  calls.validate();
  const auto deadline = standard::deadline_from_ticks(
      standard::integer(payload.at("deadline_ticks")));
  standard::validate_preserved_deadline(deadline, calls.timeout);
  const auto times = standard::integer(parameters.at("times"));
  const auto &user = parameters.at("user");
  const auto &installation = user.at("installation");
  const auto &correlation = control.at("correlation");
  standard::RequestText text{
      standard::string(installation.at("installation_id")),
      standard::string(installation.at("surface")),
      standard::string(user.at("id")), std::nullopt};
  if (!correlation.is_null()) {
    detail::require_object(correlation, "standard correlation");
    text.correlation = standard::string(correlation.at("attempt_id"));
  }
  standard::projection(budget, text, mode, false);
  // Both complete borrowed input and retained projection are charged before
  // UserRef's existing SDK codec or any retained string construction runs.
  standard::SendLikeRequest request{
      {user.get<UserRef>(), times},
      {mode, calls,
       correlation.is_null() ? std::nullopt
                             : std::optional{OperationCorrelation{
                                   std::string{*text.correlation}}}},
      values};
  request.deadline_ = deadline;
  standard::validate_fields(request);
  return request;
}
using DeleteMsgTraits = OperationTraits<standard::DeleteMsgRequest>;

auto DeleteMsgTraits::action() -> const ActionId & {
  static const ActionId id{"onebot11.standard.delete_msg"};
  return id;
}
auto DeleteMsgTraits::installation(const request_type &request)
    -> const BotInstallationRef & {
  return request.parameters.message.conversation.installation;
}
auto DeleteMsgTraits::supports_surface(const SurfaceId &surface) -> bool {
  return surface == onebot11::bot::surface;
}
void DeleteMsgTraits::validate_result(const request_type &request,
                                      const result_type &result) {
  standard::validate_outcome_mode(request.control.execution, result);
}

auto GatewayCodec<standard::DeleteMsgRequest>::encode(
    const standard::DeleteMsgRequest &request) -> Json {
  standard::RequestBudget budget(request.value_limits);
  standard::mutation_projection(budget, request, false);
  standard::validate_mutation_fields(request);
  const auto deadline =
      request.deadline_
          ? *request.deadline_
          : standard::deadline_after(request.control.limits.timeout);
  if (request.deadline_)
    standard::validate_preserved_deadline(deadline,
                                          request.control.limits.timeout);
  const auto ticks = deadline.time_since_epoch().count();
  if (!std::in_range<std::int64_t>(ticks))
    throw std::invalid_argument(
        "standard deadline exceeds wire representation");
  standard::mutation_projection(budget, request, true);
  const auto &limits = request.control.limits;
  const auto &values = request.value_limits;
  // Native parameter bytes do not bound this internal metadata envelope.
  return {{"parameters", {{"message", request.parameters.message}}},
          {"control",
           {{"execution", standard::execution_name(request.control.execution)},
            {"limits",
             {{"maximum_parameter_bytes", limits.maximum_parameter_bytes},
              {"maximum_response_bytes", limits.maximum_response_bytes},
              {"timeout_milliseconds", limits.timeout.count()}}},
            {"correlation",
             request.control.correlation
                 ? Json{{"attempt_id", request.control.correlation->attempt_id}}
                 : Json(nullptr)}}},
          {"value_limits",
           {{"maximum_bytes", values.maximum_bytes},
            {"maximum_nodes", values.maximum_nodes},
            {"maximum_depth", values.maximum_depth},
            {"maximum_text_bytes", values.maximum_text_bytes}}},
          {"deadline_ticks", static_cast<std::int64_t>(ticks)}};
}

auto GatewayCodec<standard::DeleteMsgRequest>::decode(const Json &payload)
    -> standard::DeleteMsgRequest {
  detail::require_object(payload, "standard delete_msg request");
  const auto values = standard::value_limits(payload.at("value_limits"));
  standard::RequestBudget budget(values);
  budget.inspect(payload,
                 0); // Includes unknown/discarded fields, before copies.
  const auto &parameters = payload.at("parameters");
  const auto &control = payload.at("control");
  detail::require_object(parameters, "standard parameters");
  detail::require_object(control, "standard control");
  const auto mode = standard::execution_mode(control.at("execution"));
  const auto &limits = control.at("limits");
  detail::require_object(limits, "standard call limits");
  const auto timeout = standard::integer(limits.at("timeout_milliseconds"));
  if (!std::in_range<std::chrono::milliseconds::rep>(timeout))
    throw std::invalid_argument("standard timeout exceeds representation");
  standard::CallLimits calls{
      standard::size_value(limits.at("maximum_parameter_bytes")),
      standard::size_value(limits.at("maximum_response_bytes")),
      std::chrono::milliseconds{timeout}};
  calls.validate();
  const auto deadline = standard::deadline_from_ticks(
      standard::integer(payload.at("deadline_ticks")));
  standard::validate_preserved_deadline(deadline, calls.timeout);
  const auto &correlation = control.at("correlation");
  std::optional<std::string_view> correlation_text;
  if (!correlation.is_null()) {
    detail::require_object(correlation, "standard correlation");
    correlation_text = standard::string(correlation.at("attempt_id"));
  }
  budget.value(0);
  standard::MutationProjection projection(budget, false);
  projection.field("parameters", 1);
  standard::project_parameters(
      projection, std::type_identity<standard::DeleteMsgParameters>{},
      parameters);
  projection.envelope(mode, correlation_text);
  // The whole borrowed envelope and prospective retained DTO are bounded
  // before reference decoding or any owning text construction.
  standard::DeleteMsgRequest request{
      {parameters.at("message").get<MessageRef>()},
      {mode, calls,
       correlation_text
           ? std::optional{OperationCorrelation{std::string{*correlation_text}}}
           : std::nullopt},
      values};
  request.deadline_ = deadline;
  standard::validate_mutation_fields(request);
  return request;
}
using SetGroupKickTraits = OperationTraits<standard::SetGroupKickRequest>;

auto SetGroupKickTraits::action() -> const ActionId & {
  static const ActionId id{"onebot11.standard.set_group_kick"};
  return id;
}
auto SetGroupKickTraits::installation(const request_type &request)
    -> const BotInstallationRef & {
  return request.parameters.group.installation;
}
auto SetGroupKickTraits::supports_surface(const SurfaceId &surface) -> bool {
  return surface == onebot11::bot::surface;
}
void SetGroupKickTraits::validate_result(const request_type &request,
                                         const result_type &result) {
  standard::validate_outcome_mode(request.control.execution, result);
}

auto GatewayCodec<standard::SetGroupKickRequest>::encode(
    const standard::SetGroupKickRequest &request) -> Json {
  standard::RequestBudget budget(request.value_limits);
  standard::mutation_projection(budget, request, false);
  standard::validate_mutation_fields(request);
  const auto deadline =
      request.deadline_
          ? *request.deadline_
          : standard::deadline_after(request.control.limits.timeout);
  if (request.deadline_)
    standard::validate_preserved_deadline(deadline,
                                          request.control.limits.timeout);
  const auto ticks = deadline.time_since_epoch().count();
  if (!std::in_range<std::int64_t>(ticks))
    throw std::invalid_argument(
        "standard deadline exceeds wire representation");
  standard::mutation_projection(budget, request, true);
  const auto &limits = request.control.limits;
  const auto &values = request.value_limits;
  // Native parameter bytes do not bound this internal metadata envelope.
  return {{"parameters",
           {{"group", request.parameters.group},
            {"user", request.parameters.user},
            {"reject_add_request", request.parameters.reject_add_request}}},
          {"control",
           {{"execution", standard::execution_name(request.control.execution)},
            {"limits",
             {{"maximum_parameter_bytes", limits.maximum_parameter_bytes},
              {"maximum_response_bytes", limits.maximum_response_bytes},
              {"timeout_milliseconds", limits.timeout.count()}}},
            {"correlation",
             request.control.correlation
                 ? Json{{"attempt_id", request.control.correlation->attempt_id}}
                 : Json(nullptr)}}},
          {"value_limits",
           {{"maximum_bytes", values.maximum_bytes},
            {"maximum_nodes", values.maximum_nodes},
            {"maximum_depth", values.maximum_depth},
            {"maximum_text_bytes", values.maximum_text_bytes}}},
          {"deadline_ticks", static_cast<std::int64_t>(ticks)}};
}

auto GatewayCodec<standard::SetGroupKickRequest>::decode(const Json &payload)
    -> standard::SetGroupKickRequest {
  detail::require_object(payload, "standard set_group_kick request");
  const auto values = standard::value_limits(payload.at("value_limits"));
  standard::RequestBudget budget(values);
  budget.inspect(payload,
                 0); // Includes unknown/discarded fields, before copies.
  const auto &parameters = payload.at("parameters");
  const auto &control = payload.at("control");
  detail::require_object(parameters, "standard parameters");
  detail::require_object(control, "standard control");
  const auto mode = standard::execution_mode(control.at("execution"));
  const auto &limits = control.at("limits");
  detail::require_object(limits, "standard call limits");
  const auto timeout = standard::integer(limits.at("timeout_milliseconds"));
  if (!std::in_range<std::chrono::milliseconds::rep>(timeout))
    throw std::invalid_argument("standard timeout exceeds representation");
  standard::CallLimits calls{
      standard::size_value(limits.at("maximum_parameter_bytes")),
      standard::size_value(limits.at("maximum_response_bytes")),
      std::chrono::milliseconds{timeout}};
  calls.validate();
  const auto deadline = standard::deadline_from_ticks(
      standard::integer(payload.at("deadline_ticks")));
  standard::validate_preserved_deadline(deadline, calls.timeout);
  const auto &correlation = control.at("correlation");
  std::optional<std::string_view> correlation_text;
  if (!correlation.is_null()) {
    detail::require_object(correlation, "standard correlation");
    correlation_text = standard::string(correlation.at("attempt_id"));
  }
  budget.value(0);
  standard::MutationProjection projection(budget, false);
  projection.field("parameters", 1);
  standard::project_parameters(
      projection, std::type_identity<standard::SetGroupKickParameters>{},
      parameters);
  projection.envelope(mode, correlation_text);
  // The whole borrowed envelope and prospective retained DTO are bounded
  // before reference decoding or any owning text construction.
  standard::SetGroupKickRequest request{
      {parameters.at("group").get<ConversationRef>(),
       parameters.at("user").get<UserRef>(),
       standard::boolean(parameters.at("reject_add_request"))},
      {mode, calls,
       correlation_text
           ? std::optional{OperationCorrelation{std::string{*correlation_text}}}
           : std::nullopt},
      values};
  request.deadline_ = deadline;
  standard::validate_mutation_fields(request);
  return request;
}
using SetGroupBanTraits = OperationTraits<standard::SetGroupBanRequest>;

auto SetGroupBanTraits::action() -> const ActionId & {
  static const ActionId id{"onebot11.standard.set_group_ban"};
  return id;
}
auto SetGroupBanTraits::installation(const request_type &request)
    -> const BotInstallationRef & {
  return request.parameters.group.installation;
}
auto SetGroupBanTraits::supports_surface(const SurfaceId &surface) -> bool {
  return surface == onebot11::bot::surface;
}
void SetGroupBanTraits::validate_result(const request_type &request,
                                        const result_type &result) {
  standard::validate_outcome_mode(request.control.execution, result);
}

auto GatewayCodec<standard::SetGroupBanRequest>::encode(
    const standard::SetGroupBanRequest &request) -> Json {
  standard::RequestBudget budget(request.value_limits);
  standard::mutation_projection(budget, request, false);
  standard::validate_mutation_fields(request);
  const auto deadline =
      request.deadline_
          ? *request.deadline_
          : standard::deadline_after(request.control.limits.timeout);
  if (request.deadline_)
    standard::validate_preserved_deadline(deadline,
                                          request.control.limits.timeout);
  const auto ticks = deadline.time_since_epoch().count();
  if (!std::in_range<std::int64_t>(ticks))
    throw std::invalid_argument(
        "standard deadline exceeds wire representation");
  standard::mutation_projection(budget, request, true);
  const auto &limits = request.control.limits;
  const auto &values = request.value_limits;
  // Native parameter bytes do not bound this internal metadata envelope.
  return {
      {"parameters",
       {{"group", request.parameters.group},
        {"user", request.parameters.user},
        {"duration", standard::duration_json(request.parameters.duration)}}},
      {"control",
       {{"execution", standard::execution_name(request.control.execution)},
        {"limits",
         {{"maximum_parameter_bytes", limits.maximum_parameter_bytes},
          {"maximum_response_bytes", limits.maximum_response_bytes},
          {"timeout_milliseconds", limits.timeout.count()}}},
        {"correlation",
         request.control.correlation
             ? Json{{"attempt_id", request.control.correlation->attempt_id}}
             : Json(nullptr)}}},
      {"value_limits",
       {{"maximum_bytes", values.maximum_bytes},
        {"maximum_nodes", values.maximum_nodes},
        {"maximum_depth", values.maximum_depth},
        {"maximum_text_bytes", values.maximum_text_bytes}}},
      {"deadline_ticks", static_cast<std::int64_t>(ticks)}};
}

auto GatewayCodec<standard::SetGroupBanRequest>::decode(const Json &payload)
    -> standard::SetGroupBanRequest {
  detail::require_object(payload, "standard set_group_ban request");
  const auto values = standard::value_limits(payload.at("value_limits"));
  standard::RequestBudget budget(values);
  budget.inspect(payload,
                 0); // Includes unknown/discarded fields, before copies.
  const auto &parameters = payload.at("parameters");
  const auto &control = payload.at("control");
  detail::require_object(parameters, "standard parameters");
  detail::require_object(control, "standard control");
  const auto mode = standard::execution_mode(control.at("execution"));
  const auto &limits = control.at("limits");
  detail::require_object(limits, "standard call limits");
  const auto timeout = standard::integer(limits.at("timeout_milliseconds"));
  if (!std::in_range<std::chrono::milliseconds::rep>(timeout))
    throw std::invalid_argument("standard timeout exceeds representation");
  standard::CallLimits calls{
      standard::size_value(limits.at("maximum_parameter_bytes")),
      standard::size_value(limits.at("maximum_response_bytes")),
      std::chrono::milliseconds{timeout}};
  calls.validate();
  const auto deadline = standard::deadline_from_ticks(
      standard::integer(payload.at("deadline_ticks")));
  standard::validate_preserved_deadline(deadline, calls.timeout);
  const auto &correlation = control.at("correlation");
  std::optional<std::string_view> correlation_text;
  if (!correlation.is_null()) {
    detail::require_object(correlation, "standard correlation");
    correlation_text = standard::string(correlation.at("attempt_id"));
  }
  budget.value(0);
  standard::MutationProjection projection(budget, false);
  projection.field("parameters", 1);
  standard::project_parameters(
      projection, std::type_identity<standard::SetGroupBanParameters>{},
      parameters);
  projection.envelope(mode, correlation_text);
  // The whole borrowed envelope and prospective retained DTO are bounded
  // before reference decoding or any owning text construction.
  standard::SetGroupBanRequest request{
      {parameters.at("group").get<ConversationRef>(),
       parameters.at("user").get<UserRef>(),
       standard::duration(parameters.at("duration"))},
      {mode, calls,
       correlation_text
           ? std::optional{OperationCorrelation{std::string{*correlation_text}}}
           : std::nullopt},
      values};
  request.deadline_ = deadline;
  standard::validate_mutation_fields(request);
  return request;
}
using SetGroupWholeBanTraits =
    OperationTraits<standard::SetGroupWholeBanRequest>;

auto SetGroupWholeBanTraits::action() -> const ActionId & {
  static const ActionId id{"onebot11.standard.set_group_whole_ban"};
  return id;
}
auto SetGroupWholeBanTraits::installation(const request_type &request)
    -> const BotInstallationRef & {
  return request.parameters.group.installation;
}
auto SetGroupWholeBanTraits::supports_surface(const SurfaceId &surface)
    -> bool {
  return surface == onebot11::bot::surface;
}
void SetGroupWholeBanTraits::validate_result(const request_type &request,
                                             const result_type &result) {
  standard::validate_outcome_mode(request.control.execution, result);
}

auto GatewayCodec<standard::SetGroupWholeBanRequest>::encode(
    const standard::SetGroupWholeBanRequest &request) -> Json {
  standard::RequestBudget budget(request.value_limits);
  standard::mutation_projection(budget, request, false);
  standard::validate_mutation_fields(request);
  const auto deadline =
      request.deadline_
          ? *request.deadline_
          : standard::deadline_after(request.control.limits.timeout);
  if (request.deadline_)
    standard::validate_preserved_deadline(deadline,
                                          request.control.limits.timeout);
  const auto ticks = deadline.time_since_epoch().count();
  if (!std::in_range<std::int64_t>(ticks))
    throw std::invalid_argument(
        "standard deadline exceeds wire representation");
  standard::mutation_projection(budget, request, true);
  const auto &limits = request.control.limits;
  const auto &values = request.value_limits;
  // Native parameter bytes do not bound this internal metadata envelope.
  return {{"parameters",
           {{"group", request.parameters.group},
            {"enable", request.parameters.enable}}},
          {"control",
           {{"execution", standard::execution_name(request.control.execution)},
            {"limits",
             {{"maximum_parameter_bytes", limits.maximum_parameter_bytes},
              {"maximum_response_bytes", limits.maximum_response_bytes},
              {"timeout_milliseconds", limits.timeout.count()}}},
            {"correlation",
             request.control.correlation
                 ? Json{{"attempt_id", request.control.correlation->attempt_id}}
                 : Json(nullptr)}}},
          {"value_limits",
           {{"maximum_bytes", values.maximum_bytes},
            {"maximum_nodes", values.maximum_nodes},
            {"maximum_depth", values.maximum_depth},
            {"maximum_text_bytes", values.maximum_text_bytes}}},
          {"deadline_ticks", static_cast<std::int64_t>(ticks)}};
}

auto GatewayCodec<standard::SetGroupWholeBanRequest>::decode(
    const Json &payload) -> standard::SetGroupWholeBanRequest {
  detail::require_object(payload, "standard set_group_whole_ban request");
  const auto values = standard::value_limits(payload.at("value_limits"));
  standard::RequestBudget budget(values);
  budget.inspect(payload,
                 0); // Includes unknown/discarded fields, before copies.
  const auto &parameters = payload.at("parameters");
  const auto &control = payload.at("control");
  detail::require_object(parameters, "standard parameters");
  detail::require_object(control, "standard control");
  const auto mode = standard::execution_mode(control.at("execution"));
  const auto &limits = control.at("limits");
  detail::require_object(limits, "standard call limits");
  const auto timeout = standard::integer(limits.at("timeout_milliseconds"));
  if (!std::in_range<std::chrono::milliseconds::rep>(timeout))
    throw std::invalid_argument("standard timeout exceeds representation");
  standard::CallLimits calls{
      standard::size_value(limits.at("maximum_parameter_bytes")),
      standard::size_value(limits.at("maximum_response_bytes")),
      std::chrono::milliseconds{timeout}};
  calls.validate();
  const auto deadline = standard::deadline_from_ticks(
      standard::integer(payload.at("deadline_ticks")));
  standard::validate_preserved_deadline(deadline, calls.timeout);
  const auto &correlation = control.at("correlation");
  std::optional<std::string_view> correlation_text;
  if (!correlation.is_null()) {
    detail::require_object(correlation, "standard correlation");
    correlation_text = standard::string(correlation.at("attempt_id"));
  }
  budget.value(0);
  standard::MutationProjection projection(budget, false);
  projection.field("parameters", 1);
  standard::project_parameters(
      projection, std::type_identity<standard::SetGroupWholeBanParameters>{},
      parameters);
  projection.envelope(mode, correlation_text);
  // The whole borrowed envelope and prospective retained DTO are bounded
  // before reference decoding or any owning text construction.
  standard::SetGroupWholeBanRequest request{
      {parameters.at("group").get<ConversationRef>(),
       standard::boolean(parameters.at("enable"))},
      {mode, calls,
       correlation_text
           ? std::optional{OperationCorrelation{std::string{*correlation_text}}}
           : std::nullopt},
      values};
  request.deadline_ = deadline;
  standard::validate_mutation_fields(request);
  return request;
}
using SetGroupAdminTraits = OperationTraits<standard::SetGroupAdminRequest>;

auto SetGroupAdminTraits::action() -> const ActionId & {
  static const ActionId id{"onebot11.standard.set_group_admin"};
  return id;
}
auto SetGroupAdminTraits::installation(const request_type &request)
    -> const BotInstallationRef & {
  return request.parameters.group.installation;
}
auto SetGroupAdminTraits::supports_surface(const SurfaceId &surface) -> bool {
  return surface == onebot11::bot::surface;
}
void SetGroupAdminTraits::validate_result(const request_type &request,
                                          const result_type &result) {
  standard::validate_outcome_mode(request.control.execution, result);
}

auto GatewayCodec<standard::SetGroupAdminRequest>::encode(
    const standard::SetGroupAdminRequest &request) -> Json {
  standard::RequestBudget budget(request.value_limits);
  standard::mutation_projection(budget, request, false);
  standard::validate_mutation_fields(request);
  const auto deadline =
      request.deadline_
          ? *request.deadline_
          : standard::deadline_after(request.control.limits.timeout);
  if (request.deadline_)
    standard::validate_preserved_deadline(deadline,
                                          request.control.limits.timeout);
  const auto ticks = deadline.time_since_epoch().count();
  if (!std::in_range<std::int64_t>(ticks))
    throw std::invalid_argument(
        "standard deadline exceeds wire representation");
  standard::mutation_projection(budget, request, true);
  const auto &limits = request.control.limits;
  const auto &values = request.value_limits;
  // Native parameter bytes do not bound this internal metadata envelope.
  return {{"parameters",
           {{"group", request.parameters.group},
            {"user", request.parameters.user},
            {"enable", request.parameters.enable}}},
          {"control",
           {{"execution", standard::execution_name(request.control.execution)},
            {"limits",
             {{"maximum_parameter_bytes", limits.maximum_parameter_bytes},
              {"maximum_response_bytes", limits.maximum_response_bytes},
              {"timeout_milliseconds", limits.timeout.count()}}},
            {"correlation",
             request.control.correlation
                 ? Json{{"attempt_id", request.control.correlation->attempt_id}}
                 : Json(nullptr)}}},
          {"value_limits",
           {{"maximum_bytes", values.maximum_bytes},
            {"maximum_nodes", values.maximum_nodes},
            {"maximum_depth", values.maximum_depth},
            {"maximum_text_bytes", values.maximum_text_bytes}}},
          {"deadline_ticks", static_cast<std::int64_t>(ticks)}};
}

auto GatewayCodec<standard::SetGroupAdminRequest>::decode(const Json &payload)
    -> standard::SetGroupAdminRequest {
  detail::require_object(payload, "standard set_group_admin request");
  const auto values = standard::value_limits(payload.at("value_limits"));
  standard::RequestBudget budget(values);
  budget.inspect(payload,
                 0); // Includes unknown/discarded fields, before copies.
  const auto &parameters = payload.at("parameters");
  const auto &control = payload.at("control");
  detail::require_object(parameters, "standard parameters");
  detail::require_object(control, "standard control");
  const auto mode = standard::execution_mode(control.at("execution"));
  const auto &limits = control.at("limits");
  detail::require_object(limits, "standard call limits");
  const auto timeout = standard::integer(limits.at("timeout_milliseconds"));
  if (!std::in_range<std::chrono::milliseconds::rep>(timeout))
    throw std::invalid_argument("standard timeout exceeds representation");
  standard::CallLimits calls{
      standard::size_value(limits.at("maximum_parameter_bytes")),
      standard::size_value(limits.at("maximum_response_bytes")),
      std::chrono::milliseconds{timeout}};
  calls.validate();
  const auto deadline = standard::deadline_from_ticks(
      standard::integer(payload.at("deadline_ticks")));
  standard::validate_preserved_deadline(deadline, calls.timeout);
  const auto &correlation = control.at("correlation");
  std::optional<std::string_view> correlation_text;
  if (!correlation.is_null()) {
    detail::require_object(correlation, "standard correlation");
    correlation_text = standard::string(correlation.at("attempt_id"));
  }
  budget.value(0);
  standard::MutationProjection projection(budget, false);
  projection.field("parameters", 1);
  standard::project_parameters(
      projection, std::type_identity<standard::SetGroupAdminParameters>{},
      parameters);
  projection.envelope(mode, correlation_text);
  // The whole borrowed envelope and prospective retained DTO are bounded
  // before reference decoding or any owning text construction.
  standard::SetGroupAdminRequest request{
      {parameters.at("group").get<ConversationRef>(),
       parameters.at("user").get<UserRef>(),
       standard::boolean(parameters.at("enable"))},
      {mode, calls,
       correlation_text
           ? std::optional{OperationCorrelation{std::string{*correlation_text}}}
           : std::nullopt},
      values};
  request.deadline_ = deadline;
  standard::validate_mutation_fields(request);
  return request;
}
using SetGroupAnonymousTraits =
    OperationTraits<standard::SetGroupAnonymousRequest>;

auto SetGroupAnonymousTraits::action() -> const ActionId & {
  static const ActionId id{"onebot11.standard.set_group_anonymous"};
  return id;
}
auto SetGroupAnonymousTraits::installation(const request_type &request)
    -> const BotInstallationRef & {
  return request.parameters.group.installation;
}
auto SetGroupAnonymousTraits::supports_surface(const SurfaceId &surface)
    -> bool {
  return surface == onebot11::bot::surface;
}
void SetGroupAnonymousTraits::validate_result(const request_type &request,
                                              const result_type &result) {
  standard::validate_outcome_mode(request.control.execution, result);
}

auto GatewayCodec<standard::SetGroupAnonymousRequest>::encode(
    const standard::SetGroupAnonymousRequest &request) -> Json {
  standard::RequestBudget budget(request.value_limits);
  standard::mutation_projection(budget, request, false);
  standard::validate_mutation_fields(request);
  const auto deadline =
      request.deadline_
          ? *request.deadline_
          : standard::deadline_after(request.control.limits.timeout);
  if (request.deadline_)
    standard::validate_preserved_deadline(deadline,
                                          request.control.limits.timeout);
  const auto ticks = deadline.time_since_epoch().count();
  if (!std::in_range<std::int64_t>(ticks))
    throw std::invalid_argument(
        "standard deadline exceeds wire representation");
  standard::mutation_projection(budget, request, true);
  const auto &limits = request.control.limits;
  const auto &values = request.value_limits;
  // Native parameter bytes do not bound this internal metadata envelope.
  return {{"parameters",
           {{"group", request.parameters.group},
            {"enable", request.parameters.enable}}},
          {"control",
           {{"execution", standard::execution_name(request.control.execution)},
            {"limits",
             {{"maximum_parameter_bytes", limits.maximum_parameter_bytes},
              {"maximum_response_bytes", limits.maximum_response_bytes},
              {"timeout_milliseconds", limits.timeout.count()}}},
            {"correlation",
             request.control.correlation
                 ? Json{{"attempt_id", request.control.correlation->attempt_id}}
                 : Json(nullptr)}}},
          {"value_limits",
           {{"maximum_bytes", values.maximum_bytes},
            {"maximum_nodes", values.maximum_nodes},
            {"maximum_depth", values.maximum_depth},
            {"maximum_text_bytes", values.maximum_text_bytes}}},
          {"deadline_ticks", static_cast<std::int64_t>(ticks)}};
}

auto GatewayCodec<standard::SetGroupAnonymousRequest>::decode(
    const Json &payload) -> standard::SetGroupAnonymousRequest {
  detail::require_object(payload, "standard set_group_anonymous request");
  const auto values = standard::value_limits(payload.at("value_limits"));
  standard::RequestBudget budget(values);
  budget.inspect(payload,
                 0); // Includes unknown/discarded fields, before copies.
  const auto &parameters = payload.at("parameters");
  const auto &control = payload.at("control");
  detail::require_object(parameters, "standard parameters");
  detail::require_object(control, "standard control");
  const auto mode = standard::execution_mode(control.at("execution"));
  const auto &limits = control.at("limits");
  detail::require_object(limits, "standard call limits");
  const auto timeout = standard::integer(limits.at("timeout_milliseconds"));
  if (!std::in_range<std::chrono::milliseconds::rep>(timeout))
    throw std::invalid_argument("standard timeout exceeds representation");
  standard::CallLimits calls{
      standard::size_value(limits.at("maximum_parameter_bytes")),
      standard::size_value(limits.at("maximum_response_bytes")),
      std::chrono::milliseconds{timeout}};
  calls.validate();
  const auto deadline = standard::deadline_from_ticks(
      standard::integer(payload.at("deadline_ticks")));
  standard::validate_preserved_deadline(deadline, calls.timeout);
  const auto &correlation = control.at("correlation");
  std::optional<std::string_view> correlation_text;
  if (!correlation.is_null()) {
    detail::require_object(correlation, "standard correlation");
    correlation_text = standard::string(correlation.at("attempt_id"));
  }
  budget.value(0);
  standard::MutationProjection projection(budget, false);
  projection.field("parameters", 1);
  standard::project_parameters(
      projection, std::type_identity<standard::SetGroupAnonymousParameters>{},
      parameters);
  projection.envelope(mode, correlation_text);
  // The whole borrowed envelope and prospective retained DTO are bounded
  // before reference decoding or any owning text construction.
  standard::SetGroupAnonymousRequest request{
      {parameters.at("group").get<ConversationRef>(),
       standard::boolean(parameters.at("enable"))},
      {mode, calls,
       correlation_text
           ? std::optional{OperationCorrelation{std::string{*correlation_text}}}
           : std::nullopt},
      values};
  request.deadline_ = deadline;
  standard::validate_mutation_fields(request);
  return request;
}
using SetGroupCardTraits = OperationTraits<standard::SetGroupCardRequest>;

auto SetGroupCardTraits::action() -> const ActionId & {
  static const ActionId id{"onebot11.standard.set_group_card"};
  return id;
}
auto SetGroupCardTraits::installation(const request_type &request)
    -> const BotInstallationRef & {
  return request.parameters.group.installation;
}
auto SetGroupCardTraits::supports_surface(const SurfaceId &surface) -> bool {
  return surface == onebot11::bot::surface;
}
void SetGroupCardTraits::validate_result(const request_type &request,
                                         const result_type &result) {
  standard::validate_outcome_mode(request.control.execution, result);
}

auto GatewayCodec<standard::SetGroupCardRequest>::encode(
    const standard::SetGroupCardRequest &request) -> Json {
  standard::RequestBudget budget(request.value_limits);
  standard::mutation_projection(budget, request, false);
  standard::validate_mutation_fields(request);
  const auto deadline =
      request.deadline_
          ? *request.deadline_
          : standard::deadline_after(request.control.limits.timeout);
  if (request.deadline_)
    standard::validate_preserved_deadline(deadline,
                                          request.control.limits.timeout);
  const auto ticks = deadline.time_since_epoch().count();
  if (!std::in_range<std::int64_t>(ticks))
    throw std::invalid_argument(
        "standard deadline exceeds wire representation");
  standard::mutation_projection(budget, request, true);
  const auto &limits = request.control.limits;
  const auto &values = request.value_limits;
  // Native parameter bytes do not bound this internal metadata envelope.
  return {{"parameters",
           {{"group", request.parameters.group},
            {"user", request.parameters.user},
            {"card", request.parameters.card}}},
          {"control",
           {{"execution", standard::execution_name(request.control.execution)},
            {"limits",
             {{"maximum_parameter_bytes", limits.maximum_parameter_bytes},
              {"maximum_response_bytes", limits.maximum_response_bytes},
              {"timeout_milliseconds", limits.timeout.count()}}},
            {"correlation",
             request.control.correlation
                 ? Json{{"attempt_id", request.control.correlation->attempt_id}}
                 : Json(nullptr)}}},
          {"value_limits",
           {{"maximum_bytes", values.maximum_bytes},
            {"maximum_nodes", values.maximum_nodes},
            {"maximum_depth", values.maximum_depth},
            {"maximum_text_bytes", values.maximum_text_bytes}}},
          {"deadline_ticks", static_cast<std::int64_t>(ticks)}};
}

auto GatewayCodec<standard::SetGroupCardRequest>::decode(const Json &payload)
    -> standard::SetGroupCardRequest {
  detail::require_object(payload, "standard set_group_card request");
  const auto values = standard::value_limits(payload.at("value_limits"));
  standard::RequestBudget budget(values);
  budget.inspect(payload,
                 0); // Includes unknown/discarded fields, before copies.
  const auto &parameters = payload.at("parameters");
  const auto &control = payload.at("control");
  detail::require_object(parameters, "standard parameters");
  detail::require_object(control, "standard control");
  const auto mode = standard::execution_mode(control.at("execution"));
  const auto &limits = control.at("limits");
  detail::require_object(limits, "standard call limits");
  const auto timeout = standard::integer(limits.at("timeout_milliseconds"));
  if (!std::in_range<std::chrono::milliseconds::rep>(timeout))
    throw std::invalid_argument("standard timeout exceeds representation");
  standard::CallLimits calls{
      standard::size_value(limits.at("maximum_parameter_bytes")),
      standard::size_value(limits.at("maximum_response_bytes")),
      std::chrono::milliseconds{timeout}};
  calls.validate();
  const auto deadline = standard::deadline_from_ticks(
      standard::integer(payload.at("deadline_ticks")));
  standard::validate_preserved_deadline(deadline, calls.timeout);
  const auto &correlation = control.at("correlation");
  std::optional<std::string_view> correlation_text;
  if (!correlation.is_null()) {
    detail::require_object(correlation, "standard correlation");
    correlation_text = standard::string(correlation.at("attempt_id"));
  }
  budget.value(0);
  standard::MutationProjection projection(budget, false);
  projection.field("parameters", 1);
  standard::project_parameters(
      projection, std::type_identity<standard::SetGroupCardParameters>{},
      parameters);
  projection.envelope(mode, correlation_text);
  // The whole borrowed envelope and prospective retained DTO are bounded
  // before reference decoding or any owning text construction.
  standard::SetGroupCardRequest request{
      {parameters.at("group").get<ConversationRef>(),
       parameters.at("user").get<UserRef>(),
       standard::string(parameters.at("card"))},
      {mode, calls,
       correlation_text
           ? std::optional{OperationCorrelation{std::string{*correlation_text}}}
           : std::nullopt},
      values};
  request.deadline_ = deadline;
  standard::validate_mutation_fields(request);
  return request;
}
using SetGroupNameTraits = OperationTraits<standard::SetGroupNameRequest>;

auto SetGroupNameTraits::action() -> const ActionId & {
  static const ActionId id{"onebot11.standard.set_group_name"};
  return id;
}
auto SetGroupNameTraits::installation(const request_type &request)
    -> const BotInstallationRef & {
  return request.parameters.group.installation;
}
auto SetGroupNameTraits::supports_surface(const SurfaceId &surface) -> bool {
  return surface == onebot11::bot::surface;
}
void SetGroupNameTraits::validate_result(const request_type &request,
                                         const result_type &result) {
  standard::validate_outcome_mode(request.control.execution, result);
}

auto GatewayCodec<standard::SetGroupNameRequest>::encode(
    const standard::SetGroupNameRequest &request) -> Json {
  standard::RequestBudget budget(request.value_limits);
  standard::mutation_projection(budget, request, false);
  standard::validate_mutation_fields(request);
  const auto deadline =
      request.deadline_
          ? *request.deadline_
          : standard::deadline_after(request.control.limits.timeout);
  if (request.deadline_)
    standard::validate_preserved_deadline(deadline,
                                          request.control.limits.timeout);
  const auto ticks = deadline.time_since_epoch().count();
  if (!std::in_range<std::int64_t>(ticks))
    throw std::invalid_argument(
        "standard deadline exceeds wire representation");
  standard::mutation_projection(budget, request, true);
  const auto &limits = request.control.limits;
  const auto &values = request.value_limits;
  // Native parameter bytes do not bound this internal metadata envelope.
  return {{"parameters",
           {{"group", request.parameters.group},
            {"group_name", request.parameters.group_name}}},
          {"control",
           {{"execution", standard::execution_name(request.control.execution)},
            {"limits",
             {{"maximum_parameter_bytes", limits.maximum_parameter_bytes},
              {"maximum_response_bytes", limits.maximum_response_bytes},
              {"timeout_milliseconds", limits.timeout.count()}}},
            {"correlation",
             request.control.correlation
                 ? Json{{"attempt_id", request.control.correlation->attempt_id}}
                 : Json(nullptr)}}},
          {"value_limits",
           {{"maximum_bytes", values.maximum_bytes},
            {"maximum_nodes", values.maximum_nodes},
            {"maximum_depth", values.maximum_depth},
            {"maximum_text_bytes", values.maximum_text_bytes}}},
          {"deadline_ticks", static_cast<std::int64_t>(ticks)}};
}

auto GatewayCodec<standard::SetGroupNameRequest>::decode(const Json &payload)
    -> standard::SetGroupNameRequest {
  detail::require_object(payload, "standard set_group_name request");
  const auto values = standard::value_limits(payload.at("value_limits"));
  standard::RequestBudget budget(values);
  budget.inspect(payload,
                 0); // Includes unknown/discarded fields, before copies.
  const auto &parameters = payload.at("parameters");
  const auto &control = payload.at("control");
  detail::require_object(parameters, "standard parameters");
  detail::require_object(control, "standard control");
  const auto mode = standard::execution_mode(control.at("execution"));
  const auto &limits = control.at("limits");
  detail::require_object(limits, "standard call limits");
  const auto timeout = standard::integer(limits.at("timeout_milliseconds"));
  if (!std::in_range<std::chrono::milliseconds::rep>(timeout))
    throw std::invalid_argument("standard timeout exceeds representation");
  standard::CallLimits calls{
      standard::size_value(limits.at("maximum_parameter_bytes")),
      standard::size_value(limits.at("maximum_response_bytes")),
      std::chrono::milliseconds{timeout}};
  calls.validate();
  const auto deadline = standard::deadline_from_ticks(
      standard::integer(payload.at("deadline_ticks")));
  standard::validate_preserved_deadline(deadline, calls.timeout);
  const auto &correlation = control.at("correlation");
  std::optional<std::string_view> correlation_text;
  if (!correlation.is_null()) {
    detail::require_object(correlation, "standard correlation");
    correlation_text = standard::string(correlation.at("attempt_id"));
  }
  budget.value(0);
  standard::MutationProjection projection(budget, false);
  projection.field("parameters", 1);
  standard::project_parameters(
      projection, std::type_identity<standard::SetGroupNameParameters>{},
      parameters);
  projection.envelope(mode, correlation_text);
  // The whole borrowed envelope and prospective retained DTO are bounded
  // before reference decoding or any owning text construction.
  standard::SetGroupNameRequest request{
      {parameters.at("group").get<ConversationRef>(),
       standard::string(parameters.at("group_name"))},
      {mode, calls,
       correlation_text
           ? std::optional{OperationCorrelation{std::string{*correlation_text}}}
           : std::nullopt},
      values};
  request.deadline_ = deadline;
  standard::validate_mutation_fields(request);
  return request;
}
using SetGroupLeaveTraits = OperationTraits<standard::SetGroupLeaveRequest>;

auto SetGroupLeaveTraits::action() -> const ActionId & {
  static const ActionId id{"onebot11.standard.set_group_leave"};
  return id;
}
auto SetGroupLeaveTraits::installation(const request_type &request)
    -> const BotInstallationRef & {
  return request.parameters.group.installation;
}
auto SetGroupLeaveTraits::supports_surface(const SurfaceId &surface) -> bool {
  return surface == onebot11::bot::surface;
}
void SetGroupLeaveTraits::validate_result(const request_type &request,
                                          const result_type &result) {
  standard::validate_outcome_mode(request.control.execution, result);
}

auto GatewayCodec<standard::SetGroupLeaveRequest>::encode(
    const standard::SetGroupLeaveRequest &request) -> Json {
  standard::RequestBudget budget(request.value_limits);
  standard::mutation_projection(budget, request, false);
  standard::validate_mutation_fields(request);
  const auto deadline =
      request.deadline_
          ? *request.deadline_
          : standard::deadline_after(request.control.limits.timeout);
  if (request.deadline_)
    standard::validate_preserved_deadline(deadline,
                                          request.control.limits.timeout);
  const auto ticks = deadline.time_since_epoch().count();
  if (!std::in_range<std::int64_t>(ticks))
    throw std::invalid_argument(
        "standard deadline exceeds wire representation");
  standard::mutation_projection(budget, request, true);
  const auto &limits = request.control.limits;
  const auto &values = request.value_limits;
  // Native parameter bytes do not bound this internal metadata envelope.
  return {{"parameters",
           {{"group", request.parameters.group},
            {"is_dismiss", request.parameters.is_dismiss}}},
          {"control",
           {{"execution", standard::execution_name(request.control.execution)},
            {"limits",
             {{"maximum_parameter_bytes", limits.maximum_parameter_bytes},
              {"maximum_response_bytes", limits.maximum_response_bytes},
              {"timeout_milliseconds", limits.timeout.count()}}},
            {"correlation",
             request.control.correlation
                 ? Json{{"attempt_id", request.control.correlation->attempt_id}}
                 : Json(nullptr)}}},
          {"value_limits",
           {{"maximum_bytes", values.maximum_bytes},
            {"maximum_nodes", values.maximum_nodes},
            {"maximum_depth", values.maximum_depth},
            {"maximum_text_bytes", values.maximum_text_bytes}}},
          {"deadline_ticks", static_cast<std::int64_t>(ticks)}};
}

auto GatewayCodec<standard::SetGroupLeaveRequest>::decode(const Json &payload)
    -> standard::SetGroupLeaveRequest {
  detail::require_object(payload, "standard set_group_leave request");
  const auto values = standard::value_limits(payload.at("value_limits"));
  standard::RequestBudget budget(values);
  budget.inspect(payload,
                 0); // Includes unknown/discarded fields, before copies.
  const auto &parameters = payload.at("parameters");
  const auto &control = payload.at("control");
  detail::require_object(parameters, "standard parameters");
  detail::require_object(control, "standard control");
  const auto mode = standard::execution_mode(control.at("execution"));
  const auto &limits = control.at("limits");
  detail::require_object(limits, "standard call limits");
  const auto timeout = standard::integer(limits.at("timeout_milliseconds"));
  if (!std::in_range<std::chrono::milliseconds::rep>(timeout))
    throw std::invalid_argument("standard timeout exceeds representation");
  standard::CallLimits calls{
      standard::size_value(limits.at("maximum_parameter_bytes")),
      standard::size_value(limits.at("maximum_response_bytes")),
      std::chrono::milliseconds{timeout}};
  calls.validate();
  const auto deadline = standard::deadline_from_ticks(
      standard::integer(payload.at("deadline_ticks")));
  standard::validate_preserved_deadline(deadline, calls.timeout);
  const auto &correlation = control.at("correlation");
  std::optional<std::string_view> correlation_text;
  if (!correlation.is_null()) {
    detail::require_object(correlation, "standard correlation");
    correlation_text = standard::string(correlation.at("attempt_id"));
  }
  budget.value(0);
  standard::MutationProjection projection(budget, false);
  projection.field("parameters", 1);
  standard::project_parameters(
      projection, std::type_identity<standard::SetGroupLeaveParameters>{},
      parameters);
  projection.envelope(mode, correlation_text);
  // The whole borrowed envelope and prospective retained DTO are bounded
  // before reference decoding or any owning text construction.
  standard::SetGroupLeaveRequest request{
      {parameters.at("group").get<ConversationRef>(),
       standard::boolean(parameters.at("is_dismiss"))},
      {mode, calls,
       correlation_text
           ? std::optional{OperationCorrelation{std::string{*correlation_text}}}
           : std::nullopt},
      values};
  request.deadline_ = deadline;
  standard::validate_mutation_fields(request);
  return request;
}
using SetGroupSpecialTitleTraits =
    OperationTraits<standard::SetGroupSpecialTitleRequest>;

auto SetGroupSpecialTitleTraits::action() -> const ActionId & {
  static const ActionId id{"onebot11.standard.set_group_special_title"};
  return id;
}
auto SetGroupSpecialTitleTraits::installation(const request_type &request)
    -> const BotInstallationRef & {
  return request.parameters.group.installation;
}
auto SetGroupSpecialTitleTraits::supports_surface(const SurfaceId &surface)
    -> bool {
  return surface == onebot11::bot::surface;
}
void SetGroupSpecialTitleTraits::validate_result(const request_type &request,
                                                 const result_type &result) {
  standard::validate_outcome_mode(request.control.execution, result);
}

auto GatewayCodec<standard::SetGroupSpecialTitleRequest>::encode(
    const standard::SetGroupSpecialTitleRequest &request) -> Json {
  standard::RequestBudget budget(request.value_limits);
  standard::mutation_projection(budget, request, false);
  standard::validate_mutation_fields(request);
  const auto deadline =
      request.deadline_
          ? *request.deadline_
          : standard::deadline_after(request.control.limits.timeout);
  if (request.deadline_)
    standard::validate_preserved_deadline(deadline,
                                          request.control.limits.timeout);
  const auto ticks = deadline.time_since_epoch().count();
  if (!std::in_range<std::int64_t>(ticks))
    throw std::invalid_argument(
        "standard deadline exceeds wire representation");
  standard::mutation_projection(budget, request, true);
  const auto &limits = request.control.limits;
  const auto &values = request.value_limits;
  // Native parameter bytes do not bound this internal metadata envelope.
  return {
      {"parameters",
       {{"group", request.parameters.group},
        {"user", request.parameters.user},
        {"special_title", request.parameters.special_title},
        {"duration", standard::duration_json(request.parameters.duration)}}},
      {"control",
       {{"execution", standard::execution_name(request.control.execution)},
        {"limits",
         {{"maximum_parameter_bytes", limits.maximum_parameter_bytes},
          {"maximum_response_bytes", limits.maximum_response_bytes},
          {"timeout_milliseconds", limits.timeout.count()}}},
        {"correlation",
         request.control.correlation
             ? Json{{"attempt_id", request.control.correlation->attempt_id}}
             : Json(nullptr)}}},
      {"value_limits",
       {{"maximum_bytes", values.maximum_bytes},
        {"maximum_nodes", values.maximum_nodes},
        {"maximum_depth", values.maximum_depth},
        {"maximum_text_bytes", values.maximum_text_bytes}}},
      {"deadline_ticks", static_cast<std::int64_t>(ticks)}};
}

auto GatewayCodec<standard::SetGroupSpecialTitleRequest>::decode(
    const Json &payload) -> standard::SetGroupSpecialTitleRequest {
  detail::require_object(payload, "standard set_group_special_title request");
  const auto values = standard::value_limits(payload.at("value_limits"));
  standard::RequestBudget budget(values);
  budget.inspect(payload,
                 0); // Includes unknown/discarded fields, before copies.
  const auto &parameters = payload.at("parameters");
  const auto &control = payload.at("control");
  detail::require_object(parameters, "standard parameters");
  detail::require_object(control, "standard control");
  const auto mode = standard::execution_mode(control.at("execution"));
  const auto &limits = control.at("limits");
  detail::require_object(limits, "standard call limits");
  const auto timeout = standard::integer(limits.at("timeout_milliseconds"));
  if (!std::in_range<std::chrono::milliseconds::rep>(timeout))
    throw std::invalid_argument("standard timeout exceeds representation");
  standard::CallLimits calls{
      standard::size_value(limits.at("maximum_parameter_bytes")),
      standard::size_value(limits.at("maximum_response_bytes")),
      std::chrono::milliseconds{timeout}};
  calls.validate();
  const auto deadline = standard::deadline_from_ticks(
      standard::integer(payload.at("deadline_ticks")));
  standard::validate_preserved_deadline(deadline, calls.timeout);
  const auto &correlation = control.at("correlation");
  std::optional<std::string_view> correlation_text;
  if (!correlation.is_null()) {
    detail::require_object(correlation, "standard correlation");
    correlation_text = standard::string(correlation.at("attempt_id"));
  }
  budget.value(0);
  standard::MutationProjection projection(budget, false);
  projection.field("parameters", 1);
  standard::project_parameters(
      projection,
      std::type_identity<standard::SetGroupSpecialTitleParameters>{},
      parameters);
  projection.envelope(mode, correlation_text);
  // The whole borrowed envelope and prospective retained DTO are bounded
  // before reference decoding or any owning text construction.
  standard::SetGroupSpecialTitleRequest request{
      {parameters.at("group").get<ConversationRef>(),
       parameters.at("user").get<UserRef>(),
       standard::string(parameters.at("special_title")),
       standard::duration(parameters.at("duration"))},
      {mode, calls,
       correlation_text
           ? std::optional{OperationCorrelation{std::string{*correlation_text}}}
           : std::nullopt},
      values};
  request.deadline_ = deadline;
  standard::validate_mutation_fields(request);
  return request;
}
using GetStrangerInfoTraits = OperationTraits<standard::GetStrangerInfoRequest>;

auto GetStrangerInfoTraits::action() -> const ActionId & {
  static const ActionId id{"onebot11.standard.get_stranger_info"};
  return id;
}
auto GetStrangerInfoTraits::installation(const request_type &request)
    -> const BotInstallationRef & {
  return request.parameters.user.installation;
}
auto GetStrangerInfoTraits::supports_surface(const SurfaceId &surface) -> bool {
  return surface == onebot11::bot::surface;
}
void GetStrangerInfoTraits::validate_result(const request_type &request,
                                            const result_type &result) {
  standard::validate_outcome_mode(request.control.execution, result);
  if (const auto *completed =
          std::get_if<standard::Completed<standard::StrangerInfo>>(
              &result.state))
    standard::validate_query_result(request, completed->value);
}

auto GatewayCodec<standard::GetStrangerInfoRequest>::encode(
    const standard::GetStrangerInfoRequest &request) -> Json {
  standard::RequestBudget budget(request.value_limits);
  standard::query_projection(budget, request, false);
  standard::validate_query_fields(request);
  const auto deadline =
      request.deadline_
          ? *request.deadline_
          : standard::deadline_after(request.control.limits.timeout);
  if (request.deadline_)
    standard::validate_preserved_deadline(deadline,
                                          request.control.limits.timeout);
  const auto ticks = deadline.time_since_epoch().count();
  if (!std::in_range<std::int64_t>(ticks))
    throw std::invalid_argument(
        "standard deadline exceeds wire representation");
  standard::query_projection(budget, request, true);
  const auto &limits = request.control.limits;
  const auto &values = request.value_limits;
  // Native parameter bytes do not bound this internal metadata envelope.
  return {{"parameters",
           {{"user", request.parameters.user},
            {"no_cache", request.parameters.no_cache}}},
          {"control",
           {{"execution", standard::execution_name(request.control.execution)},
            {"limits",
             {{"maximum_parameter_bytes", limits.maximum_parameter_bytes},
              {"maximum_response_bytes", limits.maximum_response_bytes},
              {"timeout_milliseconds", limits.timeout.count()}}},
            {"correlation",
             request.control.correlation
                 ? Json{{"attempt_id", request.control.correlation->attempt_id}}
                 : Json(nullptr)}}},
          {"value_limits",
           {{"maximum_bytes", values.maximum_bytes},
            {"maximum_nodes", values.maximum_nodes},
            {"maximum_depth", values.maximum_depth},
            {"maximum_text_bytes", values.maximum_text_bytes}}},
          {"deadline_ticks", static_cast<std::int64_t>(ticks)}};
}

auto GatewayCodec<standard::GetStrangerInfoRequest>::decode(const Json &payload)
    -> standard::GetStrangerInfoRequest {
  detail::require_object(payload, "standard get_stranger_info request");
  const auto values = standard::value_limits(payload.at("value_limits"));
  standard::RequestBudget budget(values);
  budget.inspect(payload,
                 0); // Includes unknown/discarded fields, before copies.
  const auto &parameters = payload.at("parameters");
  const auto &control = payload.at("control");
  detail::require_object(parameters, "standard parameters");
  detail::require_object(control, "standard control");
  const auto mode = standard::execution_mode(control.at("execution"));
  const auto &limits = control.at("limits");
  detail::require_object(limits, "standard call limits");
  const auto timeout = standard::integer(limits.at("timeout_milliseconds"));
  if (!std::in_range<std::chrono::milliseconds::rep>(timeout))
    throw std::invalid_argument("standard timeout exceeds representation");
  standard::CallLimits calls{
      standard::size_value(limits.at("maximum_parameter_bytes")),
      standard::size_value(limits.at("maximum_response_bytes")),
      std::chrono::milliseconds{timeout}};
  calls.validate();
  const auto deadline = standard::deadline_from_ticks(
      standard::integer(payload.at("deadline_ticks")));
  standard::validate_preserved_deadline(deadline, calls.timeout);
  const auto &correlation = control.at("correlation");
  std::optional<std::string_view> correlation_text;
  if (!correlation.is_null()) {
    detail::require_object(correlation, "standard correlation");
    correlation_text = standard::string(correlation.at("attempt_id"));
  }
  budget.value(0);
  standard::MutationProjection projection(budget, false);
  projection.field("parameters", 1);
  standard::project_query_parameters(
      projection, std::type_identity<standard::GetStrangerInfoParameters>{},
      parameters, false);
  projection.envelope(mode, correlation_text);
  // The whole borrowed envelope and prospective retained DTO are bounded
  // before reference decoding or any owning text construction.
  standard::GetStrangerInfoRequest request{
      {parameters.at("user").get<UserRef>(),
       standard::boolean(parameters.at("no_cache"))},
      {mode, calls,
       correlation_text
           ? std::optional{OperationCorrelation{std::string{*correlation_text}}}
           : std::nullopt},
      values};
  request.deadline_ = deadline;
  standard::validate_query_fields(request);
  return request;
}
using GetLoginInfoTraits = OperationTraits<standard::GetLoginInfoRequest>;

auto GetLoginInfoTraits::action() -> const ActionId & {
  static const ActionId id{"onebot11.standard.get_login_info"};
  return id;
}
auto GetLoginInfoTraits::installation(const request_type &request)
    -> const BotInstallationRef & {
  return request.parameters.installation;
}
auto GetLoginInfoTraits::supports_surface(const SurfaceId &surface) -> bool {
  return surface == onebot11::bot::surface;
}
void GetLoginInfoTraits::validate_result(const request_type &request,
                                         const result_type &result) {
  standard::validate_outcome_mode(request.control.execution, result);
  if (const auto *completed =
          std::get_if<standard::Completed<standard::LoginInfo>>(&result.state))
    standard::validate_login_result(request, completed->value);
}

auto GatewayCodec<standard::GetLoginInfoRequest>::encode(
    const standard::GetLoginInfoRequest &request) -> Json {
  standard::RequestBudget budget(request.value_limits);
  standard::query_projection(budget, request, false);
  standard::validate_query_fields(request);
  const auto deadline =
      request.deadline_
          ? *request.deadline_
          : standard::deadline_after(request.control.limits.timeout);
  if (request.deadline_)
    standard::validate_preserved_deadline(deadline,
                                          request.control.limits.timeout);
  const auto ticks = deadline.time_since_epoch().count();
  if (!std::in_range<std::int64_t>(ticks))
    throw std::invalid_argument(
        "standard deadline exceeds wire representation");
  standard::query_projection(budget, request, true);
  const auto &limits = request.control.limits;
  const auto &values = request.value_limits;
  // Native parameter bytes do not bound this internal metadata envelope.
  return {{"parameters", {{"installation", request.parameters.installation}}},
          {"control",
           {{"execution", standard::execution_name(request.control.execution)},
            {"limits",
             {{"maximum_parameter_bytes", limits.maximum_parameter_bytes},
              {"maximum_response_bytes", limits.maximum_response_bytes},
              {"timeout_milliseconds", limits.timeout.count()}}},
            {"correlation",
             request.control.correlation
                 ? Json{{"attempt_id", request.control.correlation->attempt_id}}
                 : Json(nullptr)}}},
          {"value_limits",
           {{"maximum_bytes", values.maximum_bytes},
            {"maximum_nodes", values.maximum_nodes},
            {"maximum_depth", values.maximum_depth},
            {"maximum_text_bytes", values.maximum_text_bytes}}},
          {"deadline_ticks", static_cast<std::int64_t>(ticks)}};
}

auto GatewayCodec<standard::GetLoginInfoRequest>::decode(const Json &payload)
    -> standard::GetLoginInfoRequest {
  detail::require_object(payload, "standard get_login_info request");
  const auto values = standard::value_limits(payload.at("value_limits"));
  standard::RequestBudget budget(values);
  budget.inspect(payload,
                 0); // Includes unknown/discarded fields, before copies.
  const auto &parameters = payload.at("parameters");
  const auto &control = payload.at("control");
  detail::require_object(parameters, "standard parameters");
  detail::require_object(control, "standard control");
  const auto mode = standard::execution_mode(control.at("execution"));
  const auto &limits = control.at("limits");
  detail::require_object(limits, "standard call limits");
  const auto timeout = standard::integer(limits.at("timeout_milliseconds"));
  if (!std::in_range<std::chrono::milliseconds::rep>(timeout))
    throw std::invalid_argument("standard timeout exceeds representation");
  standard::CallLimits calls{
      standard::size_value(limits.at("maximum_parameter_bytes")),
      standard::size_value(limits.at("maximum_response_bytes")),
      std::chrono::milliseconds{timeout}};
  calls.validate();
  const auto deadline = standard::deadline_from_ticks(
      standard::integer(payload.at("deadline_ticks")));
  standard::validate_preserved_deadline(deadline, calls.timeout);
  const auto &correlation = control.at("correlation");
  std::optional<std::string_view> correlation_text;
  if (!correlation.is_null()) {
    detail::require_object(correlation, "standard correlation");
    correlation_text = standard::string(correlation.at("attempt_id"));
  }
  budget.value(0);
  standard::MutationProjection projection(budget, false);
  projection.field("parameters", 1);
  standard::project_query_parameters(
      projection, std::type_identity<standard::GetLoginInfoParameters>{},
      parameters, false);
  projection.envelope(mode, correlation_text);
  // The whole borrowed envelope and prospective retained DTO are bounded
  // before reference decoding or any owning text construction.
  standard::GetLoginInfoRequest request{
      {parameters.at("installation").get<BotInstallationRef>()},
      {mode, calls,
       correlation_text
           ? std::optional{OperationCorrelation{std::string{*correlation_text}}}
           : std::nullopt},
      values};
  request.deadline_ = deadline;
  standard::validate_query_fields(request);
  return request;
}
using GetFriendListTraits = OperationTraits<standard::GetFriendListRequest>;

auto GetFriendListTraits::action() -> const ActionId & {
  static const ActionId id{"onebot11.standard.get_friend_list"};
  return id;
}
auto GetFriendListTraits::installation(const request_type &request)
    -> const BotInstallationRef & {
  return request.parameters.installation;
}
auto GetFriendListTraits::supports_surface(const SurfaceId &surface) -> bool {
  return surface == onebot11::bot::surface;
}
void GetFriendListTraits::validate_result(const request_type &request,
                                          const result_type &result) {
  standard::validate_outcome_mode(request.control.execution, result);
  if (const auto *completed =
          std::get_if<standard::Completed<standard::FriendList>>(&result.state))
    standard::validate_query_result(request, completed->value);
}

auto GatewayCodec<standard::GetFriendListRequest>::encode(
    const standard::GetFriendListRequest &request) -> Json {
  standard::RequestBudget budget(request.value_limits);
  standard::query_projection(budget, request, false);
  standard::validate_query_fields(request);
  const auto deadline =
      request.deadline_
          ? *request.deadline_
          : standard::deadline_after(request.control.limits.timeout);
  if (request.deadline_)
    standard::validate_preserved_deadline(deadline,
                                          request.control.limits.timeout);
  const auto ticks = deadline.time_since_epoch().count();
  if (!std::in_range<std::int64_t>(ticks))
    throw std::invalid_argument(
        "standard deadline exceeds wire representation");
  standard::query_projection(budget, request, true);
  const auto &limits = request.control.limits;
  const auto &values = request.value_limits;
  // Native parameter bytes do not bound this internal metadata envelope.
  return {{"parameters", {{"installation", request.parameters.installation}}},
          {"control",
           {{"execution", standard::execution_name(request.control.execution)},
            {"limits",
             {{"maximum_parameter_bytes", limits.maximum_parameter_bytes},
              {"maximum_response_bytes", limits.maximum_response_bytes},
              {"timeout_milliseconds", limits.timeout.count()}}},
            {"correlation",
             request.control.correlation
                 ? Json{{"attempt_id", request.control.correlation->attempt_id}}
                 : Json(nullptr)}}},
          {"value_limits",
           {{"maximum_bytes", values.maximum_bytes},
            {"maximum_nodes", values.maximum_nodes},
            {"maximum_depth", values.maximum_depth},
            {"maximum_text_bytes", values.maximum_text_bytes}}},
          {"deadline_ticks", static_cast<std::int64_t>(ticks)}};
}

auto GatewayCodec<standard::GetFriendListRequest>::decode(const Json &payload)
    -> standard::GetFriendListRequest {
  detail::require_object(payload, "standard get_friend_list request");
  const auto values = standard::value_limits(payload.at("value_limits"));
  standard::RequestBudget budget(values);
  budget.inspect(payload,
                 0); // Includes unknown/discarded fields, before copies.
  const auto &parameters = payload.at("parameters");
  const auto &control = payload.at("control");
  detail::require_object(parameters, "standard parameters");
  detail::require_object(control, "standard control");
  const auto mode = standard::execution_mode(control.at("execution"));
  const auto &limits = control.at("limits");
  detail::require_object(limits, "standard call limits");
  const auto timeout = standard::integer(limits.at("timeout_milliseconds"));
  if (!std::in_range<std::chrono::milliseconds::rep>(timeout))
    throw std::invalid_argument("standard timeout exceeds representation");
  standard::CallLimits calls{
      standard::size_value(limits.at("maximum_parameter_bytes")),
      standard::size_value(limits.at("maximum_response_bytes")),
      std::chrono::milliseconds{timeout}};
  calls.validate();
  const auto deadline = standard::deadline_from_ticks(
      standard::integer(payload.at("deadline_ticks")));
  standard::validate_preserved_deadline(deadline, calls.timeout);
  const auto &correlation = control.at("correlation");
  std::optional<std::string_view> correlation_text;
  if (!correlation.is_null()) {
    detail::require_object(correlation, "standard correlation");
    correlation_text = standard::string(correlation.at("attempt_id"));
  }
  budget.value(0);
  standard::MutationProjection projection(budget, false);
  projection.field("parameters", 1);
  standard::project_query_parameters(
      projection, std::type_identity<standard::GetFriendListParameters>{},
      parameters, false);
  projection.envelope(mode, correlation_text);
  // The whole borrowed envelope and prospective retained DTO are bounded
  // before reference decoding or any owning text construction.
  standard::GetFriendListRequest request{
      {parameters.at("installation").get<BotInstallationRef>()},
      {mode, calls,
       correlation_text
           ? std::optional{OperationCorrelation{std::string{*correlation_text}}}
           : std::nullopt},
      values};
  request.deadline_ = deadline;
  standard::validate_query_fields(request);
  return request;
}
using GetGroupInfoTraits = OperationTraits<standard::GetGroupInfoRequest>;

auto GetGroupInfoTraits::action() -> const ActionId & {
  static const ActionId id{"onebot11.standard.get_group_info"};
  return id;
}
auto GetGroupInfoTraits::installation(const request_type &request)
    -> const BotInstallationRef & {
  return request.parameters.group.installation;
}
auto GetGroupInfoTraits::supports_surface(const SurfaceId &surface) -> bool {
  return surface == onebot11::bot::surface;
}
void GetGroupInfoTraits::validate_result(const request_type &request,
                                         const result_type &result) {
  standard::validate_outcome_mode(request.control.execution, result);
  if (const auto *completed =
          std::get_if<standard::Completed<standard::GroupInfo>>(&result.state))
    standard::validate_query_result(request, completed->value);
}

auto GatewayCodec<standard::GetGroupInfoRequest>::encode(
    const standard::GetGroupInfoRequest &request) -> Json {
  standard::RequestBudget budget(request.value_limits);
  standard::query_projection(budget, request, false);
  standard::validate_query_fields(request);
  const auto deadline =
      request.deadline_
          ? *request.deadline_
          : standard::deadline_after(request.control.limits.timeout);
  if (request.deadline_)
    standard::validate_preserved_deadline(deadline,
                                          request.control.limits.timeout);
  const auto ticks = deadline.time_since_epoch().count();
  if (!std::in_range<std::int64_t>(ticks))
    throw std::invalid_argument(
        "standard deadline exceeds wire representation");
  standard::query_projection(budget, request, true);
  const auto &limits = request.control.limits;
  const auto &values = request.value_limits;
  // Native parameter bytes do not bound this internal metadata envelope.
  return {{"parameters",
           {{"group", request.parameters.group},
            {"no_cache", request.parameters.no_cache}}},
          {"control",
           {{"execution", standard::execution_name(request.control.execution)},
            {"limits",
             {{"maximum_parameter_bytes", limits.maximum_parameter_bytes},
              {"maximum_response_bytes", limits.maximum_response_bytes},
              {"timeout_milliseconds", limits.timeout.count()}}},
            {"correlation",
             request.control.correlation
                 ? Json{{"attempt_id", request.control.correlation->attempt_id}}
                 : Json(nullptr)}}},
          {"value_limits",
           {{"maximum_bytes", values.maximum_bytes},
            {"maximum_nodes", values.maximum_nodes},
            {"maximum_depth", values.maximum_depth},
            {"maximum_text_bytes", values.maximum_text_bytes}}},
          {"deadline_ticks", static_cast<std::int64_t>(ticks)}};
}

auto GatewayCodec<standard::GetGroupInfoRequest>::decode(const Json &payload)
    -> standard::GetGroupInfoRequest {
  detail::require_object(payload, "standard get_group_info request");
  const auto values = standard::value_limits(payload.at("value_limits"));
  standard::RequestBudget budget(values);
  budget.inspect(payload,
                 0); // Includes unknown/discarded fields, before copies.
  const auto &parameters = payload.at("parameters");
  const auto &control = payload.at("control");
  detail::require_object(parameters, "standard parameters");
  detail::require_object(control, "standard control");
  const auto mode = standard::execution_mode(control.at("execution"));
  const auto &limits = control.at("limits");
  detail::require_object(limits, "standard call limits");
  const auto timeout = standard::integer(limits.at("timeout_milliseconds"));
  if (!std::in_range<std::chrono::milliseconds::rep>(timeout))
    throw std::invalid_argument("standard timeout exceeds representation");
  standard::CallLimits calls{
      standard::size_value(limits.at("maximum_parameter_bytes")),
      standard::size_value(limits.at("maximum_response_bytes")),
      std::chrono::milliseconds{timeout}};
  calls.validate();
  const auto deadline = standard::deadline_from_ticks(
      standard::integer(payload.at("deadline_ticks")));
  standard::validate_preserved_deadline(deadline, calls.timeout);
  const auto &correlation = control.at("correlation");
  std::optional<std::string_view> correlation_text;
  if (!correlation.is_null()) {
    detail::require_object(correlation, "standard correlation");
    correlation_text = standard::string(correlation.at("attempt_id"));
  }
  budget.value(0);
  standard::MutationProjection projection(budget, false);
  projection.field("parameters", 1);
  standard::project_query_parameters(
      projection, std::type_identity<standard::GetGroupInfoParameters>{},
      parameters, false);
  projection.envelope(mode, correlation_text);
  // The whole borrowed envelope and prospective retained DTO are bounded
  // before reference decoding or any owning text construction.
  standard::GetGroupInfoRequest request{
      {parameters.at("group").get<ConversationRef>(),
       standard::boolean(parameters.at("no_cache"))},
      {mode, calls,
       correlation_text
           ? std::optional{OperationCorrelation{std::string{*correlation_text}}}
           : std::nullopt},
      values};
  request.deadline_ = deadline;
  standard::validate_query_fields(request);
  return request;
}
using GetGroupListTraits = OperationTraits<standard::GetGroupListRequest>;

auto GetGroupListTraits::action() -> const ActionId & {
  static const ActionId id{"onebot11.standard.get_group_list"};
  return id;
}
auto GetGroupListTraits::installation(const request_type &request)
    -> const BotInstallationRef & {
  return request.parameters.installation;
}
auto GetGroupListTraits::supports_surface(const SurfaceId &surface) -> bool {
  return surface == onebot11::bot::surface;
}
void GetGroupListTraits::validate_result(const request_type &request,
                                         const result_type &result) {
  standard::validate_outcome_mode(request.control.execution, result);
  if (const auto *completed =
          std::get_if<standard::Completed<standard::GroupList>>(&result.state))
    standard::validate_query_result(request, completed->value);
}

auto GatewayCodec<standard::GetGroupListRequest>::encode(
    const standard::GetGroupListRequest &request) -> Json {
  standard::RequestBudget budget(request.value_limits);
  standard::query_projection(budget, request, false);
  standard::validate_query_fields(request);
  const auto deadline =
      request.deadline_
          ? *request.deadline_
          : standard::deadline_after(request.control.limits.timeout);
  if (request.deadline_)
    standard::validate_preserved_deadline(deadline,
                                          request.control.limits.timeout);
  const auto ticks = deadline.time_since_epoch().count();
  if (!std::in_range<std::int64_t>(ticks))
    throw std::invalid_argument(
        "standard deadline exceeds wire representation");
  standard::query_projection(budget, request, true);
  const auto &limits = request.control.limits;
  const auto &values = request.value_limits;
  // Native parameter bytes do not bound this internal metadata envelope.
  return {{"parameters", {{"installation", request.parameters.installation}}},
          {"control",
           {{"execution", standard::execution_name(request.control.execution)},
            {"limits",
             {{"maximum_parameter_bytes", limits.maximum_parameter_bytes},
              {"maximum_response_bytes", limits.maximum_response_bytes},
              {"timeout_milliseconds", limits.timeout.count()}}},
            {"correlation",
             request.control.correlation
                 ? Json{{"attempt_id", request.control.correlation->attempt_id}}
                 : Json(nullptr)}}},
          {"value_limits",
           {{"maximum_bytes", values.maximum_bytes},
            {"maximum_nodes", values.maximum_nodes},
            {"maximum_depth", values.maximum_depth},
            {"maximum_text_bytes", values.maximum_text_bytes}}},
          {"deadline_ticks", static_cast<std::int64_t>(ticks)}};
}

auto GatewayCodec<standard::GetGroupListRequest>::decode(const Json &payload)
    -> standard::GetGroupListRequest {
  detail::require_object(payload, "standard get_group_list request");
  const auto values = standard::value_limits(payload.at("value_limits"));
  standard::RequestBudget budget(values);
  budget.inspect(payload,
                 0); // Includes unknown/discarded fields, before copies.
  const auto &parameters = payload.at("parameters");
  const auto &control = payload.at("control");
  detail::require_object(parameters, "standard parameters");
  detail::require_object(control, "standard control");
  const auto mode = standard::execution_mode(control.at("execution"));
  const auto &limits = control.at("limits");
  detail::require_object(limits, "standard call limits");
  const auto timeout = standard::integer(limits.at("timeout_milliseconds"));
  if (!std::in_range<std::chrono::milliseconds::rep>(timeout))
    throw std::invalid_argument("standard timeout exceeds representation");
  standard::CallLimits calls{
      standard::size_value(limits.at("maximum_parameter_bytes")),
      standard::size_value(limits.at("maximum_response_bytes")),
      std::chrono::milliseconds{timeout}};
  calls.validate();
  const auto deadline = standard::deadline_from_ticks(
      standard::integer(payload.at("deadline_ticks")));
  standard::validate_preserved_deadline(deadline, calls.timeout);
  const auto &correlation = control.at("correlation");
  std::optional<std::string_view> correlation_text;
  if (!correlation.is_null()) {
    detail::require_object(correlation, "standard correlation");
    correlation_text = standard::string(correlation.at("attempt_id"));
  }
  budget.value(0);
  standard::MutationProjection projection(budget, false);
  projection.field("parameters", 1);
  standard::project_query_parameters(
      projection, std::type_identity<standard::GetGroupListParameters>{},
      parameters, false);
  projection.envelope(mode, correlation_text);
  // The whole borrowed envelope and prospective retained DTO are bounded
  // before reference decoding or any owning text construction.
  standard::GetGroupListRequest request{
      {parameters.at("installation").get<BotInstallationRef>()},
      {mode, calls,
       correlation_text
           ? std::optional{OperationCorrelation{std::string{*correlation_text}}}
           : std::nullopt},
      values};
  request.deadline_ = deadline;
  standard::validate_query_fields(request);
  return request;
}
using GetGroupMemberInfoTraits =
    OperationTraits<standard::GetGroupMemberInfoRequest>;

auto GetGroupMemberInfoTraits::action() -> const ActionId & {
  static const ActionId id{"onebot11.standard.get_group_member_info"};
  return id;
}
auto GetGroupMemberInfoTraits::installation(const request_type &request)
    -> const BotInstallationRef & {
  return request.parameters.group.installation;
}
auto GetGroupMemberInfoTraits::supports_surface(const SurfaceId &surface)
    -> bool {
  return surface == onebot11::bot::surface;
}
void GetGroupMemberInfoTraits::validate_result(const request_type &request,
                                               const result_type &result) {
  standard::validate_outcome_mode(request.control.execution, result);
  if (const auto *completed =
          std::get_if<standard::Completed<standard::GroupMemberInfo>>(
              &result.state))
    standard::validate_query_result(request, completed->value);
}

auto GatewayCodec<standard::GetGroupMemberInfoRequest>::encode(
    const standard::GetGroupMemberInfoRequest &request) -> Json {
  standard::RequestBudget budget(request.value_limits);
  standard::query_projection(budget, request, false);
  standard::validate_query_fields(request);
  const auto deadline =
      request.deadline_
          ? *request.deadline_
          : standard::deadline_after(request.control.limits.timeout);
  if (request.deadline_)
    standard::validate_preserved_deadline(deadline,
                                          request.control.limits.timeout);
  const auto ticks = deadline.time_since_epoch().count();
  if (!std::in_range<std::int64_t>(ticks))
    throw std::invalid_argument(
        "standard deadline exceeds wire representation");
  standard::query_projection(budget, request, true);
  const auto &limits = request.control.limits;
  const auto &values = request.value_limits;
  // Native parameter bytes do not bound this internal metadata envelope.
  return {{"parameters",
           {{"group", request.parameters.group},
            {"user", request.parameters.user},
            {"no_cache", request.parameters.no_cache}}},
          {"control",
           {{"execution", standard::execution_name(request.control.execution)},
            {"limits",
             {{"maximum_parameter_bytes", limits.maximum_parameter_bytes},
              {"maximum_response_bytes", limits.maximum_response_bytes},
              {"timeout_milliseconds", limits.timeout.count()}}},
            {"correlation",
             request.control.correlation
                 ? Json{{"attempt_id", request.control.correlation->attempt_id}}
                 : Json(nullptr)}}},
          {"value_limits",
           {{"maximum_bytes", values.maximum_bytes},
            {"maximum_nodes", values.maximum_nodes},
            {"maximum_depth", values.maximum_depth},
            {"maximum_text_bytes", values.maximum_text_bytes}}},
          {"deadline_ticks", static_cast<std::int64_t>(ticks)}};
}

auto GatewayCodec<standard::GetGroupMemberInfoRequest>::decode(
    const Json &payload) -> standard::GetGroupMemberInfoRequest {
  detail::require_object(payload, "standard get_group_member_info request");
  const auto values = standard::value_limits(payload.at("value_limits"));
  standard::RequestBudget budget(values);
  budget.inspect(payload,
                 0); // Includes unknown/discarded fields, before copies.
  const auto &parameters = payload.at("parameters");
  const auto &control = payload.at("control");
  detail::require_object(parameters, "standard parameters");
  detail::require_object(control, "standard control");
  const auto mode = standard::execution_mode(control.at("execution"));
  const auto &limits = control.at("limits");
  detail::require_object(limits, "standard call limits");
  const auto timeout = standard::integer(limits.at("timeout_milliseconds"));
  if (!std::in_range<std::chrono::milliseconds::rep>(timeout))
    throw std::invalid_argument("standard timeout exceeds representation");
  standard::CallLimits calls{
      standard::size_value(limits.at("maximum_parameter_bytes")),
      standard::size_value(limits.at("maximum_response_bytes")),
      std::chrono::milliseconds{timeout}};
  calls.validate();
  const auto deadline = standard::deadline_from_ticks(
      standard::integer(payload.at("deadline_ticks")));
  standard::validate_preserved_deadline(deadline, calls.timeout);
  const auto &correlation = control.at("correlation");
  std::optional<std::string_view> correlation_text;
  if (!correlation.is_null()) {
    detail::require_object(correlation, "standard correlation");
    correlation_text = standard::string(correlation.at("attempt_id"));
  }
  budget.value(0);
  standard::MutationProjection projection(budget, false);
  projection.field("parameters", 1);
  standard::project_query_parameters(
      projection, std::type_identity<standard::GetGroupMemberInfoParameters>{},
      parameters, false);
  projection.envelope(mode, correlation_text);
  // The whole borrowed envelope and prospective retained DTO are bounded
  // before reference decoding or any owning text construction.
  standard::GetGroupMemberInfoRequest request{
      {parameters.at("group").get<ConversationRef>(),
       parameters.at("user").get<UserRef>(),
       standard::boolean(parameters.at("no_cache"))},
      {mode, calls,
       correlation_text
           ? std::optional{OperationCorrelation{std::string{*correlation_text}}}
           : std::nullopt},
      values};
  request.deadline_ = deadline;
  standard::validate_query_fields(request);
  return request;
}
using GetGroupMemberListTraits =
    OperationTraits<standard::GetGroupMemberListRequest>;

auto GetGroupMemberListTraits::action() -> const ActionId & {
  static const ActionId id{"onebot11.standard.get_group_member_list"};
  return id;
}
auto GetGroupMemberListTraits::installation(const request_type &request)
    -> const BotInstallationRef & {
  return request.parameters.group.installation;
}
auto GetGroupMemberListTraits::supports_surface(const SurfaceId &surface)
    -> bool {
  return surface == onebot11::bot::surface;
}
void GetGroupMemberListTraits::validate_result(const request_type &request,
                                               const result_type &result) {
  standard::validate_outcome_mode(request.control.execution, result);
  if (const auto *completed =
          std::get_if<standard::Completed<standard::GroupMemberList>>(
              &result.state))
    standard::validate_query_result(request, completed->value);
}

auto GatewayCodec<standard::GetGroupMemberListRequest>::encode(
    const standard::GetGroupMemberListRequest &request) -> Json {
  standard::RequestBudget budget(request.value_limits);
  standard::query_projection(budget, request, false);
  standard::validate_query_fields(request);
  const auto deadline =
      request.deadline_
          ? *request.deadline_
          : standard::deadline_after(request.control.limits.timeout);
  if (request.deadline_)
    standard::validate_preserved_deadline(deadline,
                                          request.control.limits.timeout);
  const auto ticks = deadline.time_since_epoch().count();
  if (!std::in_range<std::int64_t>(ticks))
    throw std::invalid_argument(
        "standard deadline exceeds wire representation");
  standard::query_projection(budget, request, true);
  const auto &limits = request.control.limits;
  const auto &values = request.value_limits;
  // Native parameter bytes do not bound this internal metadata envelope.
  return {{"parameters", {{"group", request.parameters.group}}},
          {"control",
           {{"execution", standard::execution_name(request.control.execution)},
            {"limits",
             {{"maximum_parameter_bytes", limits.maximum_parameter_bytes},
              {"maximum_response_bytes", limits.maximum_response_bytes},
              {"timeout_milliseconds", limits.timeout.count()}}},
            {"correlation",
             request.control.correlation
                 ? Json{{"attempt_id", request.control.correlation->attempt_id}}
                 : Json(nullptr)}}},
          {"value_limits",
           {{"maximum_bytes", values.maximum_bytes},
            {"maximum_nodes", values.maximum_nodes},
            {"maximum_depth", values.maximum_depth},
            {"maximum_text_bytes", values.maximum_text_bytes}}},
          {"deadline_ticks", static_cast<std::int64_t>(ticks)}};
}

auto GatewayCodec<standard::GetGroupMemberListRequest>::decode(
    const Json &payload) -> standard::GetGroupMemberListRequest {
  detail::require_object(payload, "standard get_group_member_list request");
  const auto values = standard::value_limits(payload.at("value_limits"));
  standard::RequestBudget budget(values);
  budget.inspect(payload,
                 0); // Includes unknown/discarded fields, before copies.
  const auto &parameters = payload.at("parameters");
  const auto &control = payload.at("control");
  detail::require_object(parameters, "standard parameters");
  detail::require_object(control, "standard control");
  const auto mode = standard::execution_mode(control.at("execution"));
  const auto &limits = control.at("limits");
  detail::require_object(limits, "standard call limits");
  const auto timeout = standard::integer(limits.at("timeout_milliseconds"));
  if (!std::in_range<std::chrono::milliseconds::rep>(timeout))
    throw std::invalid_argument("standard timeout exceeds representation");
  standard::CallLimits calls{
      standard::size_value(limits.at("maximum_parameter_bytes")),
      standard::size_value(limits.at("maximum_response_bytes")),
      std::chrono::milliseconds{timeout}};
  calls.validate();
  const auto deadline = standard::deadline_from_ticks(
      standard::integer(payload.at("deadline_ticks")));
  standard::validate_preserved_deadline(deadline, calls.timeout);
  const auto &correlation = control.at("correlation");
  std::optional<std::string_view> correlation_text;
  if (!correlation.is_null()) {
    detail::require_object(correlation, "standard correlation");
    correlation_text = standard::string(correlation.at("attempt_id"));
  }
  budget.value(0);
  standard::MutationProjection projection(budget, false);
  projection.field("parameters", 1);
  standard::project_query_parameters(
      projection, std::type_identity<standard::GetGroupMemberListParameters>{},
      parameters, false);
  projection.envelope(mode, correlation_text);
  // The whole borrowed envelope and prospective retained DTO are bounded
  // before reference decoding or any owning text construction.
  standard::GetGroupMemberListRequest request{
      {parameters.at("group").get<ConversationRef>()},
      {mode, calls,
       correlation_text
           ? std::optional{OperationCorrelation{std::string{*correlation_text}}}
           : std::nullopt},
      values};
  request.deadline_ = deadline;
  standard::validate_query_fields(request);
  return request;
}
using GetGroupHonorInfoTraits =
    OperationTraits<standard::GetGroupHonorInfoRequest>;

auto GetGroupHonorInfoTraits::action() -> const ActionId & {
  static const ActionId id{"onebot11.standard.get_group_honor_info"};
  return id;
}
auto GetGroupHonorInfoTraits::installation(const request_type &request)
    -> const BotInstallationRef & {
  return request.parameters.group.installation;
}
auto GetGroupHonorInfoTraits::supports_surface(const SurfaceId &surface)
    -> bool {
  return surface == onebot11::bot::surface;
}
void GetGroupHonorInfoTraits::validate_result(const request_type &request,
                                              const result_type &result) {
  standard::validate_outcome_mode(request.control.execution, result);
  if (const auto *completed =
          std::get_if<standard::Completed<standard::GroupHonorInfo>>(
              &result.state))
    standard::validate_query_result(request, completed->value);
}

auto GatewayCodec<standard::GetGroupHonorInfoRequest>::encode(
    const standard::GetGroupHonorInfoRequest &request) -> Json {
  standard::RequestBudget budget(request.value_limits);
  standard::query_projection(budget, request, false);
  standard::validate_query_fields(request);
  const auto deadline =
      request.deadline_
          ? *request.deadline_
          : standard::deadline_after(request.control.limits.timeout);
  if (request.deadline_)
    standard::validate_preserved_deadline(deadline,
                                          request.control.limits.timeout);
  const auto ticks = deadline.time_since_epoch().count();
  if (!std::in_range<std::int64_t>(ticks))
    throw std::invalid_argument(
        "standard deadline exceeds wire representation");
  standard::query_projection(budget, request, true);
  const auto &limits = request.control.limits;
  const auto &values = request.value_limits;
  // Native parameter bytes do not bound this internal metadata envelope.
  return {{"parameters",
           {{"group", request.parameters.group},
            {"type", standard::query_honor_name(request.parameters.type)}}},
          {"control",
           {{"execution", standard::execution_name(request.control.execution)},
            {"limits",
             {{"maximum_parameter_bytes", limits.maximum_parameter_bytes},
              {"maximum_response_bytes", limits.maximum_response_bytes},
              {"timeout_milliseconds", limits.timeout.count()}}},
            {"correlation",
             request.control.correlation
                 ? Json{{"attempt_id", request.control.correlation->attempt_id}}
                 : Json(nullptr)}}},
          {"value_limits",
           {{"maximum_bytes", values.maximum_bytes},
            {"maximum_nodes", values.maximum_nodes},
            {"maximum_depth", values.maximum_depth},
            {"maximum_text_bytes", values.maximum_text_bytes}}},
          {"deadline_ticks", static_cast<std::int64_t>(ticks)}};
}

auto GatewayCodec<standard::GetGroupHonorInfoRequest>::decode(
    const Json &payload) -> standard::GetGroupHonorInfoRequest {
  detail::require_object(payload, "standard get_group_honor_info request");
  const auto values = standard::value_limits(payload.at("value_limits"));
  standard::RequestBudget budget(values);
  budget.inspect(payload,
                 0); // Includes unknown/discarded fields, before copies.
  const auto &parameters = payload.at("parameters");
  const auto &control = payload.at("control");
  detail::require_object(parameters, "standard parameters");
  detail::require_object(control, "standard control");
  const auto mode = standard::execution_mode(control.at("execution"));
  const auto &limits = control.at("limits");
  detail::require_object(limits, "standard call limits");
  const auto timeout = standard::integer(limits.at("timeout_milliseconds"));
  if (!std::in_range<std::chrono::milliseconds::rep>(timeout))
    throw std::invalid_argument("standard timeout exceeds representation");
  standard::CallLimits calls{
      standard::size_value(limits.at("maximum_parameter_bytes")),
      standard::size_value(limits.at("maximum_response_bytes")),
      std::chrono::milliseconds{timeout}};
  calls.validate();
  const auto deadline = standard::deadline_from_ticks(
      standard::integer(payload.at("deadline_ticks")));
  standard::validate_preserved_deadline(deadline, calls.timeout);
  const auto &correlation = control.at("correlation");
  std::optional<std::string_view> correlation_text;
  if (!correlation.is_null()) {
    detail::require_object(correlation, "standard correlation");
    correlation_text = standard::string(correlation.at("attempt_id"));
  }
  budget.value(0);
  standard::MutationProjection projection(budget, false);
  projection.field("parameters", 1);
  standard::project_query_parameters(
      projection, std::type_identity<standard::GetGroupHonorInfoParameters>{},
      parameters, false);
  projection.envelope(mode, correlation_text);
  // The whole borrowed envelope and prospective retained DTO are bounded
  // before reference decoding or any owning text construction.
  standard::GetGroupHonorInfoRequest request{
      {parameters.at("group").get<ConversationRef>(),
       standard::query_honor_kind(parameters.at("type"))},
      {mode, calls,
       correlation_text
           ? std::optional{OperationCorrelation{std::string{*correlation_text}}}
           : std::nullopt},
      values};
  request.deadline_ = deadline;
  standard::validate_query_fields(request);
  return request;
}
using GetCookiesTraits = OperationTraits<standard::GetCookiesRequest>;
auto GetCookiesTraits::action() -> const ActionId & {
  static const ActionId id{"onebot11.standard.get_cookies"};
  return id;
}
auto GetCookiesTraits::installation(const request_type &request)
    -> const BotInstallationRef & {
  return request.installation;
}
auto GetCookiesTraits::supports_surface(const SurfaceId &surface) -> bool {
  return surface == onebot11::bot::surface;
}
void GetCookiesTraits::validate_result(const request_type &request,
                                       const result_type &result) {
  standard::validate_outcome_mode(request.control.execution, result);
  if (const auto *completed =
          std::get_if<standard::Completed<standard::CookiesData>>(
              &result.state)) {
    standard::RequestBudget budget(request.value_limits);
    standard::project_service_result(budget, completed->value);
  }
}

auto GatewayCodec<standard::GetCookiesRequest>::encode(
    const standard::GetCookiesRequest &request) -> Json {
  try {
    standard::RequestBudget budget(request.value_limits);
    standard::service_projection(budget, request, false);
    standard::service_projection(budget, request, true);
    standard::validate_service_fields(request);
    const auto deadline =
        request.deadline_
            ? *request.deadline_
            : standard::deadline_after(request.control.limits.timeout);
    if (request.deadline_)
      standard::validate_preserved_deadline(deadline,
                                            request.control.limits.timeout);
    const auto ticks = deadline.time_since_epoch().count();
    if (!std::in_range<std::int64_t>(ticks))
      throw std::invalid_argument(
          "standard deadline exceeds wire representation");
    const auto &limits = request.control.limits;
    const auto &values = request.value_limits;
    // Native parameter bytes do not bound this internal metadata envelope.
    return {
        {"installation", request.installation},
        {"parameters", standard::service_parameters_json(request)},
        {"control",
         {{"execution", standard::execution_name(request.control.execution)},
          {"limits",
           {{"maximum_parameter_bytes", limits.maximum_parameter_bytes},
            {"maximum_response_bytes", limits.maximum_response_bytes},
            {"timeout_milliseconds", limits.timeout.count()}}},
          {"correlation",
           request.control.correlation
               ? Json{{"attempt_id", request.control.correlation->attempt_id}}
               : Json(nullptr)}}},
        {"value_limits",
         {{"maximum_bytes", values.maximum_bytes},
          {"maximum_nodes", values.maximum_nodes},
          {"maximum_depth", values.maximum_depth},
          {"maximum_text_bytes", values.maximum_text_bytes}}},
        {"deadline_ticks", static_cast<std::int64_t>(ticks)}};
  } catch (...) {
    throw std::invalid_argument("invalid standard service request envelope");
  }
}

auto GatewayCodec<standard::GetCookiesRequest>::decode(const Json &payload)
    -> standard::GetCookiesRequest {
  try {
    detail::require_object(payload, "standard get_cookies request");
    const auto values = standard::value_limits(payload.at("value_limits"));
    standard::RequestBudget budget(values);
    budget.inspect(payload,
                   0); // Includes unknown/discarded fields, before copies.
    const auto &parameters = payload.at("parameters");
    const auto &control = payload.at("control");
    detail::require_object(parameters, "standard parameters");
    detail::require_object(control, "standard control");
    const auto mode = standard::execution_mode(control.at("execution"));
    const auto &limits = control.at("limits");
    detail::require_object(limits, "standard call limits");
    const auto timeout = standard::integer(limits.at("timeout_milliseconds"));
    if (!std::in_range<std::chrono::milliseconds::rep>(timeout))
      throw std::invalid_argument("standard timeout exceeds representation");
    standard::CallLimits calls{
        standard::size_value(limits.at("maximum_parameter_bytes")),
        standard::size_value(limits.at("maximum_response_bytes")),
        std::chrono::milliseconds{timeout}};
    calls.validate();
    const auto deadline = standard::deadline_from_ticks(
        standard::integer(payload.at("deadline_ticks")));
    standard::validate_preserved_deadline(deadline, calls.timeout);
    const auto &correlation = control.at("correlation");
    std::optional<std::string_view> correlation_text;
    if (!correlation.is_null()) {
      detail::require_object(correlation, "standard correlation");
      correlation_text = standard::string(correlation.at("attempt_id"));
    }
    standard::service_borrowed_projection<standard::GetCookiesRequest>(
        budget, payload, mode, correlation_text);
    // The whole borrowed envelope and prospective retained DTO are bounded
    // before reference decoding or any owning text construction.
    standard::GetCookiesRequest request{
        payload.at("installation").get<BotInstallationRef>(),
        standard::GetCookiesParameters{
            parameters.at("domain").is_null()
                ? std::nullopt
                : std::optional<std::string>{standard::string(
                      parameters.at("domain"))}},
        {mode, calls,
         correlation_text ? std::optional{OperationCorrelation{
                                std::string{*correlation_text}}}
                          : std::nullopt},
        values};
    request.deadline_ = deadline;
    standard::validate_service_fields(request);
    return request;
  } catch (...) {
    throw std::invalid_argument("invalid standard service request envelope");
  }
}
using GetCsrfTokenTraits = OperationTraits<standard::GetCsrfTokenRequest>;
auto GetCsrfTokenTraits::action() -> const ActionId & {
  static const ActionId id{"onebot11.standard.get_csrf_token"};
  return id;
}
auto GetCsrfTokenTraits::installation(const request_type &request)
    -> const BotInstallationRef & {
  return request.installation;
}
auto GetCsrfTokenTraits::supports_surface(const SurfaceId &surface) -> bool {
  return surface == onebot11::bot::surface;
}
void GetCsrfTokenTraits::validate_result(const request_type &request,
                                         const result_type &result) {
  standard::validate_outcome_mode(request.control.execution, result);
  if (const auto *completed =
          std::get_if<standard::Completed<standard::CsrfTokenData>>(
              &result.state)) {
    standard::RequestBudget budget(request.value_limits);
    standard::project_service_result(budget, completed->value);
  }
}

auto GatewayCodec<standard::GetCsrfTokenRequest>::encode(
    const standard::GetCsrfTokenRequest &request) -> Json {
  try {
    standard::RequestBudget budget(request.value_limits);
    standard::service_projection(budget, request, false);
    standard::service_projection(budget, request, true);
    standard::validate_service_fields(request);
    const auto deadline =
        request.deadline_
            ? *request.deadline_
            : standard::deadline_after(request.control.limits.timeout);
    if (request.deadline_)
      standard::validate_preserved_deadline(deadline,
                                            request.control.limits.timeout);
    const auto ticks = deadline.time_since_epoch().count();
    if (!std::in_range<std::int64_t>(ticks))
      throw std::invalid_argument(
          "standard deadline exceeds wire representation");
    const auto &limits = request.control.limits;
    const auto &values = request.value_limits;
    // Native parameter bytes do not bound this internal metadata envelope.
    return {
        {"installation", request.installation},
        {"parameters", standard::service_parameters_json(request)},
        {"control",
         {{"execution", standard::execution_name(request.control.execution)},
          {"limits",
           {{"maximum_parameter_bytes", limits.maximum_parameter_bytes},
            {"maximum_response_bytes", limits.maximum_response_bytes},
            {"timeout_milliseconds", limits.timeout.count()}}},
          {"correlation",
           request.control.correlation
               ? Json{{"attempt_id", request.control.correlation->attempt_id}}
               : Json(nullptr)}}},
        {"value_limits",
         {{"maximum_bytes", values.maximum_bytes},
          {"maximum_nodes", values.maximum_nodes},
          {"maximum_depth", values.maximum_depth},
          {"maximum_text_bytes", values.maximum_text_bytes}}},
        {"deadline_ticks", static_cast<std::int64_t>(ticks)}};
  } catch (...) {
    throw std::invalid_argument("invalid standard service request envelope");
  }
}

auto GatewayCodec<standard::GetCsrfTokenRequest>::decode(const Json &payload)
    -> standard::GetCsrfTokenRequest {
  try {
    detail::require_object(payload, "standard get_csrf_token request");
    const auto values = standard::value_limits(payload.at("value_limits"));
    standard::RequestBudget budget(values);
    budget.inspect(payload,
                   0); // Includes unknown/discarded fields, before copies.
    const auto &parameters = payload.at("parameters");
    const auto &control = payload.at("control");
    detail::require_object(parameters, "standard parameters");
    detail::require_object(control, "standard control");
    const auto mode = standard::execution_mode(control.at("execution"));
    const auto &limits = control.at("limits");
    detail::require_object(limits, "standard call limits");
    const auto timeout = standard::integer(limits.at("timeout_milliseconds"));
    if (!std::in_range<std::chrono::milliseconds::rep>(timeout))
      throw std::invalid_argument("standard timeout exceeds representation");
    standard::CallLimits calls{
        standard::size_value(limits.at("maximum_parameter_bytes")),
        standard::size_value(limits.at("maximum_response_bytes")),
        std::chrono::milliseconds{timeout}};
    calls.validate();
    const auto deadline = standard::deadline_from_ticks(
        standard::integer(payload.at("deadline_ticks")));
    standard::validate_preserved_deadline(deadline, calls.timeout);
    const auto &correlation = control.at("correlation");
    std::optional<std::string_view> correlation_text;
    if (!correlation.is_null()) {
      detail::require_object(correlation, "standard correlation");
      correlation_text = standard::string(correlation.at("attempt_id"));
    }
    standard::service_borrowed_projection<standard::GetCsrfTokenRequest>(
        budget, payload, mode, correlation_text);
    // The whole borrowed envelope and prospective retained DTO are bounded
    // before reference decoding or any owning text construction.
    standard::GetCsrfTokenRequest request{
        payload.at("installation").get<BotInstallationRef>(),
        standard::GetCsrfTokenParameters{},
        {mode, calls,
         correlation_text ? std::optional{OperationCorrelation{
                                std::string{*correlation_text}}}
                          : std::nullopt},
        values};
    request.deadline_ = deadline;
    standard::validate_service_fields(request);
    return request;
  } catch (...) {
    throw std::invalid_argument("invalid standard service request envelope");
  }
}
using GetCredentialsTraits = OperationTraits<standard::GetCredentialsRequest>;
auto GetCredentialsTraits::action() -> const ActionId & {
  static const ActionId id{"onebot11.standard.get_credentials"};
  return id;
}
auto GetCredentialsTraits::installation(const request_type &request)
    -> const BotInstallationRef & {
  return request.installation;
}
auto GetCredentialsTraits::supports_surface(const SurfaceId &surface) -> bool {
  return surface == onebot11::bot::surface;
}
void GetCredentialsTraits::validate_result(const request_type &request,
                                           const result_type &result) {
  standard::validate_outcome_mode(request.control.execution, result);
  if (const auto *completed =
          std::get_if<standard::Completed<standard::CredentialsData>>(
              &result.state)) {
    standard::RequestBudget budget(request.value_limits);
    standard::project_service_result(budget, completed->value);
  }
}

auto GatewayCodec<standard::GetCredentialsRequest>::encode(
    const standard::GetCredentialsRequest &request) -> Json {
  try {
    standard::RequestBudget budget(request.value_limits);
    standard::service_projection(budget, request, false);
    standard::service_projection(budget, request, true);
    standard::validate_service_fields(request);
    const auto deadline =
        request.deadline_
            ? *request.deadline_
            : standard::deadline_after(request.control.limits.timeout);
    if (request.deadline_)
      standard::validate_preserved_deadline(deadline,
                                            request.control.limits.timeout);
    const auto ticks = deadline.time_since_epoch().count();
    if (!std::in_range<std::int64_t>(ticks))
      throw std::invalid_argument(
          "standard deadline exceeds wire representation");
    const auto &limits = request.control.limits;
    const auto &values = request.value_limits;
    // Native parameter bytes do not bound this internal metadata envelope.
    return {
        {"installation", request.installation},
        {"parameters", standard::service_parameters_json(request)},
        {"control",
         {{"execution", standard::execution_name(request.control.execution)},
          {"limits",
           {{"maximum_parameter_bytes", limits.maximum_parameter_bytes},
            {"maximum_response_bytes", limits.maximum_response_bytes},
            {"timeout_milliseconds", limits.timeout.count()}}},
          {"correlation",
           request.control.correlation
               ? Json{{"attempt_id", request.control.correlation->attempt_id}}
               : Json(nullptr)}}},
        {"value_limits",
         {{"maximum_bytes", values.maximum_bytes},
          {"maximum_nodes", values.maximum_nodes},
          {"maximum_depth", values.maximum_depth},
          {"maximum_text_bytes", values.maximum_text_bytes}}},
        {"deadline_ticks", static_cast<std::int64_t>(ticks)}};
  } catch (...) {
    throw std::invalid_argument("invalid standard service request envelope");
  }
}

auto GatewayCodec<standard::GetCredentialsRequest>::decode(const Json &payload)
    -> standard::GetCredentialsRequest {
  try {
    detail::require_object(payload, "standard get_credentials request");
    const auto values = standard::value_limits(payload.at("value_limits"));
    standard::RequestBudget budget(values);
    budget.inspect(payload,
                   0); // Includes unknown/discarded fields, before copies.
    const auto &parameters = payload.at("parameters");
    const auto &control = payload.at("control");
    detail::require_object(parameters, "standard parameters");
    detail::require_object(control, "standard control");
    const auto mode = standard::execution_mode(control.at("execution"));
    const auto &limits = control.at("limits");
    detail::require_object(limits, "standard call limits");
    const auto timeout = standard::integer(limits.at("timeout_milliseconds"));
    if (!std::in_range<std::chrono::milliseconds::rep>(timeout))
      throw std::invalid_argument("standard timeout exceeds representation");
    standard::CallLimits calls{
        standard::size_value(limits.at("maximum_parameter_bytes")),
        standard::size_value(limits.at("maximum_response_bytes")),
        std::chrono::milliseconds{timeout}};
    calls.validate();
    const auto deadline = standard::deadline_from_ticks(
        standard::integer(payload.at("deadline_ticks")));
    standard::validate_preserved_deadline(deadline, calls.timeout);
    const auto &correlation = control.at("correlation");
    std::optional<std::string_view> correlation_text;
    if (!correlation.is_null()) {
      detail::require_object(correlation, "standard correlation");
      correlation_text = standard::string(correlation.at("attempt_id"));
    }
    standard::service_borrowed_projection<standard::GetCredentialsRequest>(
        budget, payload, mode, correlation_text);
    // The whole borrowed envelope and prospective retained DTO are bounded
    // before reference decoding or any owning text construction.
    standard::GetCredentialsRequest request{
        payload.at("installation").get<BotInstallationRef>(),
        standard::GetCredentialsParameters{
            parameters.at("domain").is_null()
                ? std::nullopt
                : std::optional<std::string>{standard::string(
                      parameters.at("domain"))}},
        {mode, calls,
         correlation_text ? std::optional{OperationCorrelation{
                                std::string{*correlation_text}}}
                          : std::nullopt},
        values};
    request.deadline_ = deadline;
    standard::validate_service_fields(request);
    return request;
  } catch (...) {
    throw std::invalid_argument("invalid standard service request envelope");
  }
}
using GetStatusTraits = OperationTraits<standard::GetStatusRequest>;
auto GetStatusTraits::action() -> const ActionId & {
  static const ActionId id{"onebot11.standard.get_status"};
  return id;
}
auto GetStatusTraits::installation(const request_type &request)
    -> const BotInstallationRef & {
  return request.installation;
}
auto GetStatusTraits::supports_surface(const SurfaceId &surface) -> bool {
  return surface == onebot11::bot::surface;
}
void GetStatusTraits::validate_result(const request_type &request,
                                      const result_type &result) {
  standard::validate_outcome_mode(request.control.execution, result);
  if (const auto *completed =
          std::get_if<standard::Completed<standard::StatusData>>(
              &result.state)) {
    standard::RequestBudget budget(request.value_limits);
    standard::project_service_result(budget, completed->value);
  }
}

auto GatewayCodec<standard::GetStatusRequest>::encode(
    const standard::GetStatusRequest &request) -> Json {
  try {
    standard::RequestBudget budget(request.value_limits);
    standard::service_projection(budget, request, false);
    standard::service_projection(budget, request, true);
    standard::validate_service_fields(request);
    const auto deadline =
        request.deadline_
            ? *request.deadline_
            : standard::deadline_after(request.control.limits.timeout);
    if (request.deadline_)
      standard::validate_preserved_deadline(deadline,
                                            request.control.limits.timeout);
    const auto ticks = deadline.time_since_epoch().count();
    if (!std::in_range<std::int64_t>(ticks))
      throw std::invalid_argument(
          "standard deadline exceeds wire representation");
    const auto &limits = request.control.limits;
    const auto &values = request.value_limits;
    // Native parameter bytes do not bound this internal metadata envelope.
    return {
        {"installation", request.installation},
        {"parameters", standard::service_parameters_json(request)},
        {"control",
         {{"execution", standard::execution_name(request.control.execution)},
          {"limits",
           {{"maximum_parameter_bytes", limits.maximum_parameter_bytes},
            {"maximum_response_bytes", limits.maximum_response_bytes},
            {"timeout_milliseconds", limits.timeout.count()}}},
          {"correlation",
           request.control.correlation
               ? Json{{"attempt_id", request.control.correlation->attempt_id}}
               : Json(nullptr)}}},
        {"value_limits",
         {{"maximum_bytes", values.maximum_bytes},
          {"maximum_nodes", values.maximum_nodes},
          {"maximum_depth", values.maximum_depth},
          {"maximum_text_bytes", values.maximum_text_bytes}}},
        {"deadline_ticks", static_cast<std::int64_t>(ticks)}};
  } catch (...) {
    throw std::invalid_argument("invalid standard service request envelope");
  }
}

auto GatewayCodec<standard::GetStatusRequest>::decode(const Json &payload)
    -> standard::GetStatusRequest {
  try {
    detail::require_object(payload, "standard get_status request");
    const auto values = standard::value_limits(payload.at("value_limits"));
    standard::RequestBudget budget(values);
    budget.inspect(payload,
                   0); // Includes unknown/discarded fields, before copies.
    const auto &parameters = payload.at("parameters");
    const auto &control = payload.at("control");
    detail::require_object(parameters, "standard parameters");
    detail::require_object(control, "standard control");
    const auto mode = standard::execution_mode(control.at("execution"));
    const auto &limits = control.at("limits");
    detail::require_object(limits, "standard call limits");
    const auto timeout = standard::integer(limits.at("timeout_milliseconds"));
    if (!std::in_range<std::chrono::milliseconds::rep>(timeout))
      throw std::invalid_argument("standard timeout exceeds representation");
    standard::CallLimits calls{
        standard::size_value(limits.at("maximum_parameter_bytes")),
        standard::size_value(limits.at("maximum_response_bytes")),
        std::chrono::milliseconds{timeout}};
    calls.validate();
    const auto deadline = standard::deadline_from_ticks(
        standard::integer(payload.at("deadline_ticks")));
    standard::validate_preserved_deadline(deadline, calls.timeout);
    const auto &correlation = control.at("correlation");
    std::optional<std::string_view> correlation_text;
    if (!correlation.is_null()) {
      detail::require_object(correlation, "standard correlation");
      correlation_text = standard::string(correlation.at("attempt_id"));
    }
    standard::service_borrowed_projection<standard::GetStatusRequest>(
        budget, payload, mode, correlation_text);
    // The whole borrowed envelope and prospective retained DTO are bounded
    // before reference decoding or any owning text construction.
    standard::GetStatusRequest request{
        payload.at("installation").get<BotInstallationRef>(),
        standard::GetStatusParameters{},
        {mode, calls,
         correlation_text ? std::optional{OperationCorrelation{
                                std::string{*correlation_text}}}
                          : std::nullopt},
        values};
    request.deadline_ = deadline;
    standard::validate_service_fields(request);
    return request;
  } catch (...) {
    throw std::invalid_argument("invalid standard service request envelope");
  }
}
using GetVersionInfoTraits = OperationTraits<standard::GetVersionInfoRequest>;
auto GetVersionInfoTraits::action() -> const ActionId & {
  static const ActionId id{"onebot11.standard.get_version_info"};
  return id;
}
auto GetVersionInfoTraits::installation(const request_type &request)
    -> const BotInstallationRef & {
  return request.installation;
}
auto GetVersionInfoTraits::supports_surface(const SurfaceId &surface) -> bool {
  return surface == onebot11::bot::surface;
}
void GetVersionInfoTraits::validate_result(const request_type &request,
                                           const result_type &result) {
  standard::validate_outcome_mode(request.control.execution, result);
  if (const auto *completed =
          std::get_if<standard::Completed<standard::VersionInfoData>>(
              &result.state)) {
    standard::RequestBudget budget(request.value_limits);
    standard::project_service_result(budget, completed->value);
  }
}

auto GatewayCodec<standard::GetVersionInfoRequest>::encode(
    const standard::GetVersionInfoRequest &request) -> Json {
  try {
    standard::RequestBudget budget(request.value_limits);
    standard::service_projection(budget, request, false);
    standard::service_projection(budget, request, true);
    standard::validate_service_fields(request);
    const auto deadline =
        request.deadline_
            ? *request.deadline_
            : standard::deadline_after(request.control.limits.timeout);
    if (request.deadline_)
      standard::validate_preserved_deadline(deadline,
                                            request.control.limits.timeout);
    const auto ticks = deadline.time_since_epoch().count();
    if (!std::in_range<std::int64_t>(ticks))
      throw std::invalid_argument(
          "standard deadline exceeds wire representation");
    const auto &limits = request.control.limits;
    const auto &values = request.value_limits;
    // Native parameter bytes do not bound this internal metadata envelope.
    return {
        {"installation", request.installation},
        {"parameters", standard::service_parameters_json(request)},
        {"control",
         {{"execution", standard::execution_name(request.control.execution)},
          {"limits",
           {{"maximum_parameter_bytes", limits.maximum_parameter_bytes},
            {"maximum_response_bytes", limits.maximum_response_bytes},
            {"timeout_milliseconds", limits.timeout.count()}}},
          {"correlation",
           request.control.correlation
               ? Json{{"attempt_id", request.control.correlation->attempt_id}}
               : Json(nullptr)}}},
        {"value_limits",
         {{"maximum_bytes", values.maximum_bytes},
          {"maximum_nodes", values.maximum_nodes},
          {"maximum_depth", values.maximum_depth},
          {"maximum_text_bytes", values.maximum_text_bytes}}},
        {"deadline_ticks", static_cast<std::int64_t>(ticks)}};
  } catch (...) {
    throw std::invalid_argument("invalid standard service request envelope");
  }
}

auto GatewayCodec<standard::GetVersionInfoRequest>::decode(const Json &payload)
    -> standard::GetVersionInfoRequest {
  try {
    detail::require_object(payload, "standard get_version_info request");
    const auto values = standard::value_limits(payload.at("value_limits"));
    standard::RequestBudget budget(values);
    budget.inspect(payload,
                   0); // Includes unknown/discarded fields, before copies.
    const auto &parameters = payload.at("parameters");
    const auto &control = payload.at("control");
    detail::require_object(parameters, "standard parameters");
    detail::require_object(control, "standard control");
    const auto mode = standard::execution_mode(control.at("execution"));
    const auto &limits = control.at("limits");
    detail::require_object(limits, "standard call limits");
    const auto timeout = standard::integer(limits.at("timeout_milliseconds"));
    if (!std::in_range<std::chrono::milliseconds::rep>(timeout))
      throw std::invalid_argument("standard timeout exceeds representation");
    standard::CallLimits calls{
        standard::size_value(limits.at("maximum_parameter_bytes")),
        standard::size_value(limits.at("maximum_response_bytes")),
        std::chrono::milliseconds{timeout}};
    calls.validate();
    const auto deadline = standard::deadline_from_ticks(
        standard::integer(payload.at("deadline_ticks")));
    standard::validate_preserved_deadline(deadline, calls.timeout);
    const auto &correlation = control.at("correlation");
    std::optional<std::string_view> correlation_text;
    if (!correlation.is_null()) {
      detail::require_object(correlation, "standard correlation");
      correlation_text = standard::string(correlation.at("attempt_id"));
    }
    standard::service_borrowed_projection<standard::GetVersionInfoRequest>(
        budget, payload, mode, correlation_text);
    // The whole borrowed envelope and prospective retained DTO are bounded
    // before reference decoding or any owning text construction.
    standard::GetVersionInfoRequest request{
        payload.at("installation").get<BotInstallationRef>(),
        standard::GetVersionInfoParameters{},
        {mode, calls,
         correlation_text ? std::optional{OperationCorrelation{
                                std::string{*correlation_text}}}
                          : std::nullopt},
        values};
    request.deadline_ = deadline;
    standard::validate_service_fields(request);
    return request;
  } catch (...) {
    throw std::invalid_argument("invalid standard service request envelope");
  }
}
using CanSendImageTraits = OperationTraits<standard::CanSendImageRequest>;
auto CanSendImageTraits::action() -> const ActionId & {
  static const ActionId id{"onebot11.standard.can_send_image"};
  return id;
}
auto CanSendImageTraits::installation(const request_type &request)
    -> const BotInstallationRef & {
  return request.installation;
}
auto CanSendImageTraits::supports_surface(const SurfaceId &surface) -> bool {
  return surface == onebot11::bot::surface;
}
void CanSendImageTraits::validate_result(const request_type &request,
                                         const result_type &result) {
  standard::validate_outcome_mode(request.control.execution, result);
  if (const auto *completed =
          std::get_if<standard::Completed<standard::CanSendImageData>>(
              &result.state)) {
    standard::RequestBudget budget(request.value_limits);
    standard::project_service_result(budget, completed->value);
  }
}

auto GatewayCodec<standard::CanSendImageRequest>::encode(
    const standard::CanSendImageRequest &request) -> Json {
  try {
    standard::RequestBudget budget(request.value_limits);
    standard::service_projection(budget, request, false);
    standard::service_projection(budget, request, true);
    standard::validate_service_fields(request);
    const auto deadline =
        request.deadline_
            ? *request.deadline_
            : standard::deadline_after(request.control.limits.timeout);
    if (request.deadline_)
      standard::validate_preserved_deadline(deadline,
                                            request.control.limits.timeout);
    const auto ticks = deadline.time_since_epoch().count();
    if (!std::in_range<std::int64_t>(ticks))
      throw std::invalid_argument(
          "standard deadline exceeds wire representation");
    const auto &limits = request.control.limits;
    const auto &values = request.value_limits;
    // Native parameter bytes do not bound this internal metadata envelope.
    return {
        {"installation", request.installation},
        {"parameters", standard::service_parameters_json(request)},
        {"control",
         {{"execution", standard::execution_name(request.control.execution)},
          {"limits",
           {{"maximum_parameter_bytes", limits.maximum_parameter_bytes},
            {"maximum_response_bytes", limits.maximum_response_bytes},
            {"timeout_milliseconds", limits.timeout.count()}}},
          {"correlation",
           request.control.correlation
               ? Json{{"attempt_id", request.control.correlation->attempt_id}}
               : Json(nullptr)}}},
        {"value_limits",
         {{"maximum_bytes", values.maximum_bytes},
          {"maximum_nodes", values.maximum_nodes},
          {"maximum_depth", values.maximum_depth},
          {"maximum_text_bytes", values.maximum_text_bytes}}},
        {"deadline_ticks", static_cast<std::int64_t>(ticks)}};
  } catch (...) {
    throw std::invalid_argument("invalid standard service request envelope");
  }
}

auto GatewayCodec<standard::CanSendImageRequest>::decode(const Json &payload)
    -> standard::CanSendImageRequest {
  try {
    detail::require_object(payload, "standard can_send_image request");
    const auto values = standard::value_limits(payload.at("value_limits"));
    standard::RequestBudget budget(values);
    budget.inspect(payload,
                   0); // Includes unknown/discarded fields, before copies.
    const auto &parameters = payload.at("parameters");
    const auto &control = payload.at("control");
    detail::require_object(parameters, "standard parameters");
    detail::require_object(control, "standard control");
    const auto mode = standard::execution_mode(control.at("execution"));
    const auto &limits = control.at("limits");
    detail::require_object(limits, "standard call limits");
    const auto timeout = standard::integer(limits.at("timeout_milliseconds"));
    if (!std::in_range<std::chrono::milliseconds::rep>(timeout))
      throw std::invalid_argument("standard timeout exceeds representation");
    standard::CallLimits calls{
        standard::size_value(limits.at("maximum_parameter_bytes")),
        standard::size_value(limits.at("maximum_response_bytes")),
        std::chrono::milliseconds{timeout}};
    calls.validate();
    const auto deadline = standard::deadline_from_ticks(
        standard::integer(payload.at("deadline_ticks")));
    standard::validate_preserved_deadline(deadline, calls.timeout);
    const auto &correlation = control.at("correlation");
    std::optional<std::string_view> correlation_text;
    if (!correlation.is_null()) {
      detail::require_object(correlation, "standard correlation");
      correlation_text = standard::string(correlation.at("attempt_id"));
    }
    standard::service_borrowed_projection<standard::CanSendImageRequest>(
        budget, payload, mode, correlation_text);
    // The whole borrowed envelope and prospective retained DTO are bounded
    // before reference decoding or any owning text construction.
    standard::CanSendImageRequest request{
        payload.at("installation").get<BotInstallationRef>(),
        standard::CanSendImageParameters{},
        {mode, calls,
         correlation_text ? std::optional{OperationCorrelation{
                                std::string{*correlation_text}}}
                          : std::nullopt},
        values};
    request.deadline_ = deadline;
    standard::validate_service_fields(request);
    return request;
  } catch (...) {
    throw std::invalid_argument("invalid standard service request envelope");
  }
}
using CanSendRecordTraits = OperationTraits<standard::CanSendRecordRequest>;
auto CanSendRecordTraits::action() -> const ActionId & {
  static const ActionId id{"onebot11.standard.can_send_record"};
  return id;
}
auto CanSendRecordTraits::installation(const request_type &request)
    -> const BotInstallationRef & {
  return request.installation;
}
auto CanSendRecordTraits::supports_surface(const SurfaceId &surface) -> bool {
  return surface == onebot11::bot::surface;
}
void CanSendRecordTraits::validate_result(const request_type &request,
                                          const result_type &result) {
  standard::validate_outcome_mode(request.control.execution, result);
  if (const auto *completed =
          std::get_if<standard::Completed<standard::CanSendRecordData>>(
              &result.state)) {
    standard::RequestBudget budget(request.value_limits);
    standard::project_service_result(budget, completed->value);
  }
}

auto GatewayCodec<standard::CanSendRecordRequest>::encode(
    const standard::CanSendRecordRequest &request) -> Json {
  try {
    standard::RequestBudget budget(request.value_limits);
    standard::service_projection(budget, request, false);
    standard::service_projection(budget, request, true);
    standard::validate_service_fields(request);
    const auto deadline =
        request.deadline_
            ? *request.deadline_
            : standard::deadline_after(request.control.limits.timeout);
    if (request.deadline_)
      standard::validate_preserved_deadline(deadline,
                                            request.control.limits.timeout);
    const auto ticks = deadline.time_since_epoch().count();
    if (!std::in_range<std::int64_t>(ticks))
      throw std::invalid_argument(
          "standard deadline exceeds wire representation");
    const auto &limits = request.control.limits;
    const auto &values = request.value_limits;
    // Native parameter bytes do not bound this internal metadata envelope.
    return {
        {"installation", request.installation},
        {"parameters", standard::service_parameters_json(request)},
        {"control",
         {{"execution", standard::execution_name(request.control.execution)},
          {"limits",
           {{"maximum_parameter_bytes", limits.maximum_parameter_bytes},
            {"maximum_response_bytes", limits.maximum_response_bytes},
            {"timeout_milliseconds", limits.timeout.count()}}},
          {"correlation",
           request.control.correlation
               ? Json{{"attempt_id", request.control.correlation->attempt_id}}
               : Json(nullptr)}}},
        {"value_limits",
         {{"maximum_bytes", values.maximum_bytes},
          {"maximum_nodes", values.maximum_nodes},
          {"maximum_depth", values.maximum_depth},
          {"maximum_text_bytes", values.maximum_text_bytes}}},
        {"deadline_ticks", static_cast<std::int64_t>(ticks)}};
  } catch (...) {
    throw std::invalid_argument("invalid standard service request envelope");
  }
}

auto GatewayCodec<standard::CanSendRecordRequest>::decode(const Json &payload)
    -> standard::CanSendRecordRequest {
  try {
    detail::require_object(payload, "standard can_send_record request");
    const auto values = standard::value_limits(payload.at("value_limits"));
    standard::RequestBudget budget(values);
    budget.inspect(payload,
                   0); // Includes unknown/discarded fields, before copies.
    const auto &parameters = payload.at("parameters");
    const auto &control = payload.at("control");
    detail::require_object(parameters, "standard parameters");
    detail::require_object(control, "standard control");
    const auto mode = standard::execution_mode(control.at("execution"));
    const auto &limits = control.at("limits");
    detail::require_object(limits, "standard call limits");
    const auto timeout = standard::integer(limits.at("timeout_milliseconds"));
    if (!std::in_range<std::chrono::milliseconds::rep>(timeout))
      throw std::invalid_argument("standard timeout exceeds representation");
    standard::CallLimits calls{
        standard::size_value(limits.at("maximum_parameter_bytes")),
        standard::size_value(limits.at("maximum_response_bytes")),
        std::chrono::milliseconds{timeout}};
    calls.validate();
    const auto deadline = standard::deadline_from_ticks(
        standard::integer(payload.at("deadline_ticks")));
    standard::validate_preserved_deadline(deadline, calls.timeout);
    const auto &correlation = control.at("correlation");
    std::optional<std::string_view> correlation_text;
    if (!correlation.is_null()) {
      detail::require_object(correlation, "standard correlation");
      correlation_text = standard::string(correlation.at("attempt_id"));
    }
    standard::service_borrowed_projection<standard::CanSendRecordRequest>(
        budget, payload, mode, correlation_text);
    // The whole borrowed envelope and prospective retained DTO are bounded
    // before reference decoding or any owning text construction.
    standard::CanSendRecordRequest request{
        payload.at("installation").get<BotInstallationRef>(),
        standard::CanSendRecordParameters{},
        {mode, calls,
         correlation_text ? std::optional{OperationCorrelation{
                                std::string{*correlation_text}}}
                          : std::nullopt},
        values};
    request.deadline_ = deadline;
    standard::validate_service_fields(request);
    return request;
  } catch (...) {
    throw std::invalid_argument("invalid standard service request envelope");
  }
}
using SetRestartTraits = OperationTraits<standard::SetRestartRequest>;
auto SetRestartTraits::action() -> const ActionId & {
  static const ActionId id{"onebot11.standard.set_restart"};
  return id;
}
auto SetRestartTraits::installation(const request_type &request)
    -> const BotInstallationRef & {
  return request.installation;
}
auto SetRestartTraits::supports_surface(const SurfaceId &surface) -> bool {
  return surface == onebot11::bot::surface;
}
void SetRestartTraits::validate_result(const request_type &request,
                                       const result_type &result) {
  standard::validate_outcome_mode(request.control.execution, result);
  if (result.evidence.completion ==
      standard::NativeCompletion::ProviderReportedCompleted)
    throw std::invalid_argument("standard restart cannot report completion");
}

auto GatewayCodec<standard::SetRestartRequest>::encode(
    const standard::SetRestartRequest &request) -> Json {
  try {
    standard::RequestBudget budget(request.value_limits);
    standard::service_projection(budget, request, false);
    standard::service_projection(budget, request, true);
    standard::validate_service_fields(request);
    const auto deadline =
        request.deadline_
            ? *request.deadline_
            : standard::deadline_after(request.control.limits.timeout);
    if (request.deadline_)
      standard::validate_preserved_deadline(deadline,
                                            request.control.limits.timeout);
    const auto ticks = deadline.time_since_epoch().count();
    if (!std::in_range<std::int64_t>(ticks))
      throw std::invalid_argument(
          "standard deadline exceeds wire representation");
    const auto &limits = request.control.limits;
    const auto &values = request.value_limits;
    // Native parameter bytes do not bound this internal metadata envelope.
    return {
        {"installation", request.installation},
        {"parameters", standard::service_parameters_json(request)},
        {"control",
         {{"execution", standard::execution_name(request.control.execution)},
          {"limits",
           {{"maximum_parameter_bytes", limits.maximum_parameter_bytes},
            {"maximum_response_bytes", limits.maximum_response_bytes},
            {"timeout_milliseconds", limits.timeout.count()}}},
          {"correlation",
           request.control.correlation
               ? Json{{"attempt_id", request.control.correlation->attempt_id}}
               : Json(nullptr)}}},
        {"value_limits",
         {{"maximum_bytes", values.maximum_bytes},
          {"maximum_nodes", values.maximum_nodes},
          {"maximum_depth", values.maximum_depth},
          {"maximum_text_bytes", values.maximum_text_bytes}}},
        {"deadline_ticks", static_cast<std::int64_t>(ticks)}};
  } catch (...) {
    throw std::invalid_argument("invalid standard service request envelope");
  }
}

auto GatewayCodec<standard::SetRestartRequest>::decode(const Json &payload)
    -> standard::SetRestartRequest {
  try {
    detail::require_object(payload, "standard set_restart request");
    const auto values = standard::value_limits(payload.at("value_limits"));
    standard::RequestBudget budget(values);
    budget.inspect(payload,
                   0); // Includes unknown/discarded fields, before copies.
    const auto &parameters = payload.at("parameters");
    const auto &control = payload.at("control");
    detail::require_object(parameters, "standard parameters");
    detail::require_object(control, "standard control");
    const auto mode = standard::execution_mode(control.at("execution"));
    const auto &limits = control.at("limits");
    detail::require_object(limits, "standard call limits");
    const auto timeout = standard::integer(limits.at("timeout_milliseconds"));
    if (!std::in_range<std::chrono::milliseconds::rep>(timeout))
      throw std::invalid_argument("standard timeout exceeds representation");
    standard::CallLimits calls{
        standard::size_value(limits.at("maximum_parameter_bytes")),
        standard::size_value(limits.at("maximum_response_bytes")),
        std::chrono::milliseconds{timeout}};
    calls.validate();
    const auto deadline = standard::deadline_from_ticks(
        standard::integer(payload.at("deadline_ticks")));
    standard::validate_preserved_deadline(deadline, calls.timeout);
    const auto &correlation = control.at("correlation");
    std::optional<std::string_view> correlation_text;
    if (!correlation.is_null()) {
      detail::require_object(correlation, "standard correlation");
      correlation_text = standard::string(correlation.at("attempt_id"));
    }
    standard::service_borrowed_projection<standard::SetRestartRequest>(
        budget, payload, mode, correlation_text);
    // The whole borrowed envelope and prospective retained DTO are bounded
    // before reference decoding or any owning text construction.
    standard::SetRestartRequest request{
        payload.at("installation").get<BotInstallationRef>(),
        standard::SetRestartParameters{
            standard::service_delay(parameters.at("delay"))},
        {mode, calls,
         correlation_text ? std::optional{OperationCorrelation{
                                std::string{*correlation_text}}}
                          : std::nullopt},
        values};
    request.deadline_ = deadline;
    standard::validate_service_fields(request);
    return request;
  } catch (...) {
    throw std::invalid_argument("invalid standard service request envelope");
  }
}
using CleanCacheTraits = OperationTraits<standard::CleanCacheRequest>;
auto CleanCacheTraits::action() -> const ActionId & {
  static const ActionId id{"onebot11.standard.clean_cache"};
  return id;
}
auto CleanCacheTraits::installation(const request_type &request)
    -> const BotInstallationRef & {
  return request.installation;
}
auto CleanCacheTraits::supports_surface(const SurfaceId &surface) -> bool {
  return surface == onebot11::bot::surface;
}
void CleanCacheTraits::validate_result(const request_type &request,
                                       const result_type &result) {
  standard::validate_outcome_mode(request.control.execution, result);
  if (const auto *completed =
          std::get_if<standard::Completed<standard::ServiceNoData>>(
              &result.state)) {
    standard::RequestBudget budget(request.value_limits);
    standard::project_service_result(budget, completed->value);
  }
}

auto GatewayCodec<standard::CleanCacheRequest>::encode(
    const standard::CleanCacheRequest &request) -> Json {
  try {
    standard::RequestBudget budget(request.value_limits);
    standard::service_projection(budget, request, false);
    standard::service_projection(budget, request, true);
    standard::validate_service_fields(request);
    const auto deadline =
        request.deadline_
            ? *request.deadline_
            : standard::deadline_after(request.control.limits.timeout);
    if (request.deadline_)
      standard::validate_preserved_deadline(deadline,
                                            request.control.limits.timeout);
    const auto ticks = deadline.time_since_epoch().count();
    if (!std::in_range<std::int64_t>(ticks))
      throw std::invalid_argument(
          "standard deadline exceeds wire representation");
    const auto &limits = request.control.limits;
    const auto &values = request.value_limits;
    // Native parameter bytes do not bound this internal metadata envelope.
    return {
        {"installation", request.installation},
        {"parameters", standard::service_parameters_json(request)},
        {"control",
         {{"execution", standard::execution_name(request.control.execution)},
          {"limits",
           {{"maximum_parameter_bytes", limits.maximum_parameter_bytes},
            {"maximum_response_bytes", limits.maximum_response_bytes},
            {"timeout_milliseconds", limits.timeout.count()}}},
          {"correlation",
           request.control.correlation
               ? Json{{"attempt_id", request.control.correlation->attempt_id}}
               : Json(nullptr)}}},
        {"value_limits",
         {{"maximum_bytes", values.maximum_bytes},
          {"maximum_nodes", values.maximum_nodes},
          {"maximum_depth", values.maximum_depth},
          {"maximum_text_bytes", values.maximum_text_bytes}}},
        {"deadline_ticks", static_cast<std::int64_t>(ticks)}};
  } catch (...) {
    throw std::invalid_argument("invalid standard service request envelope");
  }
}

auto GatewayCodec<standard::CleanCacheRequest>::decode(const Json &payload)
    -> standard::CleanCacheRequest {
  try {
    detail::require_object(payload, "standard clean_cache request");
    const auto values = standard::value_limits(payload.at("value_limits"));
    standard::RequestBudget budget(values);
    budget.inspect(payload,
                   0); // Includes unknown/discarded fields, before copies.
    const auto &parameters = payload.at("parameters");
    const auto &control = payload.at("control");
    detail::require_object(parameters, "standard parameters");
    detail::require_object(control, "standard control");
    const auto mode = standard::execution_mode(control.at("execution"));
    const auto &limits = control.at("limits");
    detail::require_object(limits, "standard call limits");
    const auto timeout = standard::integer(limits.at("timeout_milliseconds"));
    if (!std::in_range<std::chrono::milliseconds::rep>(timeout))
      throw std::invalid_argument("standard timeout exceeds representation");
    standard::CallLimits calls{
        standard::size_value(limits.at("maximum_parameter_bytes")),
        standard::size_value(limits.at("maximum_response_bytes")),
        std::chrono::milliseconds{timeout}};
    calls.validate();
    const auto deadline = standard::deadline_from_ticks(
        standard::integer(payload.at("deadline_ticks")));
    standard::validate_preserved_deadline(deadline, calls.timeout);
    const auto &correlation = control.at("correlation");
    std::optional<std::string_view> correlation_text;
    if (!correlation.is_null()) {
      detail::require_object(correlation, "standard correlation");
      correlation_text = standard::string(correlation.at("attempt_id"));
    }
    standard::service_borrowed_projection<standard::CleanCacheRequest>(
        budget, payload, mode, correlation_text);
    // The whole borrowed envelope and prospective retained DTO are bounded
    // before reference decoding or any owning text construction.
    standard::CleanCacheRequest request{
        payload.at("installation").get<BotInstallationRef>(),
        standard::CleanCacheParameters{},
        {mode, calls,
         correlation_text ? std::optional{OperationCorrelation{
                                std::string{*correlation_text}}}
                          : std::nullopt},
        values};
    request.deadline_ = deadline;
    standard::validate_service_fields(request);
    return request;
  } catch (...) {
    throw std::invalid_argument("invalid standard service request envelope");
  }
}
using GetMsgTraits = OperationTraits<standard::GetMsgRequest>;

auto GetMsgTraits::action() -> const ActionId & {
  static const ActionId id{"onebot11.standard.get_msg"};
  return id;
}
auto GetMsgTraits::installation(const request_type &request)
    -> const BotInstallationRef & {
  return request.parameters.message.installation();
}
auto GetMsgTraits::supports_surface(const SurfaceId &surface) -> bool {
  return surface == onebot11::bot::surface;
}
void GetMsgTraits::validate_result(const request_type &request,
                                   const result_type &result) {
  standard::validate_outcome_mode(request.control.execution, result);
  if (const auto *completed =
          std::get_if<standard::Completed<standard::DetachedMessage>>(
              &result.state))
    standard::detail::validate_lookup_result(
        completed->value, request.parameters, request.value_limits);
}

auto GatewayCodec<standard::GetMsgRequest>::encode(
    const standard::GetMsgRequest &request) -> Json {
  try {
    standard::RequestBudget budget(request.value_limits);
    standard::lookup_projection(budget, request, false);
    standard::lookup_validate_fields(request);
    const auto deadline =
        request.deadline_
            ? *request.deadline_
            : standard::deadline_after(request.control.limits.timeout);
    if (request.deadline_)
      standard::validate_preserved_deadline(deadline,
                                            request.control.limits.timeout);
    const auto ticks = deadline.time_since_epoch().count();
    if (!std::in_range<std::int64_t>(ticks))
      throw std::invalid_argument(
          "standard deadline exceeds wire representation");
    standard::lookup_projection(budget, request, true);
    const auto &limits = request.control.limits;
    const auto &values = request.value_limits;
    // Native parameter bytes do not bound this internal metadata envelope.
    return {
        {"parameters",
         {{"installation", request.parameters.message.installation()},
          {"message_id",
           standard::QuerySelectors::export_message(request.parameters.message,
                                                    request.value_limits)}}},
        {"control",
         {{"execution", standard::execution_name(request.control.execution)},
          {"limits",
           {{"maximum_parameter_bytes", limits.maximum_parameter_bytes},
            {"maximum_response_bytes", limits.maximum_response_bytes},
            {"timeout_milliseconds", limits.timeout.count()}}},
          {"correlation",
           request.control.correlation
               ? Json{{"attempt_id", request.control.correlation->attempt_id}}
               : Json(nullptr)}}},
        {"value_limits",
         {{"maximum_bytes", values.maximum_bytes},
          {"maximum_nodes", values.maximum_nodes},
          {"maximum_depth", values.maximum_depth},
          {"maximum_text_bytes", values.maximum_text_bytes}}},
        {"deadline_ticks", static_cast<std::int64_t>(ticks)}};
  } catch (...) {
    throw std::invalid_argument("invalid standard lookup request envelope");
  }
}

auto GatewayCodec<standard::GetMsgRequest>::decode(const Json &payload)
    -> standard::GetMsgRequest {
  try {
    detail::require_object(payload, "standard get_msg request");
    const auto values = standard::value_limits(payload.at("value_limits"));
    standard::RequestBudget budget(values);
    budget.inspect(payload,
                   0); // Includes unknown/discarded fields, before copies.
    const auto &parameters = payload.at("parameters");
    const auto &control = payload.at("control");
    detail::require_object(parameters, "standard parameters");
    detail::require_object(control, "standard control");
    const auto mode = standard::execution_mode(control.at("execution"));
    const auto &limits = control.at("limits");
    detail::require_object(limits, "standard call limits");
    const auto timeout = standard::integer(limits.at("timeout_milliseconds"));
    if (!std::in_range<std::chrono::milliseconds::rep>(timeout))
      throw std::invalid_argument("standard timeout exceeds representation");
    standard::CallLimits calls{
        standard::size_value(limits.at("maximum_parameter_bytes")),
        standard::size_value(limits.at("maximum_response_bytes")),
        std::chrono::milliseconds{timeout}};
    calls.validate();
    const auto deadline = standard::deadline_from_ticks(
        standard::integer(payload.at("deadline_ticks")));
    standard::validate_preserved_deadline(deadline, calls.timeout);
    const auto &correlation = control.at("correlation");
    std::optional<std::string_view> correlation_text;
    if (!correlation.is_null()) {
      detail::require_object(correlation, "standard correlation");
      correlation_text = standard::string(correlation.at("attempt_id"));
    }
    budget.value(0);
    standard::MutationProjection projection(budget, false);
    projection.field("parameters", 1);
    standard::lookup_project_parameters(
        projection, std::type_identity<standard::GetMessageParameters>{},
        parameters);
    projection.envelope(mode, correlation_text);
    // The whole borrowed envelope and prospective retained DTO are bounded
    // before reference decoding or any owning text construction.
    standard::GetMsgRequest request{
        {standard::QuerySelectors::import_message(
            parameters.at("installation").get<BotInstallationRef>(),
            standard::lookup_message_id(parameters.at("message_id")), values)},
        {mode, calls,
         correlation_text ? std::optional{OperationCorrelation{
                                std::string{*correlation_text}}}
                          : std::nullopt},
        values};
    request.deadline_ = deadline;
    standard::lookup_validate_fields(request);
    return request;
  } catch (...) {
    throw std::invalid_argument("invalid standard lookup request envelope");
  }
}
using GetForwardMsgTraits = OperationTraits<standard::GetForwardMsgRequest>;

auto GetForwardMsgTraits::action() -> const ActionId & {
  static const ActionId id{"onebot11.standard.get_forward_msg"};
  return id;
}
auto GetForwardMsgTraits::installation(const request_type &request)
    -> const BotInstallationRef & {
  return request.parameters.forward.installation();
}
auto GetForwardMsgTraits::supports_surface(const SurfaceId &surface) -> bool {
  return surface == onebot11::bot::surface;
}
void GetForwardMsgTraits::validate_result(const request_type &request,
                                          const result_type &result) {
  standard::validate_outcome_mode(request.control.execution, result);
  if (const auto *completed =
          std::get_if<standard::Completed<standard::DetachedForwardMessage>>(
              &result.state))
    standard::detail::validate_lookup_result(
        completed->value, request.parameters, request.value_limits);
}

auto GatewayCodec<standard::GetForwardMsgRequest>::encode(
    const standard::GetForwardMsgRequest &request) -> Json {
  try {
    standard::RequestBudget budget(request.value_limits);
    standard::lookup_projection(budget, request, false);
    standard::lookup_validate_fields(request);
    const auto deadline =
        request.deadline_
            ? *request.deadline_
            : standard::deadline_after(request.control.limits.timeout);
    if (request.deadline_)
      standard::validate_preserved_deadline(deadline,
                                            request.control.limits.timeout);
    const auto ticks = deadline.time_since_epoch().count();
    if (!std::in_range<std::int64_t>(ticks))
      throw std::invalid_argument(
          "standard deadline exceeds wire representation");
    standard::lookup_projection(budget, request, true);
    const auto &limits = request.control.limits;
    const auto &values = request.value_limits;
    // Native parameter bytes do not bound this internal metadata envelope.
    return {
        {"parameters",
         {{"installation", request.parameters.forward.installation()},
          {"id", standard::QuerySelectors::export_forward(
                     request.parameters.forward, request.value_limits)}}},
        {"control",
         {{"execution", standard::execution_name(request.control.execution)},
          {"limits",
           {{"maximum_parameter_bytes", limits.maximum_parameter_bytes},
            {"maximum_response_bytes", limits.maximum_response_bytes},
            {"timeout_milliseconds", limits.timeout.count()}}},
          {"correlation",
           request.control.correlation
               ? Json{{"attempt_id", request.control.correlation->attempt_id}}
               : Json(nullptr)}}},
        {"value_limits",
         {{"maximum_bytes", values.maximum_bytes},
          {"maximum_nodes", values.maximum_nodes},
          {"maximum_depth", values.maximum_depth},
          {"maximum_text_bytes", values.maximum_text_bytes}}},
        {"deadline_ticks", static_cast<std::int64_t>(ticks)}};
  } catch (...) {
    throw std::invalid_argument("invalid standard lookup request envelope");
  }
}

auto GatewayCodec<standard::GetForwardMsgRequest>::decode(const Json &payload)
    -> standard::GetForwardMsgRequest {
  try {
    detail::require_object(payload, "standard get_forward_msg request");
    const auto values = standard::value_limits(payload.at("value_limits"));
    standard::RequestBudget budget(values);
    budget.inspect(payload,
                   0); // Includes unknown/discarded fields, before copies.
    const auto &parameters = payload.at("parameters");
    const auto &control = payload.at("control");
    detail::require_object(parameters, "standard parameters");
    detail::require_object(control, "standard control");
    const auto mode = standard::execution_mode(control.at("execution"));
    const auto &limits = control.at("limits");
    detail::require_object(limits, "standard call limits");
    const auto timeout = standard::integer(limits.at("timeout_milliseconds"));
    if (!std::in_range<std::chrono::milliseconds::rep>(timeout))
      throw std::invalid_argument("standard timeout exceeds representation");
    standard::CallLimits calls{
        standard::size_value(limits.at("maximum_parameter_bytes")),
        standard::size_value(limits.at("maximum_response_bytes")),
        std::chrono::milliseconds{timeout}};
    calls.validate();
    const auto deadline = standard::deadline_from_ticks(
        standard::integer(payload.at("deadline_ticks")));
    standard::validate_preserved_deadline(deadline, calls.timeout);
    const auto &correlation = control.at("correlation");
    std::optional<std::string_view> correlation_text;
    if (!correlation.is_null()) {
      detail::require_object(correlation, "standard correlation");
      correlation_text = standard::string(correlation.at("attempt_id"));
    }
    budget.value(0);
    standard::MutationProjection projection(budget, false);
    projection.field("parameters", 1);
    standard::lookup_project_parameters(
        projection, std::type_identity<standard::GetForwardMessageParameters>{},
        parameters);
    projection.envelope(mode, correlation_text);
    // The whole borrowed envelope and prospective retained DTO are bounded
    // before reference decoding or any owning text construction.
    standard::GetForwardMsgRequest request{
        {standard::QuerySelectors::import_forward(
            parameters.at("installation").get<BotInstallationRef>(),
            standard::string(parameters.at("id")), values)},
        {mode, calls,
         correlation_text ? std::optional{OperationCorrelation{
                                std::string{*correlation_text}}}
                          : std::nullopt},
        values};
    request.deadline_ = deadline;
    standard::lookup_validate_fields(request);
    return request;
  } catch (...) {
    throw std::invalid_argument("invalid standard lookup request envelope");
  }
}
using GetImageTraits = OperationTraits<standard::GetImageRequest>;
auto GetImageTraits::action() -> const ActionId & {
  static const ActionId id{"onebot11.standard.get_image"};
  return id;
}
auto GetImageTraits::installation(const request_type &request)
    -> const BotInstallationRef & {
  return request.parameters.image.installation();
}
auto GetImageTraits::supports_surface(const SurfaceId &surface) -> bool {
  return surface == onebot11::bot::surface;
}
void GetImageTraits::validate_result(const request_type &request,
                                     const result_type &result) {
  standard::detail::validate_media_result(
      standard::detail::media_result_context(request), result);
}
auto GatewayCodec<standard::GetImageRequest>::encode(
    const standard::GetImageRequest &request) -> Json {
  try {
    standard::RequestBudget budget(request.value_limits);
    standard::media_projection(budget, request, false);
    standard::media_projection(budget, request, true);
    standard::media_validate_fields(request);
    const auto deadline =
        request.deadline_
            ? *request.deadline_
            : standard::deadline_after(request.control.limits.timeout);
    if (request.deadline_)
      standard::validate_preserved_deadline(deadline,
                                            request.control.limits.timeout);
    const auto ticks = deadline.time_since_epoch().count();
    if (!std::in_range<std::int64_t>(ticks))
      throw std::invalid_argument(
          "standard deadline exceeds wire representation");
    const auto &limits = request.control.limits;
    const auto &values = request.value_limits;
    // Native parameter bytes do not bound this internal metadata envelope.
    return {
        {"installation", request.parameters.image.installation()},
        {"parameters", standard::media_parameters_json(request)},
        {"control",
         {{"execution", standard::execution_name(request.control.execution)},
          {"limits",
           {{"maximum_parameter_bytes", limits.maximum_parameter_bytes},
            {"maximum_response_bytes", limits.maximum_response_bytes},
            {"timeout_milliseconds", limits.timeout.count()}}},
          {"correlation",
           request.control.correlation
               ? Json{{"attempt_id", request.control.correlation->attempt_id}}
               : Json(nullptr)}}},
        {"value_limits",
         {{"maximum_bytes", values.maximum_bytes},
          {"maximum_nodes", values.maximum_nodes},
          {"maximum_depth", values.maximum_depth},
          {"maximum_text_bytes", values.maximum_text_bytes}}},
        {"deadline_ticks", static_cast<std::int64_t>(ticks)}};
  } catch (...) {
    throw std::invalid_argument("invalid standard media request envelope");
  }
}

auto GatewayCodec<standard::GetImageRequest>::decode(const Json &payload)
    -> standard::GetImageRequest {
  try {
    detail::require_object(payload, "standard get_image request");
    const auto values = standard::value_limits(payload.at("value_limits"));
    standard::RequestBudget budget(values);
    budget.inspect(payload,
                   0); // Includes unknown/discarded fields, before copies.
    const auto &parameters = payload.at("parameters");
    const auto &control = payload.at("control");
    detail::require_object(parameters, "standard parameters");
    detail::require_object(control, "standard control");
    const auto mode = standard::execution_mode(control.at("execution"));
    const auto &limits = control.at("limits");
    detail::require_object(limits, "standard call limits");
    const auto timeout = standard::integer(limits.at("timeout_milliseconds"));
    if (!std::in_range<std::chrono::milliseconds::rep>(timeout))
      throw std::invalid_argument("standard timeout exceeds representation");
    standard::CallLimits calls{
        standard::size_value(limits.at("maximum_parameter_bytes")),
        standard::size_value(limits.at("maximum_response_bytes")),
        std::chrono::milliseconds{timeout}};
    calls.validate();
    const auto deadline = standard::deadline_from_ticks(
        standard::integer(payload.at("deadline_ticks")));
    standard::validate_preserved_deadline(deadline, calls.timeout);
    const auto &correlation = control.at("correlation");
    std::optional<std::string_view> correlation_text;
    if (!correlation.is_null()) {
      detail::require_object(correlation, "standard correlation");
      correlation_text = standard::string(correlation.at("attempt_id"));
    }
    standard::media_borrowed_projection<standard::GetImageRequest>(
        budget, payload, mode, correlation_text);
    // The whole borrowed envelope and prospective retained DTO are bounded
    // before reference decoding or any owning text construction.
    standard::GetImageRequest request{
        standard::GetImageParameters{
            standard::MediaQuerySelectors::import_image(
                payload.at("installation").get<BotInstallationRef>(),
                standard::string(parameters.at("file")), values)},
        {mode, calls,
         correlation_text ? std::optional{OperationCorrelation{
                                std::string{*correlation_text}}}
                          : std::nullopt},
        values};
    request.deadline_ = deadline;
    standard::media_validate_fields(request);
    return request;
  } catch (...) {
    throw std::invalid_argument("invalid standard media request envelope");
  }
}

using GetRecordTraits = OperationTraits<standard::GetRecordRequest>;
auto GetRecordTraits::action() -> const ActionId & {
  static const ActionId id{"onebot11.standard.get_record"};
  return id;
}
auto GetRecordTraits::installation(const request_type &request)
    -> const BotInstallationRef & {
  return request.parameters.record.installation();
}
auto GetRecordTraits::supports_surface(const SurfaceId &surface) -> bool {
  return surface == onebot11::bot::surface;
}
void GetRecordTraits::validate_result(const request_type &request,
                                      const result_type &result) {
  standard::detail::validate_media_result(
      standard::detail::media_result_context(request), result);
}
auto GatewayCodec<standard::GetRecordRequest>::encode(
    const standard::GetRecordRequest &request) -> Json {
  try {
    standard::RequestBudget budget(request.value_limits);
    standard::media_projection(budget, request, false);
    standard::media_projection(budget, request, true);
    standard::media_validate_fields(request);
    const auto deadline =
        request.deadline_
            ? *request.deadline_
            : standard::deadline_after(request.control.limits.timeout);
    if (request.deadline_)
      standard::validate_preserved_deadline(deadline,
                                            request.control.limits.timeout);
    const auto ticks = deadline.time_since_epoch().count();
    if (!std::in_range<std::int64_t>(ticks))
      throw std::invalid_argument(
          "standard deadline exceeds wire representation");
    const auto &limits = request.control.limits;
    const auto &values = request.value_limits;
    // Native parameter bytes do not bound this internal metadata envelope.
    return {
        {"installation", request.parameters.record.installation()},
        {"parameters", standard::media_parameters_json(request)},
        {"control",
         {{"execution", standard::execution_name(request.control.execution)},
          {"limits",
           {{"maximum_parameter_bytes", limits.maximum_parameter_bytes},
            {"maximum_response_bytes", limits.maximum_response_bytes},
            {"timeout_milliseconds", limits.timeout.count()}}},
          {"correlation",
           request.control.correlation
               ? Json{{"attempt_id", request.control.correlation->attempt_id}}
               : Json(nullptr)}}},
        {"value_limits",
         {{"maximum_bytes", values.maximum_bytes},
          {"maximum_nodes", values.maximum_nodes},
          {"maximum_depth", values.maximum_depth},
          {"maximum_text_bytes", values.maximum_text_bytes}}},
        {"deadline_ticks", static_cast<std::int64_t>(ticks)}};
  } catch (...) {
    throw std::invalid_argument("invalid standard media request envelope");
  }
}

auto GatewayCodec<standard::GetRecordRequest>::decode(const Json &payload)
    -> standard::GetRecordRequest {
  try {
    detail::require_object(payload, "standard get_record request");
    const auto values = standard::value_limits(payload.at("value_limits"));
    standard::RequestBudget budget(values);
    budget.inspect(payload,
                   0); // Includes unknown/discarded fields, before copies.
    const auto &parameters = payload.at("parameters");
    const auto &control = payload.at("control");
    detail::require_object(parameters, "standard parameters");
    detail::require_object(control, "standard control");
    const auto mode = standard::execution_mode(control.at("execution"));
    const auto &limits = control.at("limits");
    detail::require_object(limits, "standard call limits");
    const auto timeout = standard::integer(limits.at("timeout_milliseconds"));
    if (!std::in_range<std::chrono::milliseconds::rep>(timeout))
      throw std::invalid_argument("standard timeout exceeds representation");
    standard::CallLimits calls{
        standard::size_value(limits.at("maximum_parameter_bytes")),
        standard::size_value(limits.at("maximum_response_bytes")),
        std::chrono::milliseconds{timeout}};
    calls.validate();
    const auto deadline = standard::deadline_from_ticks(
        standard::integer(payload.at("deadline_ticks")));
    standard::validate_preserved_deadline(deadline, calls.timeout);
    const auto &correlation = control.at("correlation");
    std::optional<std::string_view> correlation_text;
    if (!correlation.is_null()) {
      detail::require_object(correlation, "standard correlation");
      correlation_text = standard::string(correlation.at("attempt_id"));
    }
    standard::media_borrowed_projection<standard::GetRecordRequest>(
        budget, payload, mode, correlation_text);
    // The whole borrowed envelope and prospective retained DTO are bounded
    // before reference decoding or any owning text construction.
    standard::GetRecordRequest request{
        standard::GetRecordParameters{
            standard::MediaQuerySelectors::import_record(
                payload.at("installation").get<BotInstallationRef>(),
                standard::string(parameters.at("file")), values),
            standard::media_format(parameters.at("out_format"))},
        {mode, calls,
         correlation_text ? std::optional{OperationCorrelation{
                                std::string{*correlation_text}}}
                          : std::nullopt},
        values};
    request.deadline_ = deadline;
    standard::media_validate_fields(request);
    return request;
  } catch (...) {
    throw std::invalid_argument("invalid standard media request envelope");
  }
}

} // namespace obcx::bot

namespace obcx::onebot11::bot::standard {
SendPrivateMsgRequest::SendPrivateMsgRequest(
    SendPrivateMsgParameters parameters, CallControl control,
    ValueLimits value_limits)
    : parameters(std::move(parameters)), control(std::move(control)),
      value_limits(value_limits) {}
void SendPrivateMsgRequest::validate() const {
  control.validate();
  detail::send_sdk_validate_envelope(parameters, control, value_limits);
  (void)detail::encode_send_private_msg(parameters, QqNativeReferenceCodec{},
                                        value_limits);
}
SendGroupMsgRequest::SendGroupMsgRequest(SendGroupMsgParameters parameters,
                                         CallControl control,
                                         ValueLimits value_limits)
    : parameters(std::move(parameters)), control(std::move(control)),
      value_limits(value_limits) {}
void SendGroupMsgRequest::validate() const {
  control.validate();
  detail::send_sdk_validate_envelope(parameters, control, value_limits);
  (void)detail::encode_send_group_msg(parameters, QqNativeReferenceCodec{},
                                      value_limits);
}
SendMsgRequest::SendMsgRequest(SendMsgParameters parameters,
                               CallControl control, ValueLimits value_limits)
    : parameters(std::move(parameters)), control(std::move(control)),
      value_limits(value_limits) {}
void SendMsgRequest::validate() const {
  control.validate();
  detail::send_sdk_validate_envelope(parameters, control, value_limits);
  (void)detail::encode_send_msg(parameters, QqNativeReferenceCodec{},
                                value_limits);
}
} // namespace obcx::onebot11::bot::standard

namespace obcx::bot {
auto OperationTraits<standard::SendPrivateMsgRequest>::action()
    -> const ActionId & {
  static const ActionId id{"onebot11.standard.send_private_msg"};
  return id;
}
auto OperationTraits<standard::SendPrivateMsgRequest>::installation(
    const request_type &request) -> const BotInstallationRef & {
  return request.parameters.target.installation;
}
auto OperationTraits<standard::SendPrivateMsgRequest>::supports_surface(
    const SurfaceId &surface) -> bool {
  return surface == onebot11::bot::surface;
}
void OperationTraits<standard::SendPrivateMsgRequest>::validate_result(
    const request_type &request, const result_type &result) {
  standard::validate_outcome_mode(request.control.execution, result);
  if (const auto *completed =
          std::get_if<standard::Completed<standard::SendMessageData>>(
              &result.state))
    standard::detail::validate_send_result(
        completed->value, request.parameters.target,
        onebot11::bot::QqNativeReferenceCodec{}, request.value_limits);
}
auto GatewayCodec<standard::SendPrivateMsgRequest>::encode(
    const standard::SendPrivateMsgRequest &request) -> Json {
  request.validate();
  const auto deadline =
      request.deadline_
          ? *request.deadline_
          : standard::deadline_after(request.control.limits.timeout);
  if (request.deadline_)
    standard::validate_preserved_deadline(deadline,
                                          request.control.limits.timeout);
  const auto ticks = deadline.time_since_epoch().count();
  if (!std::in_range<std::int64_t>(ticks))
    throw std::invalid_argument(
        "standard send deadline exceeds representation");
  const auto &limits = request.control.limits;
  const auto &values = request.value_limits;
  return {{"parameters",
           standard::detail::send_sdk_encode(request.parameters, values)},
          {"control",
           {{"execution", standard::execution_name(request.control.execution)},
            {"limits",
             {{"maximum_parameter_bytes", limits.maximum_parameter_bytes},
              {"maximum_response_bytes", limits.maximum_response_bytes},
              {"timeout_milliseconds", limits.timeout.count()}}},
            {"correlation",
             request.control.correlation
                 ? Json{{"attempt_id", request.control.correlation->attempt_id}}
                 : Json(nullptr)}}},
          {"value_limits",
           {{"maximum_bytes", values.maximum_bytes},
            {"maximum_nodes", values.maximum_nodes},
            {"maximum_depth", values.maximum_depth},
            {"maximum_text_bytes", values.maximum_text_bytes}}},
          {"deadline_ticks", static_cast<std::int64_t>(ticks)}};
}
auto GatewayCodec<standard::SendPrivateMsgRequest>::decode(const Json &payload)
    -> standard::SendPrivateMsgRequest {
  detail::require_only_keys(
      payload, "standard send request",
      {"parameters", "control", "value_limits", "deadline_ticks"});
  const auto values = standard::value_limits(payload.at("value_limits"));
  standard::RequestBudget budget(values);
  // Borrowed wire and retained projection bounded before any owning decode.
  budget.inspect(payload, 0);
  budget.inspect(payload, 0);
  const auto &control = payload.at("control");
  detail::require_only_keys(control, "standard send control",
                            {"execution", "limits", "correlation"});
  const auto mode = standard::execution_mode(control.at("execution"));
  const auto &limits = control.at("limits");
  detail::require_only_keys(limits, "standard send limits",
                            {"maximum_parameter_bytes",
                             "maximum_response_bytes", "timeout_milliseconds"});
  const auto timeout = standard::integer(limits.at("timeout_milliseconds"));
  if (!std::in_range<std::chrono::milliseconds::rep>(timeout))
    throw std::invalid_argument("standard send timeout exceeds representation");
  standard::CallLimits calls{
      standard::size_value(limits.at("maximum_parameter_bytes")),
      standard::size_value(limits.at("maximum_response_bytes")),
      std::chrono::milliseconds{timeout}};
  calls.validate();
  const auto deadline = standard::deadline_from_ticks(
      standard::integer(payload.at("deadline_ticks")));
  standard::validate_preserved_deadline(deadline, calls.timeout);
  const auto &correlation = control.at("correlation");
  std::optional<OperationCorrelation> owned_correlation;
  if (!correlation.is_null()) {
    detail::require_only_keys(correlation, "standard send correlation",
                              {"attempt_id"});
    owned_correlation.emplace(
        OperationCorrelation{standard::string(correlation.at("attempt_id"))});
  }
  standard::SendPrivateMsgRequest request{
      standard::detail::send_sdk_decode_send_private_msg(
          payload.at("parameters"), values),
      {mode, calls, std::move(owned_correlation)},
      values};
  request.deadline_ = deadline;
  request.validate();
  return request;
}
auto OperationTraits<standard::SendGroupMsgRequest>::action()
    -> const ActionId & {
  static const ActionId id{"onebot11.standard.send_group_msg"};
  return id;
}
auto OperationTraits<standard::SendGroupMsgRequest>::installation(
    const request_type &request) -> const BotInstallationRef & {
  return request.parameters.target.installation;
}
auto OperationTraits<standard::SendGroupMsgRequest>::supports_surface(
    const SurfaceId &surface) -> bool {
  return surface == onebot11::bot::surface;
}
void OperationTraits<standard::SendGroupMsgRequest>::validate_result(
    const request_type &request, const result_type &result) {
  standard::validate_outcome_mode(request.control.execution, result);
  if (const auto *completed =
          std::get_if<standard::Completed<standard::SendMessageData>>(
              &result.state))
    standard::detail::validate_send_result(
        completed->value, request.parameters.target,
        onebot11::bot::QqNativeReferenceCodec{}, request.value_limits);
}
auto GatewayCodec<standard::SendGroupMsgRequest>::encode(
    const standard::SendGroupMsgRequest &request) -> Json {
  request.validate();
  const auto deadline =
      request.deadline_
          ? *request.deadline_
          : standard::deadline_after(request.control.limits.timeout);
  if (request.deadline_)
    standard::validate_preserved_deadline(deadline,
                                          request.control.limits.timeout);
  const auto ticks = deadline.time_since_epoch().count();
  if (!std::in_range<std::int64_t>(ticks))
    throw std::invalid_argument(
        "standard send deadline exceeds representation");
  const auto &limits = request.control.limits;
  const auto &values = request.value_limits;
  return {{"parameters",
           standard::detail::send_sdk_encode(request.parameters, values)},
          {"control",
           {{"execution", standard::execution_name(request.control.execution)},
            {"limits",
             {{"maximum_parameter_bytes", limits.maximum_parameter_bytes},
              {"maximum_response_bytes", limits.maximum_response_bytes},
              {"timeout_milliseconds", limits.timeout.count()}}},
            {"correlation",
             request.control.correlation
                 ? Json{{"attempt_id", request.control.correlation->attempt_id}}
                 : Json(nullptr)}}},
          {"value_limits",
           {{"maximum_bytes", values.maximum_bytes},
            {"maximum_nodes", values.maximum_nodes},
            {"maximum_depth", values.maximum_depth},
            {"maximum_text_bytes", values.maximum_text_bytes}}},
          {"deadline_ticks", static_cast<std::int64_t>(ticks)}};
}
auto GatewayCodec<standard::SendGroupMsgRequest>::decode(const Json &payload)
    -> standard::SendGroupMsgRequest {
  detail::require_only_keys(
      payload, "standard send request",
      {"parameters", "control", "value_limits", "deadline_ticks"});
  const auto values = standard::value_limits(payload.at("value_limits"));
  standard::RequestBudget budget(values);
  // Borrowed wire and retained projection bounded before any owning decode.
  budget.inspect(payload, 0);
  budget.inspect(payload, 0);
  const auto &control = payload.at("control");
  detail::require_only_keys(control, "standard send control",
                            {"execution", "limits", "correlation"});
  const auto mode = standard::execution_mode(control.at("execution"));
  const auto &limits = control.at("limits");
  detail::require_only_keys(limits, "standard send limits",
                            {"maximum_parameter_bytes",
                             "maximum_response_bytes", "timeout_milliseconds"});
  const auto timeout = standard::integer(limits.at("timeout_milliseconds"));
  if (!std::in_range<std::chrono::milliseconds::rep>(timeout))
    throw std::invalid_argument("standard send timeout exceeds representation");
  standard::CallLimits calls{
      standard::size_value(limits.at("maximum_parameter_bytes")),
      standard::size_value(limits.at("maximum_response_bytes")),
      std::chrono::milliseconds{timeout}};
  calls.validate();
  const auto deadline = standard::deadline_from_ticks(
      standard::integer(payload.at("deadline_ticks")));
  standard::validate_preserved_deadline(deadline, calls.timeout);
  const auto &correlation = control.at("correlation");
  std::optional<OperationCorrelation> owned_correlation;
  if (!correlation.is_null()) {
    detail::require_only_keys(correlation, "standard send correlation",
                              {"attempt_id"});
    owned_correlation.emplace(
        OperationCorrelation{standard::string(correlation.at("attempt_id"))});
  }
  standard::SendGroupMsgRequest request{
      standard::detail::send_sdk_decode_send_group_msg(payload.at("parameters"),
                                                       values),
      {mode, calls, std::move(owned_correlation)},
      values};
  request.deadline_ = deadline;
  request.validate();
  return request;
}
auto OperationTraits<standard::SendMsgRequest>::action() -> const ActionId & {
  static const ActionId id{"onebot11.standard.send_msg"};
  return id;
}
auto OperationTraits<standard::SendMsgRequest>::installation(
    const request_type &request) -> const BotInstallationRef & {
  return request.parameters.target.installation;
}
auto OperationTraits<standard::SendMsgRequest>::supports_surface(
    const SurfaceId &surface) -> bool {
  return surface == onebot11::bot::surface;
}
void OperationTraits<standard::SendMsgRequest>::validate_result(
    const request_type &request, const result_type &result) {
  standard::validate_outcome_mode(request.control.execution, result);
  if (const auto *completed =
          std::get_if<standard::Completed<standard::SendMessageData>>(
              &result.state))
    standard::detail::validate_send_result(
        completed->value, request.parameters.target,
        onebot11::bot::QqNativeReferenceCodec{}, request.value_limits);
}
auto GatewayCodec<standard::SendMsgRequest>::encode(
    const standard::SendMsgRequest &request) -> Json {
  request.validate();
  const auto deadline =
      request.deadline_
          ? *request.deadline_
          : standard::deadline_after(request.control.limits.timeout);
  if (request.deadline_)
    standard::validate_preserved_deadline(deadline,
                                          request.control.limits.timeout);
  const auto ticks = deadline.time_since_epoch().count();
  if (!std::in_range<std::int64_t>(ticks))
    throw std::invalid_argument(
        "standard send deadline exceeds representation");
  const auto &limits = request.control.limits;
  const auto &values = request.value_limits;
  return {{"parameters",
           standard::detail::send_sdk_encode(request.parameters, values)},
          {"control",
           {{"execution", standard::execution_name(request.control.execution)},
            {"limits",
             {{"maximum_parameter_bytes", limits.maximum_parameter_bytes},
              {"maximum_response_bytes", limits.maximum_response_bytes},
              {"timeout_milliseconds", limits.timeout.count()}}},
            {"correlation",
             request.control.correlation
                 ? Json{{"attempt_id", request.control.correlation->attempt_id}}
                 : Json(nullptr)}}},
          {"value_limits",
           {{"maximum_bytes", values.maximum_bytes},
            {"maximum_nodes", values.maximum_nodes},
            {"maximum_depth", values.maximum_depth},
            {"maximum_text_bytes", values.maximum_text_bytes}}},
          {"deadline_ticks", static_cast<std::int64_t>(ticks)}};
}
auto GatewayCodec<standard::SendMsgRequest>::decode(const Json &payload)
    -> standard::SendMsgRequest {
  detail::require_only_keys(
      payload, "standard send request",
      {"parameters", "control", "value_limits", "deadline_ticks"});
  const auto values = standard::value_limits(payload.at("value_limits"));
  standard::RequestBudget budget(values);
  // Borrowed wire and retained projection bounded before any owning decode.
  budget.inspect(payload, 0);
  budget.inspect(payload, 0);
  const auto &control = payload.at("control");
  detail::require_only_keys(control, "standard send control",
                            {"execution", "limits", "correlation"});
  const auto mode = standard::execution_mode(control.at("execution"));
  const auto &limits = control.at("limits");
  detail::require_only_keys(limits, "standard send limits",
                            {"maximum_parameter_bytes",
                             "maximum_response_bytes", "timeout_milliseconds"});
  const auto timeout = standard::integer(limits.at("timeout_milliseconds"));
  if (!std::in_range<std::chrono::milliseconds::rep>(timeout))
    throw std::invalid_argument("standard send timeout exceeds representation");
  standard::CallLimits calls{
      standard::size_value(limits.at("maximum_parameter_bytes")),
      standard::size_value(limits.at("maximum_response_bytes")),
      std::chrono::milliseconds{timeout}};
  calls.validate();
  const auto deadline = standard::deadline_from_ticks(
      standard::integer(payload.at("deadline_ticks")));
  standard::validate_preserved_deadline(deadline, calls.timeout);
  const auto &correlation = control.at("correlation");
  std::optional<OperationCorrelation> owned_correlation;
  if (!correlation.is_null()) {
    detail::require_only_keys(correlation, "standard send correlation",
                              {"attempt_id"});
    owned_correlation.emplace(
        OperationCorrelation{standard::string(correlation.at("attempt_id"))});
  }
  standard::SendMsgRequest request{standard::detail::send_sdk_decode_send_msg(
                                       payload.at("parameters"), values),
                                   {mode, calls, std::move(owned_correlation)},
                                   values};
  request.deadline_ = deadline;
  request.validate();
  return request;
}
} // namespace obcx::bot

namespace obcx::onebot11::bot::standard {
SetGroupAnonymousBanRequest::SetGroupAnonymousBanRequest(
    SetGroupAnonymousBanParameters p, CallControl c, ValueLimits v)
    : parameters(std::move(p)), control(std::move(c)), value_limits(v),
      deadline_(deadline_after(control.limits.timeout)) {}
void SetGroupAnonymousBanRequest::validate() const {
  control.validate();
  value_limits.validate();
  if (!deadline_)
    throw std::invalid_argument("event action deadline missing");
  validate_preserved_deadline(*deadline_, control.limits.timeout);
  detail::validate_event_action(*this);
}
SetFriendAddRequestRequest::SetFriendAddRequestRequest(
    SetFriendAddRequestParameters p, CallControl c, ValueLimits v)
    : parameters(std::move(p)), control(std::move(c)), value_limits(v),
      deadline_(deadline_after(control.limits.timeout)) {}
void SetFriendAddRequestRequest::validate() const {
  control.validate();
  value_limits.validate();
  if (!deadline_)
    throw std::invalid_argument("event action deadline missing");
  validate_preserved_deadline(*deadline_, control.limits.timeout);
  detail::validate_event_action(*this);
}
SetGroupAddRequestRequest::SetGroupAddRequestRequest(
    SetGroupAddRequestParameters p, CallControl c, ValueLimits v)
    : parameters(std::move(p)), control(std::move(c)), value_limits(v),
      deadline_(deadline_after(control.limits.timeout)) {}
void SetGroupAddRequestRequest::validate() const {
  control.validate();
  value_limits.validate();
  if (!deadline_)
    throw std::invalid_argument("event action deadline missing");
  validate_preserved_deadline(*deadline_, control.limits.timeout);
  detail::validate_event_action(*this);
}
HandleQuickOperationRequest::HandleQuickOperationRequest(
    HandleQuickOperationParameters p, CallControl c, ValueLimits v)
    : parameters(std::move(p)), control(std::move(c)), value_limits(v),
      deadline_(deadline_after(control.limits.timeout)) {}
void HandleQuickOperationRequest::validate() const {
  control.validate();
  value_limits.validate();
  if (!deadline_)
    throw std::invalid_argument("event action deadline missing");
  validate_preserved_deadline(*deadline_, control.limits.timeout);
  detail::validate_event_action(*this);
}
} // namespace obcx::onebot11::bot::standard
namespace obcx::bot {
using EventSetGroupAnonymousBanTraits =
    OperationTraits<onebot11::bot::standard::SetGroupAnonymousBanRequest>;
auto EventSetGroupAnonymousBanTraits::action() -> const ActionId & {
  static const ActionId id =
      onebot11::bot::standard::detail::standard_action_id(
          onebot11::bot::standard::detail::NativeAction::SetGroupAnonymousBan);
  return id;
}
auto EventSetGroupAnonymousBanTraits::installation(const request_type &r)
    -> const BotInstallationRef & {
  return r.parameters.source.installation();
}
auto EventSetGroupAnonymousBanTraits::supports_surface(const SurfaceId &s)
    -> bool {
  return s == onebot11::bot::surface;
}
void EventSetGroupAnonymousBanTraits::validate_result(
    const request_type &r, const result_type &result) {
  standard::validate_outcome_mode(r.control.execution, result);
}
using EventSetFriendAddRequestTraits =
    OperationTraits<onebot11::bot::standard::SetFriendAddRequestRequest>;
auto EventSetFriendAddRequestTraits::action() -> const ActionId & {
  static const ActionId id =
      onebot11::bot::standard::detail::standard_action_id(
          onebot11::bot::standard::detail::NativeAction::SetFriendAddRequest);
  return id;
}
auto EventSetFriendAddRequestTraits::installation(const request_type &r)
    -> const BotInstallationRef & {
  return r.parameters.source.installation();
}
auto EventSetFriendAddRequestTraits::supports_surface(const SurfaceId &s)
    -> bool {
  return s == onebot11::bot::surface;
}
void EventSetFriendAddRequestTraits::validate_result(
    const request_type &r, const result_type &result) {
  standard::validate_outcome_mode(r.control.execution, result);
}
using EventSetGroupAddRequestTraits =
    OperationTraits<onebot11::bot::standard::SetGroupAddRequestRequest>;
auto EventSetGroupAddRequestTraits::action() -> const ActionId & {
  static const ActionId id =
      onebot11::bot::standard::detail::standard_action_id(
          onebot11::bot::standard::detail::NativeAction::SetGroupAddRequest);
  return id;
}
auto EventSetGroupAddRequestTraits::installation(const request_type &r)
    -> const BotInstallationRef & {
  return r.parameters.source.installation();
}
auto EventSetGroupAddRequestTraits::supports_surface(const SurfaceId &s)
    -> bool {
  return s == onebot11::bot::surface;
}
void EventSetGroupAddRequestTraits::validate_result(const request_type &r,
                                                    const result_type &result) {
  standard::validate_outcome_mode(r.control.execution, result);
}
using EventHandleQuickOperationTraits =
    OperationTraits<onebot11::bot::standard::HandleQuickOperationRequest>;
auto EventHandleQuickOperationTraits::action() -> const ActionId & {
  static const ActionId id =
      onebot11::bot::standard::detail::standard_action_id(
          onebot11::bot::standard::detail::NativeAction::HandleQuickOperation);
  return id;
}
auto EventHandleQuickOperationTraits::installation(const request_type &r)
    -> const BotInstallationRef & {
  return r.parameters.source.installation();
}
auto EventHandleQuickOperationTraits::supports_surface(const SurfaceId &s)
    -> bool {
  return s == onebot11::bot::surface;
}
void EventHandleQuickOperationTraits::validate_result(
    const request_type &r, const result_type &result) {
  standard::validate_outcome_mode(r.control.execution, result);
}
} // namespace obcx::bot
