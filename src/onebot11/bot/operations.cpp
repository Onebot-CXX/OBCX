#include "core/bot/operation_handler.hpp"
#include "core/bot/operation_response_helpers.hpp"
#include "onebot11/bot/capability_ids.hpp"
#include "onebot11/bot/diagnostics.hpp"
#include "onebot11/bot/operation_component.hpp"
#include "onebot11/bot/operation_definitions.hpp"
#include "onebot11/bot/response_parser.hpp"
#include "onebot11/bot/transport.hpp"
#include "received_media.hpp"
#include "standard_event_action_codec.hpp"
#include "standard_login_codec.hpp"
#include "standard_login_coordinator.hpp"
#include "standard_lookup_codec.hpp"
#include "standard_media_query_codec.hpp"
#include "standard_mutation_codec.hpp"
#include "standard_operation_binding.hpp"
#include "standard_query_codec.hpp"
#include "standard_send_codec.hpp"
#include "standard_service_codec.hpp"
#include "standard_transport_access.hpp"
#include "standard_wire_codec.hpp"

#include "network/detail/media_fetch.hpp"
#include "network/image_fetch.hpp"
#include "onebot11/adapter/message_converter.hpp"
#include "onebot11/adapter/native_fields.hpp"
#include "onebot11/bot/domain_binding.hpp"
#include <atomic>
#include <boost/asio/cancel_at.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/experimental/concurrent_channel.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/system/system_error.hpp>
#include <charconv>
#include <exception>
#include <map>
#include <mutex>
#include <system_error>
#include <type_traits>
#include <utility>

namespace obcx::core {
namespace {
using adapter::onebot11::numeric_identity;
using obcx::onebot11::bot::parse_onebot11_operation_response;
using operation_detail::malformed_read;
using operation_detail::malformed_side_effect;
using operation_detail::optional_string;
using operation_detail::parsed_value;
using operation_detail::provider_failure;

namespace standard = obcx::onebot11::bot::standard;
namespace standard_detail = standard::detail;

// A failed media read is not a malformed send request. Keep its typed error
// through preparation without leaking native transport exception text.
class SendPreparationFailure final : public std::exception {
public:
  explicit SendPreparationFailure(bot::BotOperationError error)
      : error_(std::move(error)) {}

  auto what() const noexcept -> const char * override {
    return "QQ media preparation failed";
  }
  auto error() const -> const bot::BotOperationError & { return error_; }

private:
  bot::BotOperationError error_;
};

auto send_preparation_error(std::exception_ptr exception, std::string message)
    -> bot::BotOperationError {
  using Code = bot::BotOperationErrorCode;
  bot::BotOperationError result{
      .code = Code::InvalidRequest,
      .message = std::move(message),
      .retryable = false,
      .submission_safety = bot::SubmissionSafety::DefinitelyNotSubmitted};
  try {
    std::rethrow_exception(exception);
  } catch (const SendPreparationFailure &failure) {
    result = failure.error();
    // This describes the MESSAGE invocation, not a metadata/download request.
    // Preserve classification/provider diagnostics without turning a failed
    // read into new whole-send retry permission.
    result.submission_safety = bot::SubmissionSafety::DefinitelyNotSubmitted;
    result.retryable = false;
  } catch (const network::ImageFetchError &failure) {
    switch (failure.failure()) {
    case network::ImageFetchFailure::InvalidUrl:
    case network::ImageFetchFailure::InvalidLimits:
      break;
    case network::ImageFetchFailure::OverLimit:
      result.code = Code::MediaTooLarge;
      break;
    case network::ImageFetchFailure::Timeout:
      result.code = Code::Cancelled;
      break;
    case network::ImageFetchFailure::Transport:
      result.code = Code::TransportFailure;
      break;
    }
  } catch (const boost::system::system_error &failure) {
    result.code = failure.code() == boost::asio::error::operation_aborted
                      ? Code::Cancelled
                      : Code::TransportFailure;
  } catch (const std::system_error &failure) {
    result.code = failure.code() == std::errc::operation_canceled
                      ? Code::Cancelled
                      : Code::TransportFailure;
  } catch (...) {
    // Unclassified validation/preparation failures retain the previous error.
  }
  return result;
}

struct PreparedSendLike {
  std::shared_ptr<OneBot11TransportCapability> transport_owner;
  std::shared_ptr<SelfIdentityCache> identity_owner;
  std::shared_ptr<standard_detail::StandardCallState> call;
  const standard::ExecutionMode mode;
  const standard::ValueLimits values;
  const std::uint64_t echo;
  const bot::BotInstallationRef installation;
  const bot::ActionId action;
  standard_detail::CapturedStandardSend captured;
};

auto ready_send_like(StructuredOperationCompletion completion)
    -> boost::asio::awaitable<StructuredOperationCompletion> {
  co_return std::move(completion);
}

auto reject_send_like(bot::BotOperationErrorCode code, std::string message)
    -> boost::asio::awaitable<StructuredOperationCompletion> {
  return ready_send_like(
      {bot::StructuredOperationReply::failure(
           {.code = code,
            .message = std::move(message),
            .retryable = false,
            .submission_safety =
                bot::SubmissionSafety::DefinitelyNotSubmitted}),
       StructuredCompletionDisposition::Failed});
}

auto send_like_failure(const PreparedSendLike &prepared,
                       bot::BotOperationErrorCode code,
                       standard::FailureStage stage, std::string message)
    -> standard::SendLikeOutcome {
  const auto evidence = prepared.call->evidence();
  return {standard::ExecutionFailure{
              {.code = code,
               .message = std::move(message),
               .retryable = false,
               .submission_safety =
                   evidence.submission == standard::Submission::NotSubmitted
                       ? bot::SubmissionSafety::DefinitelyNotSubmitted
                       : bot::SubmissionSafety::PossiblySubmitted},
              stage},
          evidence};
}

auto send_like_completion(const PreparedSendLike &prepared,
                          standard::SendLikeOutcome outcome)
    -> StructuredOperationCompletion {
  outcome.validate();
  const auto disposition = standard_detail::completion_disposition(outcome);
  StructuredCompletionDisposition completion;
  switch (disposition) {
  case standard_detail::CompletionDisposition::Completed:
    completion = StructuredCompletionDisposition::Completed;
    break;
  case standard_detail::CompletionDisposition::AcceptedPending:
    completion = StructuredCompletionDisposition::AcceptedPending;
    break;
  case standard_detail::CompletionDisposition::Failed:
    completion = StructuredCompletionDisposition::Failed;
    break;
  }
  return {
      bot::StructuredOperationReply::success(bot::StructuredOperationValue::own(
          prepared.installation, prepared.action,
          std::make_unique<standard::SendLikeOutcome>(std::move(outcome)))),
      completion};
}

auto send_like_transport_failure(
    const PreparedSendLike &prepared,
    standard_detail::StandardTransportFailureKind kind)
    -> standard::SendLikeOutcome {
  using Code = bot::BotOperationErrorCode;
  using Kind = standard_detail::StandardTransportFailureKind;
  const bool not_submitted = prepared.call->evidence().submission ==
                             standard::Submission::NotSubmitted;
  auto code = not_submitted ? Code::TransportFailure : Code::OutcomeUnknown;
  switch (kind) {
  case Kind::Cancelled:
  case Kind::DeadlineExceeded:
    code = Code::Cancelled;
    break;
  case Kind::ConnectionRetired:
  case Kind::TransportError:
    break;
  case Kind::ResponseTooLarge:
  case Kind::InvalidResponse:
    code = not_submitted ? Code::InvalidRequest : Code::MalformedResponse;
    break;
  }
  return send_like_failure(
      prepared, code, standard::FailureStage::Transport,
      "standard send_like transport did not yield a reply");
}

auto send_like_echo_matches(const bot::Json &reply, std::uint64_t echo)
    -> bool {
  const auto found = reply.find("echo");
  if (found == reply.end() || !found->is_string())
    return false;
  char digits[20];
  const auto [end, error] =
      std::to_chars(digits, digits + sizeof(digits), echo);
  if (error != std::errc{})
    return false;
  constexpr std::string_view prefix = "obcx.standard:";
  const std::string_view actual = found->get_ref<const std::string &>();
  return actual.starts_with(prefix) &&
         actual.substr(prefix.size()) == std::string_view(digits, end);
}

auto project_send_like_reply(const PreparedSendLike &prepared,
                             const standard_detail::StandardWireReply &wire)
    -> standard::SendLikeOutcome {
  using Code = bot::BotOperationErrorCode;
  using Kind = standard_detail::NativeReplyKind;
  // Parse the entire bounded frame before association or header observation.
  const auto reply = standard_detail::parse_standard_reply(
      wire.body, prepared.call->limits(), prepared.values);
  if (prepared.captured.transport ==
          standard_detail::StandardWireTransport::WebSocket &&
      (wire.http_status || !send_like_echo_matches(reply, prepared.echo)))
    return send_like_failure(prepared, Code::MalformedResponse,
                             standard::FailureStage::NativeReply,
                             "standard send_like reply association is invalid");
  const auto disposition = standard_detail::classify_native_reply(
      reply, standard_detail::NativeAction::SendLike, prepared.mode,
      prepared.values);
  if (disposition.kind == Kind::Malformed)
    return send_like_failure(prepared, Code::MalformedResponse,
                             standard::FailureStage::NativeReply,
                             "standard send_like reply header is invalid");
  prepared.call->observe(disposition);
  // Header evidence must survive every later status/data/projection failure.
  if (disposition.kind == Kind::Rejected)
    return send_like_failure(prepared, Code::ProviderRejected,
                             standard::FailureStage::NativeReply,
                             "standard send_like was rejected");
  if (prepared.captured.transport ==
          standard_detail::StandardWireTransport::Http &&
      wire.http_status != 200)
    return send_like_failure(prepared, Code::MalformedResponse,
                             standard::FailureStage::NativeReply,
                             "standard send_like HTTP status is inconsistent");
  const auto data = reply.find("data");
  if (data == reply.end() || !data->is_null())
    return send_like_failure(prepared, Code::MalformedResponse,
                             standard::FailureStage::NativeReply,
                             "standard send_like reply data is invalid");
  if (disposition.kind == Kind::AcceptedPending)
    return {
        standard::AcceptedPending{prepared.mode, *disposition.provider_retcode},
        prepared.call->evidence()};
  return {
      standard::Completed<standard::NoResponseData>{standard::NoResponseData{}},
      prepared.call->evidence()};
}

auto finish_send_like(std::shared_ptr<PreparedSendLike> prepared)
    -> boost::asio::awaitable<StructuredOperationCompletion> {
  using Failure = standard_detail::StandardTransportFailureKind;
  try {
    co_await std::move(prepared->captured.operation);
  } catch (const boost::system::system_error &error) {
    // An already-won reply/failure takes precedence over a late exception.
    if (!prepared->call->terminal())
      (void)prepared->call->try_fail(
          {error.code() == boost::asio::error::operation_aborted
               ? Failure::Cancelled
               : Failure::TransportError,
           error.code()});
  } catch (...) {
    if (!prepared->call->terminal())
      (void)prepared->call->try_fail({Failure::TransportError, {}});
  }
  try {
    if (!prepared->call->terminal())
      (void)prepared->call->try_fail({Failure::TransportError, {}});
    // One consumption only; the checkpoint owner remains alive afterwards.
    auto result = prepared->call->take_result();
    if (!result)
      co_return send_like_completion(
          *prepared,
          send_like_transport_failure(*prepared, Failure::TransportError));
    if (const auto *failure =
            std::get_if<standard_detail::StandardTransportFailure>(&*result))
      co_return send_like_completion(
          *prepared, send_like_transport_failure(*prepared, failure->kind));
    std::optional<standard::SendLikeOutcome> outcome;
    try {
      outcome.emplace(project_send_like_reply(
          *prepared, std::get<standard_detail::StandardWireReply>(*result)));
    } catch (...) {
      outcome.emplace(send_like_failure(
          *prepared, bot::BotOperationErrorCode::MalformedResponse,
          standard::FailureStage::NativeReply,
          "standard send_like reply failed validation"));
    }
    co_return send_like_completion(*prepared, std::move(*outcome));
  } catch (...) {
    // NotSubmitted cannot legally be a Projection failure. Keep authoritative
    // evidence unchanged, including any native header already observed.
    const bool not_submitted = prepared->call->evidence().submission ==
                               standard::Submission::NotSubmitted;
    co_return send_like_completion(
        *prepared,
        send_like_failure(*prepared,
                          not_submitted
                              ? bot::BotOperationErrorCode::TransportFailure
                              : bot::BotOperationErrorCode::MalformedResponse,
                          not_submitted ? standard::FailureStage::Transport
                                        : standard::FailureStage::Projection,
                          "standard send_like result projection failed"));
  }
}

auto serialize_send_like_parameters(const bot::Json &parameters,
                                    std::size_t maximum_bytes) -> std::string {
  if (!parameters.is_object() || parameters.size() != 2 ||
      parameters.at("user_id").type() != bot::Json::value_t::number_integer ||
      parameters.at("times").type() != bot::Json::value_t::number_integer)
    throw std::invalid_argument(
        "standard send_like parameter shape is invalid");
  const auto user = parameters.at("user_id").get<std::int64_t>();
  const auto times = parameters.at("times").get<std::int64_t>();
  if (user <= 0 || times < 0)
    throw std::invalid_argument("standard send_like parameters are invalid");
  char user_digits[20];
  char times_digits[20];
  const auto user_result =
      std::to_chars(user_digits, user_digits + sizeof(user_digits), user);
  const auto times_result =
      std::to_chars(times_digits, times_digits + sizeof(times_digits), times);
  if (user_result.ec != std::errc{} || times_result.ec != std::errc{})
    throw std::invalid_argument("standard send_like integer encoding failed");
  constexpr auto punctuation = sizeof("{\"user_id\":,\"times\":}") - 1;
  const auto size = punctuation + (user_result.ptr - user_digits) +
                    (times_result.ptr - times_digits);
  if (size > maximum_bytes)
    throw std::invalid_argument("standard send_like parameters exceed limit");
  auto serialized = parameters.dump();
  if (serialized.size() != size)
    throw std::invalid_argument("standard send_like parameter shape changed");
  return serialized;
}
struct PreparedMutation {
  std::shared_ptr<OneBot11TransportCapability> transport_owner;
  std::shared_ptr<SelfIdentityCache> identity_owner;
  std::shared_ptr<standard_detail::StandardCallState> call;
  const standard_detail::NativeAction native_action;
  const standard::ExecutionMode mode;
  const standard::ValueLimits values;
  const std::uint64_t echo;
  const bot::BotInstallationRef installation;
  const bot::ActionId action;
  standard_detail::CapturedStandardSend captured;
};

auto ready_mutation(StructuredOperationCompletion completion)
    -> boost::asio::awaitable<StructuredOperationCompletion> {
  co_return std::move(completion);
}

auto reject_mutation(bot::BotOperationErrorCode code, std::string message)
    -> boost::asio::awaitable<StructuredOperationCompletion> {
  return ready_mutation(
      {bot::StructuredOperationReply::failure(
           {.code = code,
            .message = std::move(message),
            .retryable = false,
            .submission_safety =
                bot::SubmissionSafety::DefinitelyNotSubmitted}),
       StructuredCompletionDisposition::Failed});
}

auto mutation_failure(const PreparedMutation &prepared,
                      bot::BotOperationErrorCode code,
                      standard::FailureStage stage, std::string message)
    -> standard::ExecutionOutcome<standard::NoResponseData> {
  const auto evidence = prepared.call->evidence();
  return {standard::ExecutionFailure{
              {.code = code,
               .message = std::move(message),
               .retryable = false,
               .submission_safety =
                   evidence.submission == standard::Submission::NotSubmitted
                       ? bot::SubmissionSafety::DefinitelyNotSubmitted
                       : bot::SubmissionSafety::PossiblySubmitted},
              stage},
          evidence};
}

auto mutation_completion(
    const PreparedMutation &prepared,
    standard::ExecutionOutcome<standard::NoResponseData> outcome)
    -> StructuredOperationCompletion {
  outcome.validate();
  const auto disposition = standard_detail::completion_disposition(outcome);
  StructuredCompletionDisposition completion;
  switch (disposition) {
  case standard_detail::CompletionDisposition::Completed:
    completion = StructuredCompletionDisposition::Completed;
    break;
  case standard_detail::CompletionDisposition::AcceptedPending:
    completion = StructuredCompletionDisposition::AcceptedPending;
    break;
  case standard_detail::CompletionDisposition::Failed:
    completion = StructuredCompletionDisposition::Failed;
    break;
  }
  return {
      bot::StructuredOperationReply::success(bot::StructuredOperationValue::own(
          prepared.installation, prepared.action,
          std::make_unique<
              standard::ExecutionOutcome<standard::NoResponseData>>(
              std::move(outcome)))),
      completion};
}

auto mutation_transport_failure(
    const PreparedMutation &prepared,
    standard_detail::StandardTransportFailureKind kind)
    -> standard::ExecutionOutcome<standard::NoResponseData> {
  using Code = bot::BotOperationErrorCode;
  using Kind = standard_detail::StandardTransportFailureKind;
  const bool not_submitted = prepared.call->evidence().submission ==
                             standard::Submission::NotSubmitted;
  auto code = not_submitted ? Code::TransportFailure : Code::OutcomeUnknown;
  switch (kind) {
  case Kind::Cancelled:
  case Kind::DeadlineExceeded:
    code = Code::Cancelled;
    break;
  case Kind::ConnectionRetired:
  case Kind::TransportError:
    break;
  case Kind::ResponseTooLarge:
  case Kind::InvalidResponse:
    code = not_submitted ? Code::InvalidRequest : Code::MalformedResponse;
    break;
  }
  return mutation_failure(prepared, code, standard::FailureStage::Transport,
                          "standard mutation transport did not yield a reply");
}

auto mutation_echo_matches(const bot::Json &reply, std::uint64_t echo) -> bool {
  const auto found = reply.find("echo");
  if (found == reply.end() || !found->is_string())
    return false;
  char digits[20];
  const auto [end, error] =
      std::to_chars(digits, digits + sizeof(digits), echo);
  if (error != std::errc{})
    return false;
  constexpr std::string_view prefix = "obcx.standard:";
  const std::string_view actual = found->get_ref<const std::string &>();
  return actual.starts_with(prefix) &&
         actual.substr(prefix.size()) == std::string_view(digits, end);
}

auto project_mutation_reply(const PreparedMutation &prepared,
                            const standard_detail::StandardWireReply &wire)
    -> standard::ExecutionOutcome<standard::NoResponseData> {
  using Code = bot::BotOperationErrorCode;
  using Kind = standard_detail::NativeReplyKind;
  // Parse the entire bounded frame before association or header observation.
  const auto reply = standard_detail::parse_standard_reply(
      wire.body, prepared.call->limits(), prepared.values);
  if (prepared.captured.transport ==
          standard_detail::StandardWireTransport::WebSocket &&
      (wire.http_status || !mutation_echo_matches(reply, prepared.echo)))
    return mutation_failure(prepared, Code::MalformedResponse,
                            standard::FailureStage::NativeReply,
                            "standard mutation reply association is invalid");
  const auto disposition = standard_detail::classify_native_reply(
      reply, prepared.native_action, prepared.mode, prepared.values);
  if (disposition.kind == Kind::Malformed)
    return mutation_failure(prepared, Code::MalformedResponse,
                            standard::FailureStage::NativeReply,
                            "standard mutation reply header is invalid");
  prepared.call->observe(disposition);
  // Header evidence must survive every later status/data/projection failure.
  if (disposition.kind == Kind::Rejected)
    return mutation_failure(prepared, Code::ProviderRejected,
                            standard::FailureStage::NativeReply,
                            "standard mutation was rejected");
  if (prepared.captured.transport ==
          standard_detail::StandardWireTransport::Http &&
      wire.http_status != 200)
    return mutation_failure(prepared, Code::MalformedResponse,
                            standard::FailureStage::NativeReply,
                            "standard mutation HTTP status is inconsistent");
  const auto data = reply.find("data");
  if (data == reply.end() || !data->is_null())
    return mutation_failure(prepared, Code::MalformedResponse,
                            standard::FailureStage::NativeReply,
                            "standard mutation reply data is invalid");
  if (disposition.kind == Kind::AcceptedPending)
    return {
        standard::AcceptedPending{prepared.mode, *disposition.provider_retcode},
        prepared.call->evidence()};
  return {
      standard::Completed<standard::NoResponseData>{standard::NoResponseData{}},
      prepared.call->evidence()};
}

auto finish_mutation(std::shared_ptr<PreparedMutation> prepared)
    -> boost::asio::awaitable<StructuredOperationCompletion> {
  using Failure = standard_detail::StandardTransportFailureKind;
  try {
    co_await std::move(prepared->captured.operation);
  } catch (const boost::system::system_error &error) {
    // An already-won reply/failure takes precedence over a late exception.
    if (!prepared->call->terminal())
      (void)prepared->call->try_fail(
          {error.code() == boost::asio::error::operation_aborted
               ? Failure::Cancelled
               : Failure::TransportError,
           error.code()});
  } catch (...) {
    if (!prepared->call->terminal())
      (void)prepared->call->try_fail({Failure::TransportError, {}});
  }
  try {
    if (!prepared->call->terminal())
      (void)prepared->call->try_fail({Failure::TransportError, {}});
    // One consumption only; the checkpoint owner remains alive afterwards.
    auto result = prepared->call->take_result();
    if (!result)
      co_return mutation_completion(
          *prepared,
          mutation_transport_failure(*prepared, Failure::TransportError));
    if (const auto *failure =
            std::get_if<standard_detail::StandardTransportFailure>(&*result))
      co_return mutation_completion(
          *prepared, mutation_transport_failure(*prepared, failure->kind));
    std::optional<standard::ExecutionOutcome<standard::NoResponseData>> outcome;
    try {
      outcome.emplace(project_mutation_reply(
          *prepared, std::get<standard_detail::StandardWireReply>(*result)));
    } catch (...) {
      outcome.emplace(mutation_failure(
          *prepared, bot::BotOperationErrorCode::MalformedResponse,
          standard::FailureStage::NativeReply,
          "standard mutation reply failed validation"));
    }
    co_return mutation_completion(*prepared, std::move(*outcome));
  } catch (...) {
    // NotSubmitted cannot legally be a Projection failure. Keep authoritative
    // evidence unchanged, including any native header already observed.
    const bool not_submitted = prepared->call->evidence().submission ==
                               standard::Submission::NotSubmitted;
    co_return mutation_completion(
        *prepared,
        mutation_failure(*prepared,
                         not_submitted
                             ? bot::BotOperationErrorCode::TransportFailure
                             : bot::BotOperationErrorCode::MalformedResponse,
                         not_submitted ? standard::FailureStage::Transport
                                       : standard::FailureStage::Projection,
                         "standard mutation result projection failed"));
  }
}

template <typename Request> struct SendMessageOperation;
template <> struct SendMessageOperation<standard::SendPrivateMsgRequest> {
  using Result = standard::SendMessageData;
  static constexpr auto action = standard_detail::NativeAction::SendPrivateMsg;
  static constexpr auto encode = &standard_detail::encode_send_private_msg;
  static constexpr auto decode = &standard_detail::decode_send_private_msg;
};
template <> struct SendMessageOperation<standard::SendGroupMsgRequest> {
  using Result = standard::SendMessageData;
  static constexpr auto action = standard_detail::NativeAction::SendGroupMsg;
  static constexpr auto encode = &standard_detail::encode_send_group_msg;
  static constexpr auto decode = &standard_detail::decode_send_group_msg;
};
template <> struct SendMessageOperation<standard::SendMsgRequest> {
  using Result = standard::SendMessageData;
  static constexpr auto action = standard_detail::NativeAction::SendMsg;
  static constexpr auto encode = &standard_detail::encode_send_msg;
  static constexpr auto decode = &standard_detail::decode_send_msg;
};
template <typename Request>
concept StandardSendMessage =
    requires { typename SendMessageOperation<Request>::Result; };
template <StandardSendMessage Request>
using SendMessageOutcome =
    standard::ExecutionOutcome<typename SendMessageOperation<Request>::Result>;
template <StandardSendMessage Request> struct PreparedSendMessage {
  std::shared_ptr<const PlatformDomainBinding> binding_owner;
  const Request request;
  std::shared_ptr<OneBot11TransportCapability> transport_owner;
  std::shared_ptr<SelfIdentityCache> identity_owner;
  std::shared_ptr<standard_detail::StandardCallState> call;
  const standard_detail::NativeAction native_action;
  const standard::ExecutionMode mode;
  const standard::ValueLimits values;
  const std::uint64_t echo;
  const bot::BotInstallationRef installation;
  const bot::ActionId action;
  standard_detail::CapturedStandardSend captured;
};

auto ready_send_message(StructuredOperationCompletion completion)
    -> boost::asio::awaitable<StructuredOperationCompletion> {
  co_return std::move(completion);
}

auto reject_send_message(bot::BotOperationErrorCode code, std::string message)
    -> boost::asio::awaitable<StructuredOperationCompletion> {
  return ready_send_message(
      {bot::StructuredOperationReply::failure(
           {.code = code,
            .message = std::move(message),
            .retryable = false,
            .submission_safety =
                bot::SubmissionSafety::DefinitelyNotSubmitted}),
       StructuredCompletionDisposition::Failed});
}

template <StandardSendMessage Request>
auto send_message_failure(const PreparedSendMessage<Request> &prepared,
                          bot::BotOperationErrorCode code,
                          standard::FailureStage stage, std::string message)
    -> SendMessageOutcome<Request> {
  const auto evidence = prepared.call->evidence();
  return {standard::ExecutionFailure{
              {.code = code,
               .message = std::move(message),
               .retryable = false,
               .submission_safety =
                   evidence.submission == standard::Submission::NotSubmitted
                       ? bot::SubmissionSafety::DefinitelyNotSubmitted
                       : bot::SubmissionSafety::PossiblySubmitted},
              stage},
          evidence};
}

template <StandardSendMessage Request>
auto send_message_completion(const PreparedSendMessage<Request> &prepared,
                             SendMessageOutcome<Request> outcome)
    -> StructuredOperationCompletion {
  bot::OperationTraits<Request>::validate_result(prepared.request, outcome);
  const auto disposition = standard_detail::completion_disposition(outcome);
  StructuredCompletionDisposition completion;
  switch (disposition) {
  case standard_detail::CompletionDisposition::Completed:
    completion = StructuredCompletionDisposition::Completed;
    break;
  case standard_detail::CompletionDisposition::AcceptedPending:
    completion = StructuredCompletionDisposition::AcceptedPending;
    break;
  case standard_detail::CompletionDisposition::Failed:
    completion = StructuredCompletionDisposition::Failed;
    break;
  }
  return {
      bot::StructuredOperationReply::success(bot::StructuredOperationValue::own(
          prepared.installation, prepared.action,
          std::make_unique<SendMessageOutcome<Request>>(std::move(outcome)))),
      completion};
}

template <StandardSendMessage Request>
auto send_message_transport_failure(
    const PreparedSendMessage<Request> &prepared,
    standard_detail::StandardTransportFailureKind kind)
    -> SendMessageOutcome<Request> {
  using Code = bot::BotOperationErrorCode;
  using Kind = standard_detail::StandardTransportFailureKind;
  const bool not_submitted = prepared.call->evidence().submission ==
                             standard::Submission::NotSubmitted;
  auto code = not_submitted ? Code::TransportFailure : Code::OutcomeUnknown;
  switch (kind) {
  case Kind::Cancelled:
  case Kind::DeadlineExceeded:
    code = Code::Cancelled;
    break;
  case Kind::ConnectionRetired:
  case Kind::TransportError:
    break;
  case Kind::ResponseTooLarge:
  case Kind::InvalidResponse:
    code = not_submitted ? Code::InvalidRequest : Code::MalformedResponse;
    break;
  }
  return send_message_failure(
      prepared, code, standard::FailureStage::Transport,
      "standard send_message transport did not yield a reply");
}

auto send_message_echo_matches(const bot::Json &reply, std::uint64_t echo)
    -> bool {
  const auto found = reply.find("echo");
  if (found == reply.end() || !found->is_string())
    return false;
  char digits[20];
  const auto [end, error] =
      std::to_chars(digits, digits + sizeof(digits), echo);
  if (error != std::errc{})
    return false;
  constexpr std::string_view prefix = "obcx.standard:";
  const std::string_view actual = found->get_ref<const std::string &>();
  return actual.starts_with(prefix) &&
         actual.substr(prefix.size()) == std::string_view(digits, end);
}

template <StandardSendMessage Request>
auto project_send_message_reply(const PreparedSendMessage<Request> &prepared,
                                const standard_detail::StandardWireReply &wire)
    -> SendMessageOutcome<Request> {
  using Code = bot::BotOperationErrorCode;
  using Kind = standard_detail::NativeReplyKind;
  // Parse the entire bounded frame before association or header observation.
  const auto reply = standard_detail::parse_standard_reply(
      wire.body, prepared.call->limits(), prepared.values);
  if (prepared.captured.transport ==
          standard_detail::StandardWireTransport::WebSocket &&
      (wire.http_status || !send_message_echo_matches(reply, prepared.echo)))
    return send_message_failure(
        prepared, Code::MalformedResponse, standard::FailureStage::NativeReply,
        "standard send_message reply association is invalid");
  const auto disposition = standard_detail::classify_native_reply(
      reply, prepared.native_action, prepared.mode, prepared.values);
  if (disposition.kind == Kind::Malformed)
    return send_message_failure(
        prepared, Code::MalformedResponse, standard::FailureStage::NativeReply,
        "standard send_message reply header is invalid");
  prepared.call->observe(disposition);
  // Header evidence must survive every later status/data/projection failure.
  if (disposition.kind == Kind::Rejected)
    return send_message_failure(prepared, Code::ProviderRejected,
                                standard::FailureStage::NativeReply,
                                "standard send_message was rejected");
  if (prepared.captured.transport ==
          standard_detail::StandardWireTransport::Http &&
      wire.http_status != 200)
    return send_message_failure(
        prepared, Code::MalformedResponse, standard::FailureStage::NativeReply,
        "standard send_message HTTP status is inconsistent");
  const auto data = reply.find("data");
  if (data == reply.end())
    return send_message_failure(prepared, Code::MalformedResponse,
                                standard::FailureStage::NativeReply,
                                "standard send_message reply data is missing");
  if (disposition.kind == Kind::AcceptedPending) {
    if (!data->is_null())
      return send_message_failure(
          prepared, Code::MalformedResponse,
          standard::FailureStage::NativeReply,
          "standard send_message pending data is invalid");
    return {
        standard::AcceptedPending{prepared.mode, *disposition.provider_retcode},
        prepared.call->evidence()};
  }
  // The original binding owner survives await; never decode with a new account
  // or a codec borrowed from an Endpoint whose lifetime has ended.
  return {standard::Completed<typename SendMessageOperation<Request>::Result>{
              SendMessageOperation<Request>::decode(
                  *data, prepared.request.parameters,
                  prepared.binding_owner->codec(), prepared.values)},
          prepared.call->evidence()};
}

template <StandardSendMessage Request>
auto finish_send_message(std::shared_ptr<PreparedSendMessage<Request>> prepared)
    -> boost::asio::awaitable<StructuredOperationCompletion> {
  using Failure = standard_detail::StandardTransportFailureKind;
  try {
    co_await std::move(prepared->captured.operation);
  } catch (const boost::system::system_error &error) {
    // An already-won reply/failure takes precedence over a late exception.
    if (!prepared->call->terminal())
      (void)prepared->call->try_fail(
          {error.code() == boost::asio::error::operation_aborted
               ? Failure::Cancelled
               : Failure::TransportError,
           error.code()});
  } catch (...) {
    if (!prepared->call->terminal())
      (void)prepared->call->try_fail({Failure::TransportError, {}});
  }
  try {
    if (!prepared->call->terminal())
      (void)prepared->call->try_fail({Failure::TransportError, {}});
    // One consumption only; the checkpoint owner remains alive afterwards.
    auto result = prepared->call->take_result();
    if (!result)
      co_return send_message_completion(
          *prepared,
          send_message_transport_failure(*prepared, Failure::TransportError));
    if (const auto *failure =
            std::get_if<standard_detail::StandardTransportFailure>(&*result))
      co_return send_message_completion(
          *prepared, send_message_transport_failure(*prepared, failure->kind));
    std::optional<SendMessageOutcome<Request>> outcome;
    try {
      outcome.emplace(project_send_message_reply(
          *prepared, std::get<standard_detail::StandardWireReply>(*result)));
    } catch (...) {
      outcome.emplace(send_message_failure(
          *prepared, bot::BotOperationErrorCode::MalformedResponse,
          standard::FailureStage::NativeReply,
          "standard send_message reply failed validation"));
    }
    co_return send_message_completion(*prepared, std::move(*outcome));
  } catch (...) {
    // NotSubmitted cannot legally be a Projection failure. Keep authoritative
    // evidence unchanged, including any native header already observed.
    const bool not_submitted = prepared->call->evidence().submission ==
                               standard::Submission::NotSubmitted;
    co_return send_message_completion(
        *prepared,
        send_message_failure(
            *prepared,
            not_submitted ? bot::BotOperationErrorCode::TransportFailure
                          : bot::BotOperationErrorCode::MalformedResponse,
            not_submitted ? standard::FailureStage::Transport
                          : standard::FailureStage::Projection,
            "standard send_message result projection failed"));
  }
}

template <typename Request> struct QueryOperation;
template <> struct QueryOperation<standard::GetStrangerInfoRequest> {
  using Result = standard::StrangerInfo;
  static constexpr auto action = standard_detail::NativeAction::GetStrangerInfo;
  static constexpr auto encode = &standard_detail::encode_get_stranger_info;
  static constexpr auto decode = &standard_detail::decode_get_stranger_info;
};
template <> struct QueryOperation<standard::GetLoginInfoRequest> {
  using Result = standard::LoginInfo;
  static constexpr auto action = standard_detail::NativeAction::GetLoginInfo;
  static constexpr auto encode = &standard_detail::encode_get_login_info;
  static constexpr auto decode = &standard_detail::decode_get_login_info;
};
template <> struct QueryOperation<standard::GetFriendListRequest> {
  using Result = standard::FriendList;
  static constexpr auto action = standard_detail::NativeAction::GetFriendList;
  static constexpr auto encode = &standard_detail::encode_get_friend_list;
  static constexpr auto decode = &standard_detail::decode_get_friend_list;
};
template <> struct QueryOperation<standard::GetGroupInfoRequest> {
  using Result = standard::GroupInfo;
  static constexpr auto action = standard_detail::NativeAction::GetGroupInfo;
  static constexpr auto encode = &standard_detail::encode_get_group_info;
  static constexpr auto decode = &standard_detail::decode_get_group_info;
};
template <> struct QueryOperation<standard::GetGroupListRequest> {
  using Result = standard::GroupList;
  static constexpr auto action = standard_detail::NativeAction::GetGroupList;
  static constexpr auto encode = &standard_detail::encode_get_group_list;
  static constexpr auto decode = &standard_detail::decode_get_group_list;
};
template <> struct QueryOperation<standard::GetGroupMemberInfoRequest> {
  using Result = standard::GroupMemberInfo;
  static constexpr auto action =
      standard_detail::NativeAction::GetGroupMemberInfo;
  static constexpr auto encode = &standard_detail::encode_get_group_member_info;
  static constexpr auto decode = &standard_detail::decode_get_group_member_info;
};
template <> struct QueryOperation<standard::GetGroupMemberListRequest> {
  using Result = standard::GroupMemberList;
  static constexpr auto action =
      standard_detail::NativeAction::GetGroupMemberList;
  static constexpr auto encode = &standard_detail::encode_get_group_member_list;
  static constexpr auto decode = &standard_detail::decode_get_group_member_list;
};
template <> struct QueryOperation<standard::GetGroupHonorInfoRequest> {
  using Result = standard::GroupHonorInfo;
  static constexpr auto action =
      standard_detail::NativeAction::GetGroupHonorInfo;
  static constexpr auto encode = &standard_detail::encode_get_group_honor_info;
  static constexpr auto decode = &standard_detail::decode_get_group_honor_info;
};
template <> struct QueryOperation<standard::GetMsgRequest> {
  using Result = standard::DetachedMessage;
  static constexpr auto action = standard_detail::NativeAction::GetMsg;
  static constexpr auto decode = &standard_detail::decode_get_msg;
};
template <> struct QueryOperation<standard::GetForwardMsgRequest> {
  using Result = standard::DetachedForwardMessage;
  static constexpr auto action = standard_detail::NativeAction::GetForwardMsg;
  static constexpr auto decode = &standard_detail::decode_get_forward_msg;
};
template <typename Request>
concept OrdinaryQuery = requires { typename QueryOperation<Request>::Result; };
template <OrdinaryQuery Request>
using QueryOutcome =
    standard::ExecutionOutcome<typename QueryOperation<Request>::Result>;
template <OrdinaryQuery Request> struct PreparedQuery {
  std::shared_ptr<const PlatformDomainBinding> binding_owner;
  const Request request;
  std::shared_ptr<OneBot11TransportCapability> transport_owner;
  std::shared_ptr<SelfIdentityCache> identity_owner;
  std::shared_ptr<standard_detail::StandardCallState> call;
  const standard_detail::NativeAction native_action;
  const standard::ExecutionMode mode;
  const standard::ValueLimits values;
  const std::uint64_t echo;
  const bot::BotInstallationRef installation;
  const bot::ActionId action;
  standard_detail::CapturedStandardSend captured;
};

auto ready_query(StructuredOperationCompletion completion)
    -> boost::asio::awaitable<StructuredOperationCompletion> {
  co_return std::move(completion);
}

auto reject_query(bot::BotOperationErrorCode code, std::string message)
    -> boost::asio::awaitable<StructuredOperationCompletion> {
  return ready_query({bot::StructuredOperationReply::failure(
                          {.code = code,
                           .message = std::move(message),
                           .retryable = false,
                           .submission_safety =
                               bot::SubmissionSafety::DefinitelyNotSubmitted}),
                      StructuredCompletionDisposition::Failed});
}

template <OrdinaryQuery Request>
auto query_failure(const PreparedQuery<Request> &prepared,
                   bot::BotOperationErrorCode code,
                   standard::FailureStage stage, std::string message)
    -> QueryOutcome<Request> {
  const auto evidence = prepared.call->evidence();
  return {standard::ExecutionFailure{
              {.code = code,
               .message = std::move(message),
               .retryable = false,
               .submission_safety =
                   evidence.submission == standard::Submission::NotSubmitted
                       ? bot::SubmissionSafety::DefinitelyNotSubmitted
                       : bot::SubmissionSafety::PossiblySubmitted},
              stage},
          evidence};
}

template <OrdinaryQuery Request>
auto query_completion(const PreparedQuery<Request> &prepared,
                      QueryOutcome<Request> outcome)
    -> StructuredOperationCompletion {
  bot::OperationTraits<Request>::validate_result(prepared.request, outcome);
  const auto disposition = standard_detail::completion_disposition(outcome);
  StructuredCompletionDisposition completion;
  switch (disposition) {
  case standard_detail::CompletionDisposition::Completed:
    completion = StructuredCompletionDisposition::Completed;
    break;
  case standard_detail::CompletionDisposition::AcceptedPending:
    completion = StructuredCompletionDisposition::AcceptedPending;
    break;
  case standard_detail::CompletionDisposition::Failed:
    completion = StructuredCompletionDisposition::Failed;
    break;
  }
  return {
      bot::StructuredOperationReply::success(bot::StructuredOperationValue::own(
          prepared.installation, prepared.action,
          std::make_unique<QueryOutcome<Request>>(std::move(outcome)))),
      completion};
}

template <OrdinaryQuery Request>
auto query_transport_failure(const PreparedQuery<Request> &prepared,
                             standard_detail::StandardTransportFailureKind kind)
    -> QueryOutcome<Request> {
  using Code = bot::BotOperationErrorCode;
  using Kind = standard_detail::StandardTransportFailureKind;
  const bool not_submitted = prepared.call->evidence().submission ==
                             standard::Submission::NotSubmitted;
  auto code = not_submitted ? Code::TransportFailure : Code::OutcomeUnknown;
  switch (kind) {
  case Kind::Cancelled:
  case Kind::DeadlineExceeded:
    code = Code::Cancelled;
    break;
  case Kind::ConnectionRetired:
  case Kind::TransportError:
    break;
  case Kind::ResponseTooLarge:
  case Kind::InvalidResponse:
    code = not_submitted ? Code::InvalidRequest : Code::MalformedResponse;
    break;
  }
  return query_failure(prepared, code, standard::FailureStage::Transport,
                       "standard query transport did not yield a reply");
}

auto query_echo_matches(const bot::Json &reply, std::uint64_t echo) -> bool {
  const auto found = reply.find("echo");
  if (found == reply.end() || !found->is_string())
    return false;
  char digits[20];
  const auto [end, error] =
      std::to_chars(digits, digits + sizeof(digits), echo);
  if (error != std::errc{})
    return false;
  constexpr std::string_view prefix = "obcx.standard:";
  const std::string_view actual = found->get_ref<const std::string &>();
  return actual.starts_with(prefix) &&
         actual.substr(prefix.size()) == std::string_view(digits, end);
}

template <OrdinaryQuery Request>
auto project_query_reply(const PreparedQuery<Request> &prepared,
                         const standard_detail::StandardWireReply &wire)
    -> QueryOutcome<Request> {
  using Code = bot::BotOperationErrorCode;
  using Kind = standard_detail::NativeReplyKind;
  // Parse the entire bounded frame before association or header observation.
  const auto reply = standard_detail::parse_standard_reply(
      wire.body, prepared.call->limits(), prepared.values);
  if (prepared.captured.transport ==
          standard_detail::StandardWireTransport::WebSocket &&
      (wire.http_status || !query_echo_matches(reply, prepared.echo)))
    return query_failure(prepared, Code::MalformedResponse,
                         standard::FailureStage::NativeReply,
                         "standard query reply association is invalid");
  const auto disposition = standard_detail::classify_native_reply(
      reply, prepared.native_action, prepared.mode, prepared.values);
  if (disposition.kind == Kind::Malformed)
    return query_failure(prepared, Code::MalformedResponse,
                         standard::FailureStage::NativeReply,
                         "standard query reply header is invalid");
  prepared.call->observe(disposition);
  // Header evidence must survive every later status/data/projection failure.
  if (disposition.kind == Kind::Rejected)
    return query_failure(prepared, Code::ProviderRejected,
                         standard::FailureStage::NativeReply,
                         "standard query was rejected");
  if (prepared.captured.transport ==
          standard_detail::StandardWireTransport::Http &&
      wire.http_status != 200)
    return query_failure(prepared, Code::MalformedResponse,
                         standard::FailureStage::NativeReply,
                         "standard query HTTP status is inconsistent");
  const auto data = reply.find("data");
  if (data == reply.end())
    return query_failure(prepared, Code::MalformedResponse,
                         standard::FailureStage::NativeReply,
                         "standard query reply data is missing");
  if (disposition.kind == Kind::AcceptedPending) {
    if (!data->is_null())
      return query_failure(prepared, Code::MalformedResponse,
                           standard::FailureStage::NativeReply,
                           "standard query pending data is invalid");
    return {
        standard::AcceptedPending{prepared.mode, *disposition.provider_retcode},
        prepared.call->evidence()};
  }
  // The original binding owner survives await; never decode with a new account
  // or a codec borrowed from an Endpoint whose lifetime has ended.
  return {standard::Completed<typename QueryOperation<Request>::Result>{
              QueryOperation<Request>::decode(
                  *data, prepared.request.parameters,
                  prepared.binding_owner->codec(), prepared.values)},
          prepared.call->evidence()};
}

template <OrdinaryQuery Request>
auto finish_query(std::shared_ptr<PreparedQuery<Request>> prepared)
    -> boost::asio::awaitable<StructuredOperationCompletion> {
  using Failure = standard_detail::StandardTransportFailureKind;
  try {
    co_await std::move(prepared->captured.operation);
  } catch (const boost::system::system_error &error) {
    // An already-won reply/failure takes precedence over a late exception.
    if (!prepared->call->terminal())
      (void)prepared->call->try_fail(
          {error.code() == boost::asio::error::operation_aborted
               ? Failure::Cancelled
               : Failure::TransportError,
           error.code()});
  } catch (...) {
    if (!prepared->call->terminal())
      (void)prepared->call->try_fail({Failure::TransportError, {}});
  }
  try {
    if (!prepared->call->terminal())
      (void)prepared->call->try_fail({Failure::TransportError, {}});
    // One consumption only; the checkpoint owner remains alive afterwards.
    auto result = prepared->call->take_result();
    if (!result)
      co_return query_completion(
          *prepared,
          query_transport_failure(*prepared, Failure::TransportError));
    if (const auto *failure =
            std::get_if<standard_detail::StandardTransportFailure>(&*result))
      co_return query_completion(
          *prepared, query_transport_failure(*prepared, failure->kind));
    std::optional<QueryOutcome<Request>> outcome;
    try {
      outcome.emplace(project_query_reply(
          *prepared, std::get<standard_detail::StandardWireReply>(*result)));
    } catch (...) {
      outcome.emplace(query_failure(
          *prepared, bot::BotOperationErrorCode::MalformedResponse,
          standard::FailureStage::NativeReply,
          "standard query reply failed validation"));
    }
    co_return query_completion(*prepared, std::move(*outcome));
  } catch (...) {
    // NotSubmitted cannot legally be a Projection failure. Keep authoritative
    // evidence unchanged, including any native header already observed.
    const bool not_submitted = prepared->call->evidence().submission ==
                               standard::Submission::NotSubmitted;
    co_return query_completion(
        *prepared,
        query_failure(*prepared,
                      not_submitted
                          ? bot::BotOperationErrorCode::TransportFailure
                          : bot::BotOperationErrorCode::MalformedResponse,
                      not_submitted ? standard::FailureStage::Transport
                                    : standard::FailureStage::Projection,
                      "standard query result projection failed"));
  }
}

auto finish_login(
    std::shared_ptr<PreparedQuery<standard::GetLoginInfoRequest>> prepared,
    std::shared_ptr<standard_detail::LoginCoordinator> coordinator,
    SelfIdentityOrigin origin)
    -> boost::asio::awaitable<StructuredOperationCompletion> {
  using Failure = standard_detail::StandardTransportFailureKind;
  try {
    co_await std::move(prepared->captured.operation);
  } catch (const boost::system::system_error &error) {
    // An already-won reply/failure takes precedence over a late exception.
    if (!prepared->call->terminal())
      (void)prepared->call->try_fail(
          {error.code() == boost::asio::error::operation_aborted
               ? Failure::Cancelled
               : Failure::TransportError,
           error.code()});
  } catch (...) {
    if (!prepared->call->terminal())
      (void)prepared->call->try_fail({Failure::TransportError, {}});
  }
  try {
    if (!prepared->call->terminal())
      (void)prepared->call->try_fail({Failure::TransportError, {}});
    // One consumption only; the checkpoint owner remains alive afterwards.
    auto result = prepared->call->take_result();
    if (!result)
      co_return query_completion(
          *prepared,
          query_transport_failure(*prepared, Failure::TransportError));
    if (const auto *failure =
            std::get_if<standard_detail::StandardTransportFailure>(&*result))
      co_return query_completion(
          *prepared, query_transport_failure(*prepared, failure->kind));
    std::optional<standard::GetLoginInfoOutcome> outcome;
    try {
      outcome.emplace(project_query_reply(
          *prepared, std::get<standard_detail::StandardWireReply>(*result)));
    } catch (...) {
      outcome.emplace(query_failure(
          *prepared, bot::BotOperationErrorCode::MalformedResponse,
          standard::FailureStage::NativeReply,
          "standard query reply failed validation"));
    }
    // Validate the complete typed identity before the shared publication
    // boundary. Pending/rejected/unknown never touch the identity snapshot.
    bot::OperationTraits<standard::GetLoginInfoRequest>::validate_result(
        prepared->request, *outcome);
    if (const auto *completed =
            std::get_if<standard::Completed<standard::LoginInfo>>(
                &outcome->state)) {
      const auto publication =
          coordinator->publish_completed(origin, completed->value);
      if (!publication.accepted())
        outcome.emplace(query_failure(
            *prepared, bot::BotOperationErrorCode::MalformedResponse,
            standard::FailureStage::Projection,
            "standard login identity publication was rejected"));
      // A transition retires origin. Do not use publication.current_origin
      // to rebase this completion or any other admitted work.
    }
    co_return query_completion(*prepared, std::move(*outcome));
  } catch (...) {
    // NotSubmitted cannot legally be a Projection failure. Keep authoritative
    // evidence unchanged, including any native header already observed.
    const bool not_submitted = prepared->call->evidence().submission ==
                               standard::Submission::NotSubmitted;
    co_return query_completion(
        *prepared,
        query_failure(*prepared,
                      not_submitted
                          ? bot::BotOperationErrorCode::TransportFailure
                          : bot::BotOperationErrorCode::MalformedResponse,
                      not_submitted ? standard::FailureStage::Transport
                                    : standard::FailureStage::Projection,
                      "standard query result projection failed"));
  }
}

template <typename Request> struct ServiceOperation;
template <> struct ServiceOperation<standard::GetCookiesRequest> {
  using Result = standard::CookiesData;
  static constexpr auto action = standard_detail::NativeAction::GetCookies;
  static constexpr auto decode = &standard_detail::decode_get_cookies_data;
};
template <> struct ServiceOperation<standard::GetCsrfTokenRequest> {
  using Result = standard::CsrfTokenData;
  static constexpr auto action = standard_detail::NativeAction::GetCsrfToken;
  static constexpr auto decode = &standard_detail::decode_get_csrf_token_data;
};
template <> struct ServiceOperation<standard::GetCredentialsRequest> {
  using Result = standard::CredentialsData;
  static constexpr auto action = standard_detail::NativeAction::GetCredentials;
  static constexpr auto decode = &standard_detail::decode_get_credentials_data;
};
template <> struct ServiceOperation<standard::GetStatusRequest> {
  using Result = standard::StatusData;
  static constexpr auto action = standard_detail::NativeAction::GetStatus;
  static constexpr auto decode = &standard_detail::decode_get_status_data;
};
template <> struct ServiceOperation<standard::GetVersionInfoRequest> {
  using Result = standard::VersionInfoData;
  static constexpr auto action = standard_detail::NativeAction::GetVersionInfo;
  static constexpr auto decode = &standard_detail::decode_get_version_info_data;
};
template <> struct ServiceOperation<standard::CanSendImageRequest> {
  using Result = standard::CanSendImageData;
  static constexpr auto action = standard_detail::NativeAction::CanSendImage;
  static constexpr auto decode = &standard_detail::decode_can_send_image_data;
};
template <> struct ServiceOperation<standard::CanSendRecordRequest> {
  using Result = standard::CanSendRecordData;
  static constexpr auto action = standard_detail::NativeAction::CanSendRecord;
  static constexpr auto decode = &standard_detail::decode_can_send_record_data;
};
template <> struct ServiceOperation<standard::SetRestartRequest> {
  using Result = standard::ServiceNoData;
  static constexpr auto action = standard_detail::NativeAction::SetRestart;
  static constexpr auto decode = &standard_detail::decode_set_restart_data;
};
template <> struct ServiceOperation<standard::CleanCacheRequest> {
  using Result = standard::ServiceNoData;
  static constexpr auto action = standard_detail::NativeAction::CleanCache;
  static constexpr auto decode = &standard_detail::decode_clean_cache_data;
};
template <typename Request>
concept OrdinaryService =
    requires { typename ServiceOperation<Request>::Result; };
template <OrdinaryService Request>
using ServiceOutcome =
    standard::ExecutionOutcome<typename ServiceOperation<Request>::Result>;
template <OrdinaryService Request> struct PreparedService {
  std::shared_ptr<const PlatformDomainBinding> binding_owner;
  const Request request;
  std::shared_ptr<OneBot11TransportCapability> transport_owner;
  std::shared_ptr<SelfIdentityCache> identity_owner;
  std::shared_ptr<standard_detail::StandardCallState> call;
  const standard_detail::NativeAction native_action;
  const standard::ExecutionMode mode;
  const standard::ValueLimits values;
  const std::uint64_t echo;
  const bot::BotInstallationRef installation;
  const bot::ActionId action;
  standard_detail::CapturedStandardSend captured;
};

auto ready_service(StructuredOperationCompletion completion)
    -> boost::asio::awaitable<StructuredOperationCompletion> {
  co_return std::move(completion);
}

auto reject_service(bot::BotOperationErrorCode code, std::string message)
    -> boost::asio::awaitable<StructuredOperationCompletion> {
  return ready_service(
      {bot::StructuredOperationReply::failure(
           {.code = code,
            .message = std::move(message),
            .retryable = false,
            .submission_safety =
                bot::SubmissionSafety::DefinitelyNotSubmitted}),
       StructuredCompletionDisposition::Failed});
}

template <OrdinaryService Request>
auto service_failure(const PreparedService<Request> &prepared,
                     bot::BotOperationErrorCode code,
                     standard::FailureStage stage, std::string message)
    -> ServiceOutcome<Request> {
  const auto evidence = prepared.call->evidence();
  return {standard::ExecutionFailure{
              {.code = code,
               .message = std::move(message),
               .retryable = false,
               .submission_safety =
                   evidence.submission == standard::Submission::NotSubmitted
                       ? bot::SubmissionSafety::DefinitelyNotSubmitted
                       : bot::SubmissionSafety::PossiblySubmitted},
              stage},
          evidence};
}

template <OrdinaryService Request>
auto service_completion(const PreparedService<Request> &prepared,
                        ServiceOutcome<Request> outcome)
    -> StructuredOperationCompletion {
  bot::OperationTraits<Request>::validate_result(prepared.request, outcome);
  const auto disposition = standard_detail::completion_disposition(outcome);
  StructuredCompletionDisposition completion;
  switch (disposition) {
  case standard_detail::CompletionDisposition::Completed:
    completion = StructuredCompletionDisposition::Completed;
    break;
  case standard_detail::CompletionDisposition::AcceptedPending:
    completion = StructuredCompletionDisposition::AcceptedPending;
    break;
  case standard_detail::CompletionDisposition::Failed:
    completion = StructuredCompletionDisposition::Failed;
    break;
  }
  return {
      bot::StructuredOperationReply::success(bot::StructuredOperationValue::own(
          prepared.installation, prepared.action,
          std::make_unique<ServiceOutcome<Request>>(std::move(outcome)))),
      completion};
}

template <OrdinaryService Request>
auto service_transport_failure(
    const PreparedService<Request> &prepared,
    standard_detail::StandardTransportFailureKind kind)
    -> ServiceOutcome<Request> {
  using Code = bot::BotOperationErrorCode;
  using Kind = standard_detail::StandardTransportFailureKind;
  const bool not_submitted = prepared.call->evidence().submission ==
                             standard::Submission::NotSubmitted;
  auto code = not_submitted ? Code::TransportFailure : Code::OutcomeUnknown;
  switch (kind) {
  case Kind::Cancelled:
  case Kind::DeadlineExceeded:
    code = Code::Cancelled;
    break;
  case Kind::ConnectionRetired:
  case Kind::TransportError:
    break;
  case Kind::ResponseTooLarge:
  case Kind::InvalidResponse:
    code = not_submitted ? Code::InvalidRequest : Code::MalformedResponse;
    break;
  }
  return service_failure(prepared, code, standard::FailureStage::Transport,
                         "standard service transport did not yield a reply");
}

auto service_echo_matches(const bot::Json &reply, std::uint64_t echo) -> bool {
  const auto found = reply.find("echo");
  if (found == reply.end() || !found->is_string())
    return false;
  char digits[20];
  const auto [end, error] =
      std::to_chars(digits, digits + sizeof(digits), echo);
  if (error != std::errc{})
    return false;
  constexpr std::string_view prefix = "obcx.standard:";
  const std::string_view actual = found->get_ref<const std::string &>();
  return actual.starts_with(prefix) &&
         actual.substr(prefix.size()) == std::string_view(digits, end);
}

template <OrdinaryService Request>
auto project_service_reply(const PreparedService<Request> &prepared,
                           const standard_detail::StandardWireReply &wire)
    -> ServiceOutcome<Request> {
  using Code = bot::BotOperationErrorCode;
  using Kind = standard_detail::NativeReplyKind;
  // Parse the entire bounded frame before association or header observation.
  const auto reply = standard_detail::parse_standard_reply(
      wire.body, prepared.call->limits(), prepared.values);
  if (prepared.captured.transport ==
          standard_detail::StandardWireTransport::WebSocket &&
      (wire.http_status || !service_echo_matches(reply, prepared.echo)))
    return service_failure(prepared, Code::MalformedResponse,
                           standard::FailureStage::NativeReply,
                           "standard service reply association is invalid");
  const auto disposition = standard_detail::classify_native_reply(
      reply, prepared.native_action, prepared.mode, prepared.values);
  if (disposition.kind == Kind::Malformed)
    return service_failure(prepared, Code::MalformedResponse,
                           standard::FailureStage::NativeReply,
                           "standard service reply header is invalid");
  prepared.call->observe(disposition);
  // Header evidence must survive every later status/data/projection failure.
  if (disposition.kind == Kind::Rejected)
    return service_failure(prepared, Code::ProviderRejected,
                           standard::FailureStage::NativeReply,
                           "standard service was rejected");
  if (prepared.captured.transport ==
          standard_detail::StandardWireTransport::Http &&
      wire.http_status != 200)
    return service_failure(prepared, Code::MalformedResponse,
                           standard::FailureStage::NativeReply,
                           "standard service HTTP status is inconsistent");
  const auto data = reply.find("data");
  if (data == reply.end())
    return service_failure(prepared, Code::MalformedResponse,
                           standard::FailureStage::NativeReply,
                           "standard service reply data is missing");
  if (disposition.kind == Kind::AcceptedPending) {
    if (!data->is_null())
      return service_failure(prepared, Code::MalformedResponse,
                             standard::FailureStage::NativeReply,
                             "standard service pending data is invalid");
    return {
        standard::AcceptedPending{prepared.mode, *disposition.provider_retcode},
        prepared.call->evidence()};
  }
  // The original binding owner survives await; never decode with a new account
  // or a codec borrowed from an Endpoint whose lifetime has ended.
  return {standard::Completed<typename ServiceOperation<Request>::Result>{
              ServiceOperation<Request>::decode(*data, prepared.values)},
          prepared.call->evidence()};
}

template <OrdinaryService Request>
auto finish_service(std::shared_ptr<PreparedService<Request>> prepared)
    -> boost::asio::awaitable<StructuredOperationCompletion> {
  using Failure = standard_detail::StandardTransportFailureKind;
  try {
    co_await std::move(prepared->captured.operation);
  } catch (const boost::system::system_error &error) {
    // An already-won reply/failure takes precedence over a late exception.
    if (!prepared->call->terminal())
      (void)prepared->call->try_fail(
          {error.code() == boost::asio::error::operation_aborted
               ? Failure::Cancelled
               : Failure::TransportError,
           error.code()});
  } catch (...) {
    if (!prepared->call->terminal())
      (void)prepared->call->try_fail({Failure::TransportError, {}});
  }
  try {
    if (!prepared->call->terminal())
      (void)prepared->call->try_fail({Failure::TransportError, {}});
    // One consumption only; the checkpoint owner remains alive afterwards.
    auto result = prepared->call->take_result();
    if (!result)
      co_return service_completion(
          *prepared,
          service_transport_failure(*prepared, Failure::TransportError));
    if (const auto *failure =
            std::get_if<standard_detail::StandardTransportFailure>(&*result))
      co_return service_completion(
          *prepared, service_transport_failure(*prepared, failure->kind));
    std::optional<ServiceOutcome<Request>> outcome;
    try {
      outcome.emplace(project_service_reply(
          *prepared, std::get<standard_detail::StandardWireReply>(*result)));
    } catch (...) {
      outcome.emplace(service_failure(
          *prepared, bot::BotOperationErrorCode::MalformedResponse,
          standard::FailureStage::NativeReply,
          "standard service reply failed validation"));
    }
    co_return service_completion(*prepared, std::move(*outcome));
  } catch (...) {
    // NotSubmitted cannot legally be a Projection failure. Keep authoritative
    // evidence unchanged, including any native header already observed.
    const bool not_submitted = prepared->call->evidence().submission ==
                               standard::Submission::NotSubmitted;
    co_return service_completion(
        *prepared,
        service_failure(*prepared,
                        not_submitted
                            ? bot::BotOperationErrorCode::TransportFailure
                            : bot::BotOperationErrorCode::MalformedResponse,
                        not_submitted ? standard::FailureStage::Transport
                                      : standard::FailureStage::Projection,
                        "standard service result projection failed"));
  }
}

template <typename Request> struct MediaOperation;
template <> struct MediaOperation<standard::GetImageRequest> {
  using Result = standard::ImageFileData;
  static auto decode(const bot::Json &data,
                     const standard_detail::MediaResultContext &context)
      -> Result {
    return standard_detail::decode_get_image_data(data, context.values);
  }
};
template <> struct MediaOperation<standard::GetRecordRequest> {
  using Result = standard::ConvertedRecordData;
  static auto decode(const bot::Json &data,
                     const standard_detail::MediaResultContext &context)
      -> Result {
    return standard_detail::decode_get_record_data(
        data, context.requested_format.value(), context.values);
  }
};
template <typename Request>
concept OrdinaryMedia = requires { typename MediaOperation<Request>::Result; };
template <OrdinaryMedia Request>
using MediaOutcome =
    standard::ExecutionOutcome<typename MediaOperation<Request>::Result>;
template <OrdinaryMedia Request> struct PreparedMedia {
  std::shared_ptr<const PlatformDomainBinding> binding_owner;
  const standard_detail::MediaResultContext context;
  std::shared_ptr<OneBot11TransportCapability> transport_owner;
  std::shared_ptr<SelfIdentityCache> identity_owner;
  std::shared_ptr<standard_detail::StandardCallState> call;
  const standard_detail::NativeAction native_action;
  const standard::ExecutionMode mode;
  const standard::ValueLimits values;
  const std::uint64_t echo;
  const bot::BotInstallationRef installation;
  const bot::ActionId action;
  standard_detail::CapturedStandardSend captured;
};

auto ready_media(StructuredOperationCompletion completion)
    -> boost::asio::awaitable<StructuredOperationCompletion> {
  co_return std::move(completion);
}

auto reject_media(bot::BotOperationErrorCode code, std::string message)
    -> boost::asio::awaitable<StructuredOperationCompletion> {
  return ready_media({bot::StructuredOperationReply::failure(
                          {.code = code,
                           .message = std::move(message),
                           .retryable = false,
                           .submission_safety =
                               bot::SubmissionSafety::DefinitelyNotSubmitted}),
                      StructuredCompletionDisposition::Failed});
}

template <OrdinaryMedia Request>
auto media_failure(const PreparedMedia<Request> &prepared,
                   bot::BotOperationErrorCode code,
                   standard::FailureStage stage, std::string message)
    -> MediaOutcome<Request> {
  const auto evidence = prepared.call->evidence();
  return {standard::ExecutionFailure{
              {.code = code,
               .message = std::move(message),
               .retryable = false,
               .submission_safety =
                   evidence.submission == standard::Submission::NotSubmitted
                       ? bot::SubmissionSafety::DefinitelyNotSubmitted
                       : bot::SubmissionSafety::PossiblySubmitted},
              stage},
          evidence};
}

template <OrdinaryMedia Request>
auto media_completion(const PreparedMedia<Request> &prepared,
                      MediaOutcome<Request> outcome)
    -> StructuredOperationCompletion {
  standard_detail::validate_media_result(prepared.context, outcome);
  const auto disposition = standard_detail::completion_disposition(outcome);
  StructuredCompletionDisposition completion;
  switch (disposition) {
  case standard_detail::CompletionDisposition::Completed:
    completion = StructuredCompletionDisposition::Completed;
    break;
  case standard_detail::CompletionDisposition::AcceptedPending:
    completion = StructuredCompletionDisposition::AcceptedPending;
    break;
  case standard_detail::CompletionDisposition::Failed:
    completion = StructuredCompletionDisposition::Failed;
    break;
  }
  return {
      bot::StructuredOperationReply::success(bot::StructuredOperationValue::own(
          prepared.installation, prepared.action,
          std::make_unique<MediaOutcome<Request>>(std::move(outcome)))),
      completion};
}

template <OrdinaryMedia Request>
auto media_transport_failure(const PreparedMedia<Request> &prepared,
                             standard_detail::StandardTransportFailureKind kind)
    -> MediaOutcome<Request> {
  using Code = bot::BotOperationErrorCode;
  using Kind = standard_detail::StandardTransportFailureKind;
  const bool not_submitted = prepared.call->evidence().submission ==
                             standard::Submission::NotSubmitted;
  auto code = not_submitted ? Code::TransportFailure : Code::OutcomeUnknown;
  switch (kind) {
  case Kind::Cancelled:
  case Kind::DeadlineExceeded:
    code = Code::Cancelled;
    break;
  case Kind::ConnectionRetired:
  case Kind::TransportError:
    break;
  case Kind::ResponseTooLarge:
  case Kind::InvalidResponse:
    code = not_submitted ? Code::InvalidRequest : Code::MalformedResponse;
    break;
  }
  return media_failure(prepared, code, standard::FailureStage::Transport,
                       "standard media transport did not yield a reply");
}

auto media_echo_matches(const bot::Json &reply, std::uint64_t echo) -> bool {
  const auto found = reply.find("echo");
  if (found == reply.end() || !found->is_string())
    return false;
  char digits[20];
  const auto [end, error] =
      std::to_chars(digits, digits + sizeof(digits), echo);
  if (error != std::errc{})
    return false;
  constexpr std::string_view prefix = "obcx.standard:";
  const std::string_view actual = found->get_ref<const std::string &>();
  return actual.starts_with(prefix) &&
         actual.substr(prefix.size()) == std::string_view(digits, end);
}

template <OrdinaryMedia Request>
auto project_media_reply(const PreparedMedia<Request> &prepared,
                         const standard_detail::StandardWireReply &wire)
    -> MediaOutcome<Request> {
  using Code = bot::BotOperationErrorCode;
  using Kind = standard_detail::NativeReplyKind;
  // Parse the entire bounded frame before association or header observation.
  const auto reply = standard_detail::parse_standard_reply(
      wire.body, prepared.call->limits(), prepared.values);
  if (prepared.captured.transport ==
          standard_detail::StandardWireTransport::WebSocket &&
      (wire.http_status || !media_echo_matches(reply, prepared.echo)))
    return media_failure(prepared, Code::MalformedResponse,
                         standard::FailureStage::NativeReply,
                         "standard media reply association is invalid");
  const auto disposition = standard_detail::classify_native_reply(
      reply, prepared.native_action, prepared.mode, prepared.values);
  if (disposition.kind == Kind::Malformed)
    return media_failure(prepared, Code::MalformedResponse,
                         standard::FailureStage::NativeReply,
                         "standard media reply header is invalid");
  prepared.call->observe(disposition);
  // Header evidence must survive every later status/data/projection failure.
  if (disposition.kind == Kind::Rejected)
    return media_failure(prepared, Code::ProviderRejected,
                         standard::FailureStage::NativeReply,
                         "standard media was rejected");
  if (prepared.captured.transport ==
          standard_detail::StandardWireTransport::Http &&
      wire.http_status != 200)
    return media_failure(prepared, Code::MalformedResponse,
                         standard::FailureStage::NativeReply,
                         "standard media HTTP status is inconsistent");
  const auto data = reply.find("data");
  if (data == reply.end())
    return media_failure(prepared, Code::MalformedResponse,
                         standard::FailureStage::NativeReply,
                         "standard media reply data is missing");
  if (disposition.kind == Kind::AcceptedPending) {
    if (!data->is_null())
      return media_failure(prepared, Code::MalformedResponse,
                           standard::FailureStage::NativeReply,
                           "standard media pending data is invalid");
    return {
        standard::AcceptedPending{prepared.mode, *disposition.provider_retcode},
        prepared.call->evidence()};
  }
  // The original binding owner survives await; never decode with a new account
  // or a codec borrowed from an Endpoint whose lifetime has ended.
  return {standard::Completed<typename MediaOperation<Request>::Result>{
              MediaOperation<Request>::decode(*data, prepared.context)},
          prepared.call->evidence()};
}

template <OrdinaryMedia Request>
auto finish_media(std::shared_ptr<PreparedMedia<Request>> prepared)
    -> boost::asio::awaitable<StructuredOperationCompletion> {
  using Failure = standard_detail::StandardTransportFailureKind;
  try {
    co_await std::move(prepared->captured.operation);
  } catch (const boost::system::system_error &error) {
    // An already-won reply/failure takes precedence over a late exception.
    if (!prepared->call->terminal())
      (void)prepared->call->try_fail(
          {error.code() == boost::asio::error::operation_aborted
               ? Failure::Cancelled
               : Failure::TransportError,
           error.code()});
  } catch (...) {
    if (!prepared->call->terminal())
      (void)prepared->call->try_fail({Failure::TransportError, {}});
  }
  try {
    if (!prepared->call->terminal())
      (void)prepared->call->try_fail({Failure::TransportError, {}});
    // One consumption only; the checkpoint owner remains alive afterwards.
    auto result = prepared->call->take_result();
    if (!result)
      co_return media_completion(
          *prepared,
          media_transport_failure(*prepared, Failure::TransportError));
    if (const auto *failure =
            std::get_if<standard_detail::StandardTransportFailure>(&*result))
      co_return media_completion(
          *prepared, media_transport_failure(*prepared, failure->kind));
    std::optional<MediaOutcome<Request>> outcome;
    try {
      outcome.emplace(project_media_reply(
          *prepared, std::get<standard_detail::StandardWireReply>(*result)));
    } catch (...) {
      outcome.emplace(media_failure(
          *prepared, bot::BotOperationErrorCode::MalformedResponse,
          standard::FailureStage::NativeReply,
          "standard media reply failed validation"));
    }
    co_return media_completion(*prepared, std::move(*outcome));
  } catch (...) {
    // NotSubmitted cannot legally be a Projection failure. Keep authoritative
    // evidence unchanged, including any native header already observed.
    const bool not_submitted = prepared->call->evidence().submission ==
                               standard::Submission::NotSubmitted;
    co_return media_completion(
        *prepared,
        media_failure(*prepared,
                      not_submitted
                          ? bot::BotOperationErrorCode::TransportFailure
                          : bot::BotOperationErrorCode::MalformedResponse,
                      not_submitted ? standard::FailureStage::Transport
                                    : standard::FailureStage::Projection,
                      "standard media result projection failed"));
  }
}

// Common media.read owns one origin and deadline across native metadata and
// credential-isolated HTTP acquisition. No Endpoint/request reference survives
// suspension, and no later stage captures a replacement account origin.
// Private current get_image projection, not an SDK operation or wire DTO.
// Provider metadata aliases from the retired image facade are not accepted.
struct NativeImageMetadata {
  std::string file_path;
  std::string url;
  std::string file_name;
  std::optional<std::uint64_t> file_size;
  void validate() const {
    if (file_path.empty() && url.empty())
      throw std::invalid_argument("QQ image has no locator");
    if (!file_path.empty())
      bot::detail::validate_identifier(file_path, "QQ image server path",
                                       16384);
    if (!url.empty() && !network::is_valid_media_http_url(url))
      throw std::invalid_argument("invalid QQ image URL");
    if (!file_name.empty())
      bot::detail::validate_identifier(file_name, "QQ image filename", 1024);
  }
};
void validate_native_image_file(std::string_view file) {
  bot::detail::validate_identifier(file, "QQ image selector", 1024);
  if (file == "." || file == ".." ||
      file.find_first_of("/\\:?#&=%") != std::string_view::npos)
    throw std::invalid_argument("QQ image selector is not a native filename");
}
auto native_image_metadata(const bot::Json &document) -> NativeImageMetadata {
  if (!document.is_object())
    throw std::invalid_argument("invalid QQ image response");
  const auto text = [&](const char *key) -> std::string {
    const auto field = document.find(key);
    return field == document.end() ? std::string{} : field->get<std::string>();
  };
  std::optional<std::uint64_t> size;
  if (const auto value = document.find("file_size");
      value != document.end() && !value->is_null()) {
    if (value->is_number_unsigned())
      size = value->get<std::uint64_t>();
    else if (value->is_number_integer() && value->get<std::int64_t>() >= 0)
      size = static_cast<std::uint64_t>(value->get<std::int64_t>());
    else if (value->is_string()) {
      const auto &input = value->get_ref<const std::string &>();
      std::uint64_t parsed;
      const auto [end, error] =
          std::from_chars(input.data(), input.data() + input.size(), parsed);
      if (input.empty() || error != std::errc{} ||
          end != input.data() + input.size())
        throw std::invalid_argument("invalid QQ image size");
      size = parsed;
    } else
      throw std::invalid_argument("invalid QQ image size");
  }
  NativeImageMetadata result{text("file"), text("url"), text("file_name"),
                             size};
  result.validate();
  return result;
}
struct NativeImageReadWork {
  std::shared_ptr<OneBot11TransportCapability> transport_owner;
  std::shared_ptr<SelfIdentityCache> identity;
  SelfIdentityOrigin origin;
  std::shared_ptr<standard_detail::StandardCallState> call;
  standard_detail::CapturedStandardSend captured;
  standard::ValueLimits values;
  std::uint64_t echo;
  std::uint64_t maximum_bytes;
};
using MediaReadResult = bot::BotOperationResult<bot::MediaBlob>;
auto ready_media_read(MediaReadResult result)
    -> boost::asio::awaitable<MediaReadResult> {
  co_return std::move(result);
}
auto finish_native_image_read(std::shared_ptr<NativeImageReadWork> work)
    -> boost::asio::awaitable<MediaReadResult> {
  using Code = bot::BotOperationErrorCode;
  using Failure = standard_detail::StandardTransportFailureKind;
  const auto fail = [](Code code, const char *message) {
    return bot::failed_operation<bot::MediaBlob>(code, message);
  };
  const auto eligible = [identity = work->identity, origin = work->origin] {
    return identity->observe_user_and_capture(origin, std::nullopt, [] {})
        .accepted();
  };
  try {
    const auto cancellation =
        co_await boost::asio::this_coro::cancellation_state;
    const auto stage_failure = [&]() -> std::optional<MediaReadResult> {
      if (!eligible())
        return fail(Code::CacheUnavailable, "QQ image read origin retired");
      if (cancellation.cancelled() != boost::asio::cancellation_type::none)
        return fail(Code::Cancelled, "QQ image read cancelled");
      if (std::chrono::steady_clock::now() >= work->call->deadline())
        return fail(Code::TransportFailure, "QQ image read deadline expired");
      return std::nullopt;
    };
    if (auto failure = stage_failure())
      co_return std::move(*failure);
    try {
      co_await std::move(work->captured.operation);
    } catch (const boost::system::system_error &error) {
      if (!work->call->terminal())
        (void)work->call->try_fail(
            {error.code() == boost::asio::error::operation_aborted
                 ? Failure::Cancelled
                 : Failure::TransportError,
             error.code()});
    } catch (...) {
      if (!work->call->terminal())
        (void)work->call->try_fail({Failure::TransportError, {}});
    }
    if (auto failure = stage_failure())
      co_return std::move(*failure);
    std::optional<NativeImageMetadata> image;
    {
      auto result = work->call->take_result();
      if (!result)
        co_return fail(Code::TransportFailure,
                       "QQ image metadata outcome unavailable");
      if (const auto *failure =
              std::get_if<standard_detail::StandardTransportFailure>(
                  &*result)) {
        const auto code = failure->kind == Failure::Cancelled ? Code::Cancelled
                          : failure->kind == Failure::ConnectionRetired
                              ? Code::CacheUnavailable
                          : failure->kind == Failure::ResponseTooLarge
                              ? Code::MediaTooLarge
                          : failure->kind == Failure::InvalidResponse
                              ? Code::MalformedResponse
                              : Code::TransportFailure;
        co_return fail(code, "QQ image metadata transport failed");
      }
      const auto &wire = std::get<standard_detail::StandardWireReply>(*result);
      try {
        const auto reply = standard_detail::parse_standard_reply(
            wire.body, work->call->limits(), work->values);
        if (work->captured.transport ==
                standard_detail::StandardWireTransport::WebSocket &&
            (wire.http_status || !media_echo_matches(reply, work->echo)))
          co_return fail(Code::MalformedResponse,
                         "QQ image metadata association invalid");
        const auto disposition = standard_detail::classify_native_reply(
            reply, standard_detail::NativeAction::GetImage,
            standard::ExecutionMode::Direct, work->values);
        if (disposition.kind == standard_detail::NativeReplyKind::Rejected) {
          auto error = fail(Code::ProviderRejected,
                            "QQ image metadata provider rejected");
          error.error->provider_code =
              std::to_string(*disposition.provider_retcode);
          error.error->retryable = *disposition.provider_retcode < 0;
          co_return error;
        }
        if (disposition.kind != standard_detail::NativeReplyKind::Completed ||
            (work->captured.transport ==
                 standard_detail::StandardWireTransport::Http &&
             wire.http_status != 200))
          co_return fail(Code::MalformedResponse,
                         "QQ image metadata response invalid");
        image = native_image_metadata(reply.at("data"));
      } catch (...) {
        co_return fail(Code::MalformedResponse,
                       "QQ image metadata failed validation");
      }
    } // Release native response/tree before downloading media bytes.
    if (image->url.empty())
      co_return fail(Code::UnsupportedAction,
                     "QQ image has no supported HTTP download locator");
    if (image->file_size && *image->file_size > work->maximum_bytes)
      co_return fail(Code::MediaTooLarge, "QQ image exceeds byte budget");
    if (auto failure = stage_failure())
      co_return std::move(*failure);
    auto response = co_await network::detail::fetch_media_until(
        image->url, work->maximum_bytes, work->call->deadline(), eligible);
    if (auto failure = stage_failure())
      co_return std::move(*failure);
    if (response.error == boost::asio::error::message_size)
      co_return fail(Code::MediaTooLarge, "QQ image exceeds byte budget");
    if (response.error == boost::asio::error::operation_aborted)
      co_return fail(Code::Cancelled, "QQ image read cancelled");
    if (response.error || !response.response_complete)
      co_return fail(Code::TransportFailure, "QQ image download failed");
    if (response.status_code != 200)
      co_return fail(Code::ProviderRejected,
                     "QQ image HTTP resource is inaccessible");
    if (response.body.size() > work->maximum_bytes)
      co_return fail(Code::MediaTooLarge, "QQ image exceeds byte budget");
    if (response.body.empty() ||
        (image->file_size && *image->file_size != response.body.size()))
      co_return fail(Code::MalformedResponse,
                     "QQ image size does not match file metadata");
    bot::BytesSource bytes{{response.body.begin(), response.body.end()}, {}};
    bytes.metadata.size_bytes = bytes.bytes.size();
    const auto &name = image->file_name;
    if (!name.empty() && name != "." && name != ".." &&
        name.find_first_of("/\\:?#") == std::string::npos)
      bytes.metadata.filename = name;
    bytes.validate();
    if (auto failure = stage_failure())
      co_return std::move(*failure);
    co_return MediaReadResult::success(
        {std::move(bytes), bot::MediaKind::Image, std::nullopt});
  } catch (const boost::system::system_error &error) {
    co_return fail(!eligible() ? Code::CacheUnavailable
                   : error.code() == boost::asio::error::operation_aborted
                       ? Code::Cancelled
                       : Code::TransportFailure,
                   "QQ image read interrupted");
  } catch (...) {
    co_return fail(eligible() ? Code::TransportFailure : Code::CacheUnavailable,
                   "QQ image read failed");
  }
}

struct NativeFileReadWork {
  std::shared_ptr<OneBot11TransportCapability> transport_owner;
  std::shared_ptr<SelfIdentityCache> identity;
  SelfIdentityOrigin origin;
  std::shared_ptr<standard_detail::StandardCallState> call;
  standard_detail::CapturedStandardSend captured;
  standard::ValueLimits values;
  std::uint64_t echo;
  std::size_t maximum_bytes;
};
auto finish_native_file_read(std::shared_ptr<NativeFileReadWork> work)
    -> boost::asio::awaitable<MediaReadResult> {
  using Code = bot::BotOperationErrorCode;
  using Failure = standard_detail::StandardTransportFailureKind;
  const auto fail = [](Code code, const char *message) {
    return bot::failed_operation<bot::MediaBlob>(code, message);
  };
  const auto eligible = [identity = work->identity,
                         origin = work->origin]() noexcept {
    try {
      return identity->observe_user_and_capture(origin, std::nullopt, [] {})
          .accepted();
    } catch (...) {
      return false;
    }
  };
  try {
    const auto cancellation =
        co_await boost::asio::this_coro::cancellation_state;
    const auto stage_failure = [&]() -> std::optional<MediaReadResult> {
      if (!eligible())
        return fail(Code::CacheUnavailable, "QQ file read origin retired");
      if (cancellation.cancelled() != boost::asio::cancellation_type::none)
        return fail(Code::Cancelled, "QQ file read cancelled");
      if (std::chrono::steady_clock::now() >= work->call->deadline())
        return fail(Code::TransportFailure, "QQ file read deadline expired");
      return std::nullopt;
    };
    if (auto failure = stage_failure())
      co_return std::move(*failure);
    try {
      co_await std::move(work->captured.operation);
    } catch (const boost::system::system_error &error) {
      if (!work->call->terminal())
        (void)work->call->try_fail(
            {error.code() == boost::asio::error::operation_aborted
                 ? Failure::Cancelled
                 : Failure::TransportError,
             error.code()});
    } catch (...) {
      if (!work->call->terminal())
        (void)work->call->try_fail({Failure::TransportError, {}});
    }
    if (auto failure = stage_failure())
      co_return std::move(*failure);
    std::string url;
    std::optional<std::uint64_t> declared_size;
    {
      auto result = work->call->take_result();
      if (!result)
        co_return fail(Code::TransportFailure,
                       "QQ file query outcome unavailable");
      if (const auto *failure =
              std::get_if<standard_detail::StandardTransportFailure>(
                  &*result)) {
        const auto code = failure->kind == Failure::Cancelled ? Code::Cancelled
                          : failure->kind == Failure::ConnectionRetired
                              ? Code::CacheUnavailable
                          : failure->kind == Failure::ResponseTooLarge
                              ? Code::MediaTooLarge
                          : failure->kind == Failure::InvalidResponse
                              ? Code::MalformedResponse
                              : Code::TransportFailure;
        co_return fail(code, "QQ file query transport failed");
      }
      const auto &wire = std::get<standard_detail::StandardWireReply>(*result);
      try {
        const auto reply = standard_detail::parse_standard_reply(
            wire.body, work->call->limits(), work->values);
        if (work->captured.transport ==
                standard_detail::StandardWireTransport::WebSocket &&
            (wire.http_status || !media_echo_matches(reply, work->echo)))
          co_return fail(Code::MalformedResponse,
                         "QQ file query association invalid");
        // This is a named vendor read, NOT GetImage or a new standard action.
        const auto &status = reply.at("status");
        const auto &retcode = reply.at("retcode");
        if (!status.is_string() || !retcode.is_number_integer() ||
            (retcode.is_number_unsigned() &&
             retcode.get<std::uint64_t>() >
                 static_cast<std::uint64_t>(
                     std::numeric_limits<std::int64_t>::max())))
          throw std::invalid_argument("invalid QQ file query header");
        const auto code = retcode.get<std::int64_t>();
        if (status == "failed" && code != 0) {
          auto error =
              fail(Code::ProviderRejected, "QQ file query provider rejected");
          error.error->provider_code = std::to_string(code);
          error.error->retryable = code < 0;
          co_return error;
        }
        if (status != "ok" || code != 0 ||
            (work->captured.transport ==
                 standard_detail::StandardWireTransport::Http &&
             wire.http_status != 200))
          throw std::invalid_argument("QQ file query is not a completed read");
        const auto &data = reply.at("data");
        const auto &locator = data.at("url");
        if (!locator.is_string() ||
            locator.get_ref<const std::string &>().size() > 8192)
          throw std::invalid_argument("invalid QQ file locator");
        url = locator.get<std::string>();
        if (url.empty() ||
            (!url.starts_with("http://") && !url.starts_with("https://")))
          co_return fail(Code::UnsupportedAction,
                         "QQ file has no HTTP read locator");
        bot::HttpSource{url, {}}.validate();
        for (const auto key : {"file_size", "size"}) {
          const auto field = data.find(key);
          if (field == data.end() || field->is_null())
            continue;
          std::uint64_t size;
          if (field->is_string()) {
            const auto &text = field->get_ref<const std::string &>();
            if (text.empty() || text.size() > 20)
              throw std::invalid_argument("invalid QQ file size");
            const auto [end, ec] =
                std::from_chars(text.data(), text.data() + text.size(), size);
            if (ec != std::errc{} || end != text.data() + text.size())
              throw std::invalid_argument("invalid QQ file size");
          } else {
            size = bot::detail::bounded_unsigned(
                *field, std::numeric_limits<std::uint64_t>::max());
          }
          if (declared_size && *declared_size != size)
            throw std::invalid_argument("conflicting QQ file size");
          declared_size = size;
        }
      } catch (...) {
        co_return fail(Code::MalformedResponse,
                       "QQ file query failed validation");
      }
    } // Drop bounded native response/tree before acquiring file bytes.
    if (declared_size && *declared_size > work->maximum_bytes)
      co_return fail(Code::MediaTooLarge, "QQ file exceeds byte budget");
    if (auto failure = stage_failure())
      co_return std::move(*failure);
    auto response = co_await network::detail::fetch_media_until(
        url, work->maximum_bytes, work->call->deadline(), eligible);
    if (auto failure = stage_failure())
      co_return std::move(*failure);
    if (response.error == boost::asio::error::message_size ||
        response.body.size() > work->maximum_bytes)
      co_return fail(Code::MediaTooLarge, "QQ file exceeds byte budget");
    if (response.error == boost::asio::error::operation_aborted)
      co_return fail(Code::Cancelled, "QQ file read cancelled");
    if (response.error || !response.response_complete)
      co_return fail(Code::TransportFailure, "QQ file download failed");
    if (response.status_code != 200)
      co_return fail(Code::ProviderRejected,
                     "QQ file HTTP resource is inaccessible");
    if (response.body.empty() ||
        (declared_size && *declared_size != response.body.size()))
      co_return fail(Code::MalformedResponse,
                     "QQ file size declaration mismatch or empty body");
    bot::BytesSource bytes{{response.body.begin(), response.body.end()}, {}};
    bytes.metadata.size_bytes = bytes.bytes.size();
    bytes.validate();
    if (auto failure = stage_failure())
      co_return std::move(*failure);
    co_return MediaReadResult::success(
        {std::move(bytes), bot::MediaKind::File, std::nullopt});
  } catch (const boost::system::system_error &error) {
    co_return fail(!eligible() ? Code::CacheUnavailable
                   : error.code() == boost::asio::error::operation_aborted
                       ? Code::Cancelled
                       : Code::TransportFailure,
                   "QQ file read interrupted");
  } catch (...) {
    co_return fail(eligible() ? Code::TransportFailure : Code::CacheUnavailable,
                   "QQ file read failed");
  }
}

// Count using the identical serializer and options used by Json::dump().
// The sink retains no output, and checks subtraction before every addition.
class ParameterCountingSink final
    : public nlohmann::detail::output_adapter_protocol<char> {
public:
  explicit ParameterCountingSink(std::size_t maximum) : maximum_(maximum) {}
  void write_character(char) override { add(1); }
  void write_characters(const char *, std::size_t count) override {
    add(count);
  }
  auto size() const -> std::size_t { return size_; }

private:
  void add(std::size_t count) {
    if (count > maximum_ - size_)
      throw std::invalid_argument("standard mutation parameters exceed limit");
    size_ += count;
  }
  const std::size_t maximum_;
  std::size_t size_{};
};
auto serialize_mutation_parameters(const bot::Json &parameters,
                                   std::size_t maximum_bytes) -> std::string {
  auto sink = std::make_shared<ParameterCountingSink>(maximum_bytes);
  nlohmann::detail::serializer<bot::Json> serializer(
      sink, ' ', bot::Json::error_handler_t::strict);
  serializer.dump(parameters, false, false, 0);
  auto serialized = parameters.dump();
  if (serialized.size() != sink->size() || serialized.size() > maximum_bytes)
    throw std::invalid_argument("standard mutation parameter size changed");
  return serialized;
}
} // namespace

class OneBot11OperationsComponent::Endpoint final {
public:
  explicit Endpoint(std::string installation_id,
                    std::shared_ptr<const PlatformDomainBinding> binding,
                    std::chrono::milliseconds action_timeout)
      : installation_{.installation_id = std::move(installation_id),
                      .surface = bot::SurfaceId{"onebot11.qq"}},
        binding_(std::move(binding)), action_timeout_(action_timeout) {
    installation_.validate();
  }

  void configure(std::shared_ptr<adapter::onebot11::ProtocolAdapter> protocol,
                 std::shared_ptr<OneBot11Transport> transport,
                 std::shared_ptr<SelfIdentityCache> self_identity,
                 std::shared_ptr<ReplyEvidenceStore> evidence) {
    if (protocol == nullptr || transport == nullptr || protocol_ != nullptr ||
        transport_ != nullptr) {
      throw BotComponentRuntimeError(
          "OneBot operations require one protocol and transport");
    }
    protocol_ = std::move(protocol);
    transport_ = std::move(transport);
    self_identity_ = std::move(self_identity);
    login_coordinator_ = std::make_shared<standard_detail::LoginCoordinator>(
        self_identity_, binding_, installation_);
    evidence_ = std::move(evidence);
    transport_->set_connection_callback(
        [weak = std::weak_ptr(self_identity_)](bool connected) {
          // Manager has already established/retired the originating native
          // client under its lifecycle lock. Cache generation owns evidence
          // invalidation.
          if (auto cache = weak.lock())
            cache->connection_changed(connected);
        });
  }

  void configure_received_media(OperationRegistry &operations,
                                std::shared_ptr<BotEventCapability> ingress) {
    auto binding =
        std::dynamic_pointer_cast<const onebot11::bot::QqDomainBinding>(
            binding_);
    if (!binding || received_media_)
      throw BotComponentRuntimeError("QQ live media requires one QQ binding");
    auto store = std::make_shared<onebot11::bot::detail::ReceivedMediaStore>(
        installation_, self_identity_, std::move(ingress), binding,
        bot::detail::domain_maximum_items, bot::detail::domain_maximum_bytes);
    operations.set_live_message_normalizer(
        [store, binding](
            const bot::BotInstallationRef &installation,
            const common::MessageEvent &event, bot::TimePoint received_at,
            std::string_view observation_id, const SelfIdentityOrigin &origin) {
          return store->normalize(*binding, installation, event, received_at,
                                  observation_id, origin);
        });
    received_media_ = std::move(store);
  }

  [[nodiscard]] auto installation() const -> bot::BotInstallationRef {
    return installation_;
  }

  [[nodiscard]] auto supports_standard_send_like() const -> bool;
  auto execute_standard_send_like(const standard::SendLikeRequest &request)
      -> boost::asio::awaitable<StructuredOperationCompletion>;

  [[nodiscard]] auto supports_standard_mutations() const -> bool;
  auto execute_standard_delete_msg(const standard::DeleteMsgRequest &request)
      -> boost::asio::awaitable<StructuredOperationCompletion>;
  auto execute_standard_set_group_kick(
      const standard::SetGroupKickRequest &request)
      -> boost::asio::awaitable<StructuredOperationCompletion>;
  auto execute_standard_set_group_anonymous_ban(
      const standard::SetGroupAnonymousBanRequest &request)
      -> boost::asio::awaitable<StructuredOperationCompletion>;
  auto execute_standard_set_friend_add_request(
      const standard::SetFriendAddRequestRequest &request)
      -> boost::asio::awaitable<StructuredOperationCompletion>;
  auto execute_standard_set_group_add_request(
      const standard::SetGroupAddRequestRequest &request)
      -> boost::asio::awaitable<StructuredOperationCompletion>;
  auto execute_standard_handle_quick_operation(
      const standard::HandleQuickOperationRequest &request)
      -> boost::asio::awaitable<StructuredOperationCompletion>;
  auto execute_standard_set_group_ban(
      const standard::SetGroupBanRequest &request)
      -> boost::asio::awaitable<StructuredOperationCompletion>;
  auto execute_standard_set_group_whole_ban(
      const standard::SetGroupWholeBanRequest &request)
      -> boost::asio::awaitable<StructuredOperationCompletion>;
  auto execute_standard_set_group_admin(
      const standard::SetGroupAdminRequest &request)
      -> boost::asio::awaitable<StructuredOperationCompletion>;
  auto execute_standard_set_group_anonymous(
      const standard::SetGroupAnonymousRequest &request)
      -> boost::asio::awaitable<StructuredOperationCompletion>;
  auto execute_standard_set_group_card(
      const standard::SetGroupCardRequest &request)
      -> boost::asio::awaitable<StructuredOperationCompletion>;
  auto execute_standard_set_group_name(
      const standard::SetGroupNameRequest &request)
      -> boost::asio::awaitable<StructuredOperationCompletion>;
  auto execute_standard_set_group_leave(
      const standard::SetGroupLeaveRequest &request)
      -> boost::asio::awaitable<StructuredOperationCompletion>;
  auto execute_standard_set_group_special_title(
      const standard::SetGroupSpecialTitleRequest &request)
      -> boost::asio::awaitable<StructuredOperationCompletion>;

  [[nodiscard]] auto supports_standard_send_messages() const -> bool;
  auto execute_standard_send_private_msg(
      const standard::SendPrivateMsgRequest &request)
      -> boost::asio::awaitable<StructuredOperationCompletion>;
  auto execute_standard_send_group_msg(
      const standard::SendGroupMsgRequest &request)
      -> boost::asio::awaitable<StructuredOperationCompletion>;
  auto execute_standard_send_msg(const standard::SendMsgRequest &request)
      -> boost::asio::awaitable<StructuredOperationCompletion>;

  [[nodiscard]] auto supports_standard_login() const -> bool;
  auto execute_standard_get_login_info(
      const standard::GetLoginInfoRequest &request)
      -> boost::asio::awaitable<StructuredOperationCompletion>;

  auto execute_standard_get_msg(const standard::GetMsgRequest &request)
      -> boost::asio::awaitable<StructuredOperationCompletion>;
  auto execute_standard_get_forward_msg(
      const standard::GetForwardMsgRequest &request)
      -> boost::asio::awaitable<StructuredOperationCompletion>;
  [[nodiscard]] auto supports_standard_queries() const -> bool;
  auto execute_standard_get_stranger_info(
      const standard::GetStrangerInfoRequest &request)
      -> boost::asio::awaitable<StructuredOperationCompletion>;
  auto execute_standard_get_friend_list(
      const standard::GetFriendListRequest &request)
      -> boost::asio::awaitable<StructuredOperationCompletion>;
  auto execute_standard_get_group_info(
      const standard::GetGroupInfoRequest &request)
      -> boost::asio::awaitable<StructuredOperationCompletion>;
  auto execute_standard_get_group_list(
      const standard::GetGroupListRequest &request)
      -> boost::asio::awaitable<StructuredOperationCompletion>;
  auto execute_standard_get_group_member_info(
      const standard::GetGroupMemberInfoRequest &request)
      -> boost::asio::awaitable<StructuredOperationCompletion>;
  auto execute_standard_get_group_member_list(
      const standard::GetGroupMemberListRequest &request)
      -> boost::asio::awaitable<StructuredOperationCompletion>;
  auto execute_standard_get_group_honor_info(
      const standard::GetGroupHonorInfoRequest &request)
      -> boost::asio::awaitable<StructuredOperationCompletion>;

  [[nodiscard]] auto supports_standard_services() const -> bool;
  auto execute_standard_get_image(const standard::GetImageRequest &request)
      -> boost::asio::awaitable<StructuredOperationCompletion>;

  auto execute_standard_get_record(const standard::GetRecordRequest &request)
      -> boost::asio::awaitable<StructuredOperationCompletion>;

  auto execute_standard_get_cookies(const standard::GetCookiesRequest &request)
      -> boost::asio::awaitable<StructuredOperationCompletion>;
  auto execute_standard_get_csrf_token(
      const standard::GetCsrfTokenRequest &request)
      -> boost::asio::awaitable<StructuredOperationCompletion>;
  auto execute_standard_get_credentials(
      const standard::GetCredentialsRequest &request)
      -> boost::asio::awaitable<StructuredOperationCompletion>;
  auto execute_standard_get_status(const standard::GetStatusRequest &request)
      -> boost::asio::awaitable<StructuredOperationCompletion>;
  auto execute_standard_get_version_info(
      const standard::GetVersionInfoRequest &request)
      -> boost::asio::awaitable<StructuredOperationCompletion>;
  auto execute_standard_can_send_image(
      const standard::CanSendImageRequest &request)
      -> boost::asio::awaitable<StructuredOperationCompletion>;
  auto execute_standard_can_send_record(
      const standard::CanSendRecordRequest &request)
      -> boost::asio::awaitable<StructuredOperationCompletion>;
  auto execute_standard_set_restart(const standard::SetRestartRequest &request)
      -> boost::asio::awaitable<StructuredOperationCompletion>;
  auto execute_standard_clean_cache(const standard::CleanCacheRequest &request)
      -> boost::asio::awaitable<StructuredOperationCompletion>;

  auto execute(const bot::GetSelfRequest &)
      -> boost::asio::awaitable<bot::BotOperationResult<bot::SelfInfo>> {
    co_return self_identity_->read();
  }

  auto fetch_self(SelfIdentityOrigin origin)
      -> boost::asio::awaitable<bot::BotOperationResult<bot::SelfInfo>> {
    const auto echo = next_echo();
    return login_coordinator_->refresh(
        transport_, protocol().serialize_get_self_info_request(echo), echo,
        std::move(origin));
  }

  auto selected_user_id(const onebot11::bot::UserTarget &target) const
      -> std::string {
    onebot11::bot::validate_user_target(target);
    if (onebot11::bot::user_target_installation(target) != installation_)
      throw std::invalid_argument("QQ user query installation mismatch");
    return std::visit(
        [&](const auto &value) -> std::string {
          using V = std::decay_t<decltype(value)>;
          if constexpr (std::same_as<V, bot::UserRef>) {
            codec().validate(value);
            return codec().unpack(value.id).components.front();
          } else {
            return std::to_string(value.user_id);
          }
        },
        target);
  }

  auto execute(const obcx::onebot11::bot::GetOneBotUserInfoRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<obcx::onebot11::bot::OneBotUserInfo>> {
    using Result = obcx::onebot11::bot::OneBotUserInfo;
    const auto selected = selected_user_id(request.user);
    const auto echo = next_echo();
    const auto response = co_await transport().send_action(
        protocol().serialize_get_user_info_request({}, selected,
                                                   request.no_cache, echo),
        echo);
    const auto parsed = parse_onebot11_operation_response(response, false);
    if (!parsed.ok()) {
      co_return provider_failure<Result>(parsed);
    }
    const auto user_id = numeric_identity(parsed_value(parsed), "user_id");
    if (!user_id || *user_id != selected) {
      co_return malformed_read<Result>(
          "OneBot user info response has mismatched user_id");
    }
    try {
      const auto &document = parsed_value(parsed);
      Result result{
          bot::UserRef{installation_,
                       codec().make<bot::UserId>("user", {*user_id})},
          optional_string(document, "nickname"),
          optional_string(document, "sex"),
          bot::detail::domain_decode<std::int64_t>(document.at("age"))};
      result.validate();
      co_return bot::BotOperationResult<Result>::success(std::move(result));
    } catch (const std::exception &) {
      co_return malformed_read<Result>(
          "OneBot user info response has invalid profile fields");
    }
  }

  auto execute(
      const onebot11::bot::SendOneBotGroupForwardMessageRequest &request)
      -> boost::asio::awaitable<bot::BotOperationResult<
          onebot11::bot::OneBotGroupForwardMessageResult>> {
    using Result = onebot11::bot::OneBotGroupForwardMessageResult;
    using Reply = bot::BotOperationResult<Result>;
    // Independent bounded checkpoint remains intact through all later wrapper
    // work; no throwing callback receives its sole owned copy.
    auto checkpoint =
        bot::send_failure(bot::detail::unknown_send_error(), {}, true);
    std::optional<bot::SendOutcomeEnvelope> reply_allowance;
    bool submitting = false;
    const auto deadline = std::chrono::steady_clock::now() + action_timeout_;
    try {
      request.validate();
      reply_allowance.emplace(onebot11::bot::forward_outcome::envelope());
      const auto group = group_id(request.target);
      for (const auto &node : request.nodes)
        if (std::holds_alternative<bot::MessageRef>(node))
          throw std::invalid_argument("opaque QQ forward reference nodes have "
                                      "no bounded installed recipe");
      const auto body_of = [](const auto &node) -> const bot::MessageBody & {
        return std::visit(
            [](const auto &value) -> const bot::MessageBody & {
              if constexpr (std::same_as<std::remove_cvref_t<decltype(value)>,
                                         bot::MessageRef>)
                throw std::invalid_argument("unsupported QQ forward node");
              else
                return value.body;
            },
            node);
      };
      onebot11::bot::detail::ForwardPreflight envelope_budget(
          request.target, request.limits, request.maximum_envelope_bytes,
          onebot11::bot::detail::ForwardBudgetScope::WholeSend);
      for (const auto &node : request.nodes) {
        preflight_segments(request.target, body_of(node), std::nullopt,
                           request.limits);
        if (const auto *authored =
                std::get_if<onebot11::bot::ForwardNode>(&node)) {
          codec().validate(authored->author);
          envelope_budget.node(authored->author, authored->display_name,
                               authored->body);
        } else {
          envelope_budget.authorless_node(body_of(node));
        }
      }
      bot::Json nodes = bot::Json::array();
      std::size_t total = 0;
      for (const auto &node : request.nodes) {
        auto limits = request.limits;
        limits.maximum_media_total_bytes -= total;
        limits.maximum_media_item_bytes = std::min(
            limits.maximum_media_item_bytes, limits.maximum_media_total_bytes);
        const std::optional<bot::MessageRef> no_reply;
        auto content = co_await until(
            prepare_segments(request.target, body_of(node), no_reply, limits,
                             deadline, &total, &envelope_budget),
            deadline);
        bot::Json data{{"content", std::move(content)}};
        if (const auto *authored =
                std::get_if<onebot11::bot::ForwardNode>(&node)) {
          data["uin"] = codec().unpack(authored->author.id).components[0];
          data["name"] = authored->display_name;
        }
        // The explicit authorless alternative emits neither name nor uin.
        // Do not turn cached self or a command source into invented authorship.
        nodes.push_back({{"type", "node"}, {"data", std::move(data)}});
        // A bounded dump at each of at most 99 nodes enforces the caller's
        // envelope budget before another node's preparation is retained.
        if (nodes.dump().size() > request.maximum_envelope_bytes)
          throw std::invalid_argument("QQ forward envelope exceeds budget");
      }
      const auto echo = next_echo();
      auto payload = protocol().serialize_send_group_forward_msg_request(
          group, nodes, echo);
      if (payload.size() > request.maximum_envelope_bytes)
        throw std::invalid_argument("QQ forward total envelope exceeded");
      if (std::chrono::steady_clock::now() >= deadline)
        throw boost::system::system_error(boost::asio::error::operation_aborted,
                                          "QQ forward total deadline exceeded");
      submitting = true;
      const auto executor = co_await boost::asio::this_coro::executor;
      auto response = co_await boost::asio::co_spawn(
          executor, transport().send_action(std::move(payload), echo),
          boost::asio::cancel_at(deadline, boost::asio::use_awaitable));
      auto raw = response.size() <= bot::detail::domain_maximum_bytes
                     ? bot::Json::parse(response, nullptr, false)
                     : bot::Json{};
      if (raw.is_object() && raw.contains("data") && raw["data"].is_object()) {
        if (auto id = verified_send_id(raw["data"], request.target)) {
          bot::MessageRef message{
              request.target, codec().make<bot::MessageId>("message", {*id})};
          bot::detail::SendEvidenceRecovery recovery(request.target,
                                                     *reply_allowance);
          recovery.reference(message);
          checkpoint = recovery.finish();
        }
      }
      // Capture verified physical evidence before parsing provider status or
      // touching optional resource/provenance fields.
      const auto parsed = parse_onebot11_operation_response(response, true);
      const auto &known = std::get<bot::SendFailure>(checkpoint);
      auto primary = checkpoint;
      if (parsed.ok() && !known.confirmed_messages.empty())
        primary =
            bot::SendReceipt{known.confirmed_messages, known.output_details};
      else if (!parsed.ok())
        primary =
            bot::send_failure(*parsed.error, known.output_details,
                              parsed.error->submission_safety ==
                                  bot::SubmissionSafety::PossiblySubmitted);
      primary = bot::normalize_single_message_outcome(
          request.target, std::move(primary), *reply_allowance);
      auto result = onebot11::bot::forward_outcome::minimal(std::move(primary));
      onebot11::bot::forward_outcome::Assembly metadata(request, result,
                                                        *reply_allowance);
      bool corrupt_token = false;
      if (!known.confirmed_messages.empty()) {
        const auto &data = raw.at("data");
        if (data.contains("forward_id") && !data.at("forward_id").is_null()) {
          try {
            const auto &token = bot::detail::require_identifier(
                data, "forward_id", "QQ forward resource", 512);
            onebot11::bot::QqForwardRef forward{
                known.confirmed_messages.front(), token};
            metadata.resource(forward);
          } catch (...) {
            corrupt_token = true;
          }
        }
        if (parsed.ok()) {
          onebot11::bot::ForwardOutputDetails provenance{
              known.confirmed_messages.front(), std::vector<std::size_t>{}};
          for (std::size_t index = 0; index < request.nodes.size(); ++index)
            provenance.node_indices->push_back(index);
          metadata.provenance(provenance);
        }
      }
      if (corrupt_token || metadata.corrupt())
        onebot11::bot::forward_outcome::make_unknown(result.outcome);
      co_return Reply::success(std::move(result));
    } catch (...) {
      if (submitting)
        co_return Reply::success(
            onebot11::bot::forward_outcome::minimal(std::move(checkpoint)));
      co_return Reply::success(onebot11::bot::forward_outcome::minimal(
          bot::send_failure(send_preparation_error(
                                std::current_exception(),
                                "QQ forward preflight or preparation failed"),
                            {}, false)));
    }
  }

  auto execute(const bot::GetMemberRequest &request)
      -> boost::asio::awaitable<bot::BotOperationResult<bot::MemberInfo>> {
    using Result = bot::MemberInfo;
    const auto scope = codec().unpack(request.conversation.id);
    if (scope.kind != "group")
      co_return unavailable<Result>("QQ member lookup requires a group");
    const auto user = codec().unpack(request.user.id).components[0];
    const auto echo = next_echo();
    auto response = co_await transport().send_action(
        protocol().serialize_get_chat_member_info_request(scope.components[0],
                                                          user, false, echo),
        echo);
    auto parsed = parse_onebot11_operation_response(response, false);
    if (!parsed.ok())
      co_return provider_failure<Result>(parsed);
    const auto &data = parsed_value(parsed);
    if (numeric_identity(data, "user_id") != user ||
        (data.contains("group_id") &&
         numeric_identity(data, "group_id") != scope.components[0]))
      co_return malformed_read<Result>("QQ member scope mismatch");
    auto name = optional_string(data, "card");
    if (name.empty())
      name = optional_string(data, "nickname");
    Result result{request.conversation, request.user};
    if (!name.empty())
      result.display_name = std::move(name);
    const auto role = optional_string(data, "role");
    if (role == "owner")
      result.role = bot::MemberRole::Owner;
    else if (role == "admin")
      result.role = bot::MemberRole::Administrator;
    else if (role == "member")
      result.role = bot::MemberRole::Member;
    result.membership = bot::Membership::Present;
    co_return bot::BotOperationResult<Result>::success(std::move(result));
  }

  auto execute(const bot::ResolveReplyRequest &request)
      -> boost::asio::awaitable<bot::BotOperationResult<bot::ReplyInfo>> {
    using Result = bot::BotOperationResult<bot::ReplyInfo>;
    const auto epoch = evidence_->generation();
    const auto local = evidence_->find(request.reply);
    if (!local)
      co_return unavailable<bot::ReplyInfo>("QQ reply evidence expired");
    if (request.policy == bot::ReplyLookupPolicy::LocalOnly || local->author())
      co_return Result::success(*local);
    const auto key = std::to_string(epoch.value_or(UINT64_MAX)) + ":" +
                     bot::Json(request.reply).dump();
    const auto executor = co_await boost::asio::this_coro::executor;
    auto waiter = std::make_shared<ReplyChannel>(executor, 1);
    bool leader = false;
    {
      std::lock_guard lock(reply_mutex_);
      auto [it, inserted] = reply_flights_.try_emplace(key);
      leader = inserted;
      it->second.push_back(waiter);
    }
    if (leader) {
      auto result =
          unavailable<bot::ReplyInfo>("QQ reply lookup failed or cancelled");
      try {
        result = co_await lookup_reply(request);
      } catch (...) {
      }
      std::vector<std::shared_ptr<ReplyChannel>> subscribers;
      {
        std::lock_guard lock(reply_mutex_);
        auto it = reply_flights_.find(key);
        subscribers = std::move(it->second);
        reply_flights_.erase(it);
      }
      for (auto &subscriber : subscribers)
        subscriber->try_send(boost::system::error_code{}, result);
    }
    auto result = co_await waiter->async_receive(boost::asio::use_awaitable);
    if (evidence_->generation() != epoch || !evidence_->admits(request.reply))
      co_return unavailable<bot::ReplyInfo>(
          "QQ reply evidence expired during coalesced lookup");
    co_return result;
  }

  auto lookup_reply(const bot::ResolveReplyRequest &request)
      -> boost::asio::awaitable<bot::BotOperationResult<bot::ReplyInfo>> {
    using Result = bot::ReplyInfo;
    const auto epoch = evidence_->generation();
    const auto local = evidence_->find(request.reply);
    if (!local)
      co_return unavailable<Result>("QQ reply evidence expired");
    if (request.policy == bot::ReplyLookupPolicy::LocalOnly || local->author())
      co_return bot::BotOperationResult<Result>::success(*local);
    const auto target = request.reply.target();
    const auto scope = codec().unpack(target.conversation.id);
    const auto id = codec().unpack(target.id).components[0];
    if (evidence_->generation() != epoch || !evidence_->admits(request.reply))
      co_return unavailable<Result>(
          "QQ reply evidence invalidated before lookup");
    const auto echo = next_echo();
    const auto response = co_await transport().send_action(
        protocol().serialize_get_message_request(id, echo), echo);
    if (evidence_->generation() != epoch || !evidence_->admits(request.reply))
      co_return unavailable<Result>(
          "QQ reply evidence invalidated during lookup");
    const auto parsed = parse_onebot11_operation_response(response, false);
    if (!parsed.ok())
      co_return provider_failure<Result>(parsed);
    try {
      const auto &data = parsed_value(parsed);
      const auto actual_kind = optional_string(data, "message_type");
      if (numeric_identity(data, "message_id") != id ||
          actual_kind != scope.kind)
        co_return malformed_read<Result>(
            "QQ reply target scope is missing or mismatched");
      if (scope.kind == "group") {
        if (numeric_identity(data, "group_id") != scope.components[0])
          co_return malformed_read<Result>("QQ reply group mismatch");
      } else {
        // sender alone is not proof of a private conversation for an outgoing
        // message.
        if (data.contains("group_id") ||
            numeric_identity(data, "target_id") != scope.components[0])
          co_return malformed_read<Result>(
              "QQ reply private peer is unverified");
      }
      const auto author = numeric_identity(data.at("sender"), "user_id");
      if (!author)
        co_return malformed_read<Result>("QQ reply author missing");
      bot::Sender sender = bot::KnownUser{
          {installation_, codec().make<bot::UserId>("user", {*author})}};
      co_return bot::BotOperationResult<Result>::success(
          bot::binding::EvidenceFactory::reply_info(
              request.reply.source(), request.reply, std::move(sender),
              local->relation(), bot::ReplyAvailability::AuthorKnown));
    } catch (...) {
      co_return malformed_read<Result>("QQ reply author or scope invalid");
    }
  }

  auto execute(const bot::ReadMediaRequest &request)
      -> boost::asio::awaitable<MediaReadResult> {
    // Ordinary function: freeze ownership/authority before any suspension.
    using Code = bot::BotOperationErrorCode;
    const auto deadline = std::chrono::steady_clock::now() + action_timeout_;
    request.validate();
    codec().validate(request.media);
    if (request.media.installation != installation_)
      return ready_media_read(bot::failed_operation<bot::MediaBlob>(
          Code::InvalidRequest, "QQ media installation mismatch"));
    const auto parts = codec().unpack(request.media.id);
    if (onebot11::bot::detail::is_received_media_kind(parts.kind)) {
      if (!received_media_)
        return ready_media_read(bot::failed_operation<bot::MediaBlob>(
            Code::CacheUnavailable, "QQ received media is unavailable"));
      return received_media_->read(request.media, request.maximum_bytes,
                                   deadline);
    }
    if (parts.kind == "file-id") {
      // Only the new explicit-file-ID vocabulary authorizes this recipe.
      // Legacy "file" may have encoded a display filename: never guess.
      const bot::ConversationRef conversation{
          installation_, codec().make<bot::ConversationId>(
                             parts.components[0], {parts.components[1]})};
      return read_native_file(
          {conversation, parts.components[4], request.maximum_bytes}, deadline);
    }
    if (parts.kind != "image" && parts.kind != "forward-image")
      return ready_media_read(
          unavailable<bot::MediaBlob>("QQ media has no supported read recipe"));
    auto identity = self_identity_;
    auto origin = identity ? identity->capture_origin() : std::nullopt;
    if (!origin)
      return ready_media_read(bot::failed_operation<bot::MediaBlob>(
          Code::CacheUnavailable, "QQ image read origin unavailable"));
    auto facade =
        std::dynamic_pointer_cast<OneBot11TransportCapability>(transport_);
    if (!facade)
      return ready_media_read(unavailable<bot::MediaBlob>(
          "QQ image read requires origin-aware native transport"));
    const auto &file = parts.components.back();
    validate_native_image_file(file);
    // Reuse the existing common wire/node/depth ceilings and configured
    // action timeout, not new configuration defaults or memory/RSS policy.
    const standard::CallLimits limits{bot::detail::domain_maximum_bytes,
                                      bot::detail::domain_maximum_bytes,
                                      action_timeout_};
    const standard::ValueLimits values{bot::detail::domain_maximum_bytes,
                                       bot::detail::domain_maximum_items, 32,
                                       bot::detail::domain_maximum_bytes};
    auto call =
        std::make_shared<standard_detail::StandardCallState>(limits, deadline);
    const auto echo = next_echo();
    auto captured = standard_detail::StandardTransportAccess::send(
        *facade, standard_detail::NativeAction::GetImage,
        standard::ExecutionMode::Direct,
        bot::Json{{"file", file}, {"download", false}}.dump(), echo, call,
        identity, *origin);
    return finish_native_image_read(std::make_shared<NativeImageReadWork>(
        NativeImageReadWork{std::move(facade), std::move(identity), *origin,
                            std::move(call), std::move(captured), values, echo,
                            request.maximum_bytes}));
  }

  auto execute(const bot::GetCapabilitiesRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<bot::ConversationCapabilities>> {
    bot::ConversationCapabilities result{request.conversation};
    result.operations = {
        bot::CommonOperation::Send, bot::CommonOperation::Delete,
        bot::CommonOperation::GetSelf, bot::CommonOperation::ResolveReply,
        bot::CommonOperation::Capabilities};
    if (codec().unpack(request.conversation.id).kind == "group")
      result.operations.push_back(bot::CommonOperation::GetMember);
    result.operations.push_back(bot::CommonOperation::ReadMedia);
    for (auto kind : {bot::MediaKind::Image, bot::MediaKind::Animation}) {
      result.media.push_back(
          {kind, bot::MediaSourceKind::Bytes,
           kind == bot::MediaKind::Animation
               ? std::vector<std::string>{"image/gif"}
               : std::vector<std::string>{"image/png", "image/jpeg"},
           true});
      if (kind == bot::MediaKind::Image)
        result.media.push_back(
            {kind, bot::MediaSourceKind::Reference, std::nullopt, true});
      for (auto source :
           {bot::MediaSourceKind::LocalFile, bot::MediaSourceKind::Http})
        result.media.push_back(
            {kind, source,
             kind == bot::MediaKind::Animation
                 ? std::vector<std::string>{"image/gif"}
                 : std::vector<std::string>{"image/png", "image/jpeg"},
             true});
    }
    // OneBot11 defines record/video sources, not a universal codec matrix.
    for (auto kind : {bot::MediaKind::Voice, bot::MediaKind::Video})
      for (auto source :
           {bot::MediaSourceKind::Bytes, bot::MediaSourceKind::LocalFile,
            bot::MediaSourceKind::Http})
        result.media.push_back({kind, source, std::nullopt, true});
    for (auto delivery : {bot::DeliveryPolicy::SingleMessageOnly,
                          bot::DeliveryPolicy::AllowSplit})
      result.combinations.push_back(
          {delivery,
           {bot::MediaKind::Image, bot::MediaKind::Animation,
            bot::MediaKind::Voice, bot::MediaKind::Video},
           true,
           true,
           true,
           1,
           std::nullopt,
           std::nullopt});
    co_return bot::BotOperationResult<bot::ConversationCapabilities>::success(
        std::move(result));
  }

  auto execute(const bot::SendMessageRequest &request,
               ConfirmedAuthorEvidenceCollector &authors)
      -> boost::asio::awaitable<bot::BotOperationResult<bot::SendOutcome>> {
    using Result = bot::BotOperationResult<bot::SendOutcome>;
    std::vector<bot::SendOutputDetails> confirmed;
    bool submitting = false;
    const auto deadline = std::chrono::steady_clock::now() + action_timeout_;
    try {
      auto segments = co_await until(
          prepare_segments(request.target, request.body, request.reply_to,
                           request.limits, deadline),
          deadline);
      const auto scope = codec().unpack(request.target.id);
      const auto echo = next_echo();
      auto payload = scope.kind == "group"
                         ? protocol().serialize_send_group_message_request(
                               scope.components[0], segments, echo)
                         : protocol().serialize_send_private_message_request(
                               scope.components[0], segments, echo);
      if (payload.size() > bot::detail::domain_maximum_bytes)
        throw std::invalid_argument("QQ send envelope exceeded");
      if (std::chrono::steady_clock::now() >= deadline)
        throw boost::system::system_error(boost::asio::error::operation_aborted,
                                          "QQ send total deadline exceeded");
      submitting = true;
      const auto executor = co_await boost::asio::this_coro::executor;
      auto response = co_await boost::asio::co_spawn(
          executor, transport().send_action(std::move(payload), echo),
          boost::asio::cancel_at(deadline, boost::asio::use_awaitable));
      auto parsed = parse_onebot11_operation_response(response, true);
      // Recover independently verifiable IDs even when the surrounding status
      // envelope is malformed. Association is then explicitly unknown.
      auto raw = response.size() <= bot::detail::domain_maximum_bytes
                     ? bot::Json::parse(response, nullptr, false)
                     : bot::Json{};
      if (raw.is_object() && raw.contains("data") && raw["data"].is_object()) {
        if (auto id = verified_send_id(raw["data"], request.target)) {
          try {
            bot::MessageRef message{
                request.target, codec().make<bot::MessageId>("message", {*id})};
            confirmed.push_back({message, std::nullopt, {}});
            // This owned common-body recipe emits ordinary text/media/reply
            // segments only, never anonymous or authored/reference nodes.
            // Revisit this guarantee if that execution recipe is expanded.
            authors.initiating_self(message);
          } catch (const std::invalid_argument &) {
          }
        }
      }
      if (!parsed.ok())
        co_return Result::success(
            bot::send_failure(*parsed.error, std::move(confirmed),
                              parsed.error->submission_safety ==
                                  bot::SubmissionSafety::PossiblySubmitted));
      if (confirmed.empty())
        co_return Result::success(
            bot::send_failure(bot::detail::unknown_send_error(), {}, true));
      std::vector<bot::ContentSlice> content;
      for (std::size_t index = 0; index < request.body.size(); ++index)
        content.push_back({index, std::nullopt});
      confirmed[0].content = std::move(content);
      bot::SendReceipt receipt{{confirmed[0].message}, std::move(confirmed)};
      co_return Result::success(bot::normalize_send_outcome(
          request, std::move(receipt), bot::SendOutcomeEnvelope::standalone()));
    } catch (...) {
      auto error = submitting ? bot::detail::unknown_send_error()
                              : send_preparation_error(
                                    std::current_exception(),
                                    "QQ send preflight or preparation failed");
      co_return Result::success(bot::send_failure(
          std::move(error), std::move(confirmed), submitting));
    }
  }

  auto execute(const bot::DeleteMessageRequest &request)
      -> boost::asio::awaitable<bot::BotOperationResult<bot::DeleteReceipt>> {
    const auto echo = next_echo();
    const auto response = co_await transport().send_action(
        protocol().serialize_delete_message_request(
            {}, codec().unpack(request.message.id).components[0], echo),
        echo);
    const auto parsed = parse_onebot11_operation_response(response, true);
    if (!parsed.ok()) {
      co_return provider_failure<bot::DeleteReceipt>(parsed);
    }
    co_return bot::BotOperationResult<bot::DeleteReceipt>::success(
        {.message = request.message});
  }

  auto execute(const obcx::onebot11::bot::GetOneBotGroupMemberRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<obcx::onebot11::bot::OneBotGroupMember>> {
    const auto selected = selected_user_id(request.user);
    const auto echo = next_echo();
    const auto response = co_await transport().send_action(
        protocol().serialize_get_chat_member_info_request(
            group_id(request.target), selected, request.no_cache, echo),
        echo);
    const auto parsed = parse_onebot11_operation_response(response, false);
    if (!parsed.ok()) {
      co_return provider_failure<obcx::onebot11::bot::OneBotGroupMember>(
          parsed);
    }
    using Result = obcx::onebot11::bot::OneBotGroupMember;
    const auto &value = parsed_value(parsed);
    const auto user_id = numeric_identity(value, "user_id");
    if (!user_id || *user_id != selected) {
      co_return malformed_read<Result>(
          "OneBot member response has missing or mismatched user ID");
    }
    if (value.contains("group_id") &&
        numeric_identity(value, "group_id") != group_id(request.target)) {
      co_return malformed_read<Result>(
          "OneBot member response has mismatched group ID");
    }
    try {
      bot::Json profile{
          {"schema", 1},
          {"target", request.target},
          {"user", bot::UserRef{installation_,
                                codec().make<bot::UserId>("user", {*user_id})}},
          {"nickname", ""},
          {"card", ""},
          {"title", ""},
          {"role", ""},
          {"sex", ""},
          {"age", nullptr},
          {"area", ""},
          {"level", ""}};
      for (const auto key : {"nickname", "card", "title", "role", "sex", "age",
                             "area", "level"}) {
        if (value.contains(key)) {
          profile[key] = value.at(key);
        }
      }
      co_return bot::BotOperationResult<Result>::success(profile.get<Result>());
    } catch (const std::exception &) {
      co_return malformed_read<Result>(
          "OneBot member response has invalid profile fields");
    }
  }

  struct ForwardQueryWork {
    onebot11::bot::ObserveOneBotForwardMessageRequest request;
    bot::BotInstallationRef installation;
    std::shared_ptr<const onebot11::bot::QqDomainBinding> binding;
    std::shared_ptr<adapter::onebot11::ProtocolAdapter> protocol;
    std::shared_ptr<onebot11::bot::detail::ReceivedMediaStore> media;
    std::shared_ptr<OneBot11TransportCapability> transport;
    std::shared_ptr<SelfIdentityCache> identity;
    SelfIdentityOrigin origin;
    std::shared_ptr<standard_detail::StandardCallState> call;
    standard_detail::CapturedStandardSend captured;
    standard::ValueLimits values;
    std::uint64_t echo;
  };
  using ForwardResult =
      bot::BotOperationResult<onebot11::bot::ObservedForwardMessage>;
  static auto ready_forward(ForwardResult result)
      -> boost::asio::awaitable<ForwardResult> {
    co_return std::move(result);
  }

  auto execute(const onebot11::bot::GetOneBotForwardMessageRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<onebot11::bot::OneBotForwardMessage>> {
    // Ordinary entry: the underlying observation request is owned before
    // returning an awaitable, not borrowed through its initial suspension.
    return forward_message_only(execute(
        onebot11::bot::ObserveOneBotForwardMessageRequest{request.forward}));
  }
  static auto forward_message_only(
      boost::asio::awaitable<ForwardResult> operation)
      -> boost::asio::awaitable<
          bot::BotOperationResult<onebot11::bot::OneBotForwardMessage>> {
    using Result = onebot11::bot::OneBotForwardMessage;
    auto observed = co_await std::move(operation);
    observed.validate();
    if (!observed.ok())
      co_return bot::BotOperationResult<Result>::failure(
          std::move(*observed.error));
    co_return bot::BotOperationResult<Result>::success(
        std::move(observed.value->message));
  }

  auto execute(const onebot11::bot::ObserveOneBotForwardMessageRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<onebot11::bot::ObservedForwardMessage>> {
    using Result = onebot11::bot::ObservedForwardMessage;
    using Code = bot::BotOperationErrorCode;
    const auto deadline = std::chrono::steady_clock::now() + action_timeout_;
    request.validate();
    codec().validate(request.forward.source);
    if (request.forward.source.conversation.installation != installation_)
      return ready_forward(bot::failed_operation<Result>(
          Code::InvalidRequest, "QQ forward query installation mismatch"));
    auto binding =
        std::dynamic_pointer_cast<const onebot11::bot::QqDomainBinding>(
            binding_);
    auto facade =
        std::dynamic_pointer_cast<OneBot11TransportCapability>(transport_);
    auto origin =
        self_identity_ ? self_identity_->capture_origin() : std::nullopt;
    if (!binding || !facade || !origin || !protocol_)
      return ready_forward(bot::failed_operation<Result>(
          Code::CacheUnavailable, "QQ forward query origin unavailable"));
    auto owned = request;
    const standard::CallLimits limits{bot::detail::domain_maximum_bytes,
                                      bot::detail::domain_maximum_bytes,
                                      action_timeout_};
    const standard::ValueLimits values{bot::detail::domain_maximum_bytes,
                                       bot::detail::domain_maximum_items, 32,
                                       bot::detail::domain_maximum_bytes};
    auto call =
        std::make_shared<standard_detail::StandardCallState>(limits, deadline);
    const auto echo = next_echo();
    auto captured = standard_detail::StandardTransportAccess::send(
        *facade, standard_detail::NativeAction::GetForwardMsg,
        standard::ExecutionMode::Direct,
        bot::Json{{"id", owned.forward.token}}.dump(), echo, call,
        self_identity_, *origin);
    return observe_forward_owned(std::make_shared<ForwardQueryWork>(
        ForwardQueryWork{std::move(owned), installation_, std::move(binding),
                         protocol_, received_media_, std::move(facade),
                         self_identity_, *origin, std::move(call),
                         std::move(captured), values, echo}));
  }

  static auto observe_forward_owned(std::shared_ptr<ForwardQueryWork> work)
      -> boost::asio::awaitable<ForwardResult> {
    using Result = onebot11::bot::ObservedForwardMessage;
    using Code = bot::BotOperationErrorCode;
    using Failure = standard_detail::StandardTransportFailureKind;
    const auto &request = work->request;
    const auto &installation = work->installation;
    const auto codec = [&]() -> const onebot11::bot::QqNativeReferenceCodec & {
      return work->binding->codec();
    };
    const auto cancellation =
        co_await boost::asio::this_coro::cancellation_state;
    const auto stage_failure = [&]() -> std::optional<ForwardResult> {
      if (!work->identity
               ->observe_user_and_capture(work->origin, std::nullopt, [] {})
               .accepted())
        return bot::failed_operation<Result>(Code::CacheUnavailable,
                                             "QQ forward query origin retired");
      if (cancellation.cancelled() != boost::asio::cancellation_type::none)
        return bot::failed_operation<Result>(Code::Cancelled,
                                             "QQ forward query cancelled");
      if (std::chrono::steady_clock::now() >= work->call->deadline())
        return bot::failed_operation<Result>(
            Code::TransportFailure, "QQ forward query deadline expired");
      return std::nullopt;
    };
    if (auto failure = stage_failure())
      co_return std::move(*failure);
    try {
      co_await std::move(work->captured.operation);
    } catch (const boost::system::system_error &error) {
      if (!work->call->terminal())
        (void)work->call->try_fail(
            {error.code() == boost::asio::error::operation_aborted
                 ? Failure::Cancelled
                 : Failure::TransportError,
             error.code()});
    } catch (...) {
      if (!work->call->terminal())
        (void)work->call->try_fail({Failure::TransportError, {}});
    }
    if (auto failure = stage_failure())
      co_return std::move(*failure);
    bot::Json reply;
    try {
      auto outcome = work->call->take_result();
      if (!outcome)
        co_return bot::failed_operation<Result>(
            Code::TransportFailure, "QQ forward query outcome unavailable");
      if (const auto *failure =
              std::get_if<standard_detail::StandardTransportFailure>(
                  &*outcome)) {
        const auto code = failure->kind == Failure::Cancelled ? Code::Cancelled
                          : failure->kind == Failure::ConnectionRetired
                              ? Code::CacheUnavailable
                          : failure->kind == Failure::ResponseTooLarge
                              ? Code::MediaTooLarge
                          : failure->kind == Failure::InvalidResponse
                              ? Code::MalformedResponse
                              : Code::TransportFailure;
        co_return bot::failed_operation<Result>(
            code, "QQ forward query transport failed");
      }
      const auto &wire = std::get<standard_detail::StandardWireReply>(*outcome);
      reply = standard_detail::parse_standard_reply(
          wire.body, work->call->limits(), work->values);
      if (work->captured.transport ==
              standard_detail::StandardWireTransport::WebSocket &&
          (wire.http_status || !media_echo_matches(reply, work->echo)))
        co_return malformed_read<Result>(
            "QQ forward query association invalid");
      const auto disposition = standard_detail::classify_native_reply(
          reply, standard_detail::NativeAction::GetForwardMsg,
          standard::ExecutionMode::Direct, work->values);
      if (disposition.kind == standard_detail::NativeReplyKind::Rejected) {
        auto result = bot::failed_operation<Result>(
            Code::ProviderRejected, "QQ forward query rejected");
        result.error->provider_code =
            std::to_string(*disposition.provider_retcode);
        result.error->retryable = *disposition.provider_retcode < 0;
        co_return result;
      }
      if (disposition.kind != standard_detail::NativeReplyKind::Completed ||
          (work->captured.transport ==
               standard_detail::StandardWireTransport::Http &&
           wire.http_status != 200))
        co_return malformed_read<Result>("QQ forward query response invalid");
    } catch (...) {
      co_return malformed_read<Result>("QQ forward query response invalid");
    }
    if (auto failure = stage_failure())
      co_return std::move(*failure);
    try {
      const auto &data = reply.at("data");
      // OneBot11 get_forward_msg specifies singular message/node.data. Keep
      // plural-key precedence for previously accepted provider responses,
      // including when both keys occur; malformed selected data never falls
      // back.
      const bool standard = !data.contains("messages");
      const auto &nodes = data.at(standard ? "message" : "messages");
      bot::detail::validate_domain_wire(nodes);
      if (!nodes.is_array() || nodes.size() > 99)
        co_return malformed_read<Result>("QQ forward node limit exceeded");
      if (standard) {
        // Bound the entire borrowed tree and cumulative CQ expansion before
        // copying any node content into the synthetic normalization event.
        bot::detail::DomainWireBudget expansion;
        expansion.inspect(nodes);
        const auto validate_segment = [](const bot::Json &segment) {
          if (!segment.is_object() || !segment.at("type").is_string() ||
              segment.at("type").get_ref<const std::string &>().empty())
            throw std::invalid_argument("invalid QQ forward segment");
          const auto &type = segment.at("type").get_ref<const std::string &>();
          const auto &fields = segment.at("data");
          // Array-format parameterless data may be null. Required-parameter
          // segments still need an object; never synthesize missing data.
          const bool parameterless = type == "rps" || type == "dice" ||
                                     type == "shake" || type == "anonymous";
          if (!fields.is_object() && !(fields.is_null() && parameterless))
            throw std::invalid_argument("invalid QQ forward segment");
        };
        for (const auto &node : nodes) {
          if (!node.is_object() || node.at("type") != "node" ||
              !node.at("data").is_object())
            throw std::invalid_argument("invalid QQ forward node");
          const auto &fields = node.at("data");
          const auto &nickname = fields.at("nickname");
          if (!nickname.is_string())
            throw std::invalid_argument("invalid QQ forward nickname");
          const auto &name = nickname.get_ref<const std::string &>();
          if (!name.empty())
            bot::detail::validate_identifier(name, "QQ forward sender name",
                                             2048);
          bot::detail::validate_utf8(name);
          const auto id = numeric_identity(fields, "user_id");
          if (!id)
            throw std::invalid_argument("invalid QQ forward sender");
          codec().make<bot::UserId>("user", {*id});
          const auto &content = fields.at("content");
          if (content.is_string()) {
            adapter::onebot11::MessageConverter::visit_v11_string(
                content.get_ref<const std::string &>(),
                [&](adapter::onebot11::MessageConverter::CqSegmentView view) {
                  expansion.nodes(3); // segment, type, data
                  expansion.bytes(8); // type/data keys
                  expansion.bytes(view.type.size());
                  if (view.literal_text) {
                    expansion.nodes();
                    expansion.bytes(4); // text key
                    expansion.bytes(view.literal_text->size());
                  } else {
                    adapter::onebot11::MessageConverter::visit_v11_parameters(
                        view.parameters, [&](auto key, auto value) {
                          expansion.nodes();
                          expansion.bytes(key.size());
                          expansion.bytes(value.size());
                        });
                  }
                });
          } else if (content.is_array()) {
            for (const auto &segment : content)
              validate_segment(segment);
          } else if (content.is_object()) {
            validate_segment(content);
            expansion.nodes(); // single-segment array wrapper
          } else {
            throw std::invalid_argument("invalid QQ forward content");
          }
        }
      }
      Result result{{request.forward, {}}, {}};
      const auto scope = codec().unpack(request.forward.source.conversation.id);
      for (std::size_t index = 0; index < nodes.size(); ++index) {
        const auto &node = standard ? nodes[index].at("data") : nodes[index];
        const auto &content = standard || node.contains("content")
                                  ? node.at("content")
                                  : node.at("message");
        // Node has no proven standalone MessageRef. The source ref below is
        // used only to normalize body/sender; it is never emitted as a node ID.
        bot::Json native{
            {"post_type", "message"},
            {"message_type", scope.kind},
            {"message_id",
             codec().unpack(request.forward.source.id).components[0]},
            {"user_id", scope.kind == "private" ? scope.components[0] : ""},
            {"message", content}};
        if (scope.kind == "group") {
          native["group_id"] = scope.components[0];
          native.erase("user_id");
        }
        // The API also permits a single segment object as message content.
        // Wrap only this bounded standard form, not the global event parser.
        if (standard && content.is_object())
          native["message"] = bot::Json::array({content});
        // Standard node fields carry no timestamp; leave it genuinely unknown.
        if (!standard && node.contains("time"))
          native["time"] = node["time"];
        auto event = work->protocol->parse_event(native.dump());
        if (!event || !std::holds_alternative<common::MessageEvent>(*event))
          throw std::invalid_argument("invalid QQ forward content");
        // A forward node has no verified standalone message scope, so its
        // reply segment cannot become a source/target relation or disappear.
        for (auto &segment : std::get<common::MessageEvent>(*event).message)
          if (segment.type == "reply")
            segment.type = "forward_reply";
        const auto received = bot::TimePoint{
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count()};
        const auto *qq_binding = work->binding.get();
        if (!qq_binding)
          throw std::invalid_argument(
              "forward observation requires owning QQ binding");
        std::vector<std::size_t> positions;
        const auto &native_event = std::get<common::MessageEvent>(*event);
        auto normalized = qq_binding->normalize_with_positions(
            installation, native_event, received, "forward", positions);
        for (std::size_t ordinal = 0; ordinal < native_event.message.size();
             ++ordinal) {
          const auto &segment = native_event.message[ordinal];
          const auto url = segment.data.find("url");
          const bool attachment =
              segment.type == "image" || segment.type == "video" ||
              segment.type == "record" || segment.type == "file";
          // Image without an observed URL keeps its existing native-image
          // lookup recipe. Other attachment kinds must not borrow the wrapper
          // group for file-ID queries or manufacture URL authority from IDs.
          if (attachment &&
              (segment.type != "image" || url != segment.data.end())) {
            if (!work->media)
              throw std::invalid_argument(
                  "forward media retention unavailable");
            const std::string_view actual_url =
                url != segment.data.end() && url->is_string()
                    ? std::string_view(url->get_ref<const std::string &>())
                    : std::string_view{};
            const auto position = positions.at(ordinal);
            if (position >= normalized.body.size() ||
                (!std::holds_alternative<bot::ReceivedMedia>(
                     normalized.body[position]) &&
                 !std::holds_alternative<bot::UnsupportedContent>(
                     normalized.body[position])))
              throw std::invalid_argument("forward media position mismatch");
            normalized.body[position] = work->media->retain_forward_media(
                request.forward.source, index, ordinal, segment.type,
                actual_url,
                onebot11::bot::QqDomainBinding::attachment_metadata(
                    segment.type, segment.data),
                work->origin);
          }
          if (segment.type != "face")
            continue;
          // Malformed optional specialized data cannot invent an observation;
          // its original UnsupportedContent remains visible in the common body.
          std::optional<onebot11::bot::FaceView> face;
          try {
            const auto &id = segment.data.at("id");
            auto value = id.is_string() ? id.get<std::string>() : id.dump();
            if (!std::ranges::all_of(
                    value, [](char c) { return c >= '0' && c <= '9'; }))
              continue;
            onebot11::bot::FaceView candidate{std::move(value), std::nullopt};
            if (const auto description = segment.data.find("text");
                description != segment.data.end() && description->is_string()) {
              const auto &text = description->get_ref<const std::string &>();
              if (!text.empty()) {
                bot::detail::validate_identifier(
                    text, "QQ forward face description", 2048);
                bot::detail::validate_utf8(text);
                candidate.description = text;
              }
            }
            candidate.validate();
            face = std::move(candidate);
          } catch (const std::exception &) {
          }
          if (face)
            result.faces.push_back(
                {index, positions.at(ordinal), std::move(*face)});
        }
        bot::Sender sender = bot::UnknownSender{};
        std::optional<std::string> name;
        if (standard) {
          sender = bot::KnownUser{
              {installation,
               codec().make<bot::UserId>(
                   "user", {*numeric_identity(node, "user_id")})}};
          name = node.at("nickname").get_ref<const std::string &>();
        } else if (node.contains("sender") && node["sender"].is_object()) {
          if (auto id = numeric_identity(node["sender"], "user_id"))
            sender = bot::KnownUser{
                {installation, codec().make<bot::UserId>("user", {*id})}};
          const auto &profile = node["sender"];
          if (profile.contains("nickname")) {
            // Validate borrowed provider data before creating the owned name;
            // missing remains absent, while an observed empty string survives.
            onebot11::bot::detail::preflight_text(profile, "nickname", 2048,
                                                  true);
            name = profile.at("nickname").get_ref<const std::string &>();
          }
        }
        for (auto &block : normalized.body)
          if (auto *media = std::get_if<bot::ReceivedMedia>(&block)) {
            auto parts = codec().unpack(media->media.id);
            if (onebot11::bot::detail::is_received_media_kind(parts.kind))
              continue; // Opaque private receipt, never re-encoded as a
                        // locator.
            media->media.id = codec().make<bot::MediaId>(
                "forward-" + parts.kind,
                {scope.kind, scope.components[0],
                 codec().unpack(request.forward.source.id).components[0],
                 request.forward.token, std::to_string(index),
                 parts.components[3], parts.components[4]});
          }
        result.message.nodes.push_back({std::move(sender), std::move(name),
                                        normalized.sent_at,
                                        std::move(normalized.body)});
      }
      result.validate();
      if (auto failure = stage_failure())
        co_return std::move(*failure);
      co_return bot::BotOperationResult<Result>::success(std::move(result));
    } catch (...) {
      if (auto failure = stage_failure())
        co_return std::move(*failure);
      co_return malformed_read<Result>("QQ forwarded node content is invalid");
    }
  }

  auto query_native_image(const std::string &file)
      -> boost::asio::awaitable<bot::BotOperationResult<NativeImageMetadata>> {
    using Result = NativeImageMetadata;
    validate_native_image_file(file);
    const auto echo = next_echo();
    const auto response = co_await transport().send_action(
        protocol().serialize_get_image_request(file, false, echo), echo);
    const auto parsed = parse_onebot11_operation_response(response, false);
    if (!parsed.ok()) {
      co_return provider_failure<Result>(parsed);
    }
    try {
      co_return bot::BotOperationResult<Result>::success(
          native_image_metadata(parsed_value(parsed)));
    } catch (const std::exception &) {
      co_return malformed_read<Result>(
          "OneBot get_image returned invalid image metadata");
    }
  }

  auto execute(const onebot11::bot::ReadOneBotFileRequest &request)
      -> boost::asio::awaitable<MediaReadResult> {
    return read_native_file(request,
                            std::chrono::steady_clock::now() + action_timeout_);
  }

  auto read_native_file(const onebot11::bot::ReadOneBotFileRequest &request,
                        network::detail::HttpDeadline deadline)
      -> boost::asio::awaitable<MediaReadResult> {
    using Code = bot::BotOperationErrorCode;
    try {
      request.validate();
      codec().validate(request.conversation);
      if (request.conversation.installation != installation_)
        throw std::invalid_argument("QQ file installation mismatch");
      const auto scope = codec().unpack(request.conversation.id);
      if (scope.kind != "group" && scope.kind != "private")
        throw std::invalid_argument("invalid QQ file conversation kind");
      const auto kind = scope.kind == "group"
                            ? onebot11::bot::detail::FileQueryKind::Group
                            : onebot11::bot::detail::FileQueryKind::Private;
      // Preserve the existing named file-query parameter representation.
      auto parameters = bot::Json{
          {scope.kind == "group" ? "group_id" : "user_id", scope.components[0]},
          {"file_id",
           request.file_id}}.dump();
      auto identity = self_identity_;
      auto origin = identity ? identity->capture_origin() : std::nullopt;
      if (!origin)
        return ready_media_read(bot::failed_operation<bot::MediaBlob>(
            Code::CacheUnavailable, "QQ file read origin unavailable"));
      auto facade =
          std::dynamic_pointer_cast<OneBot11TransportCapability>(transport_);
      if (!facade)
        return ready_media_read(unavailable<bot::MediaBlob>(
            "QQ file read requires origin-aware native transport"));
      const standard::CallLimits limits{bot::detail::domain_maximum_bytes,
                                        bot::detail::domain_maximum_bytes,
                                        action_timeout_};
      const standard::ValueLimits values{bot::detail::domain_maximum_bytes,
                                         bot::detail::domain_maximum_items, 32,
                                         bot::detail::domain_maximum_bytes};
      auto call = std::make_shared<standard_detail::StandardCallState>(
          limits, deadline);
      const auto echo = next_echo();
      auto captured = onebot11::bot::detail::FileTransportAccess::send(
          *facade, kind, std::move(parameters), echo, call, identity, *origin);
      return finish_native_file_read(std::make_shared<NativeFileReadWork>(
          NativeFileReadWork{std::move(facade), std::move(identity), *origin,
                             std::move(call), std::move(captured), values, echo,
                             request.maximum_bytes}));
    } catch (...) {
      return ready_media_read(bot::failed_operation<bot::MediaBlob>(
          Code::InvalidRequest, "QQ file read failed native preflight"));
    }
  }

  auto execute(const onebot11::bot::ResolveOneBotGroupFileRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<onebot11::bot::ResolvedOneBotGroupFile>> {
    using Result = onebot11::bot::ResolvedOneBotGroupFile;
    const auto parts = codec().unpack(request.media.id);
    if (parts.kind != "file" || parts.components[0] != "group")
      co_return unavailable<Result>("QQ group-file scope mismatch");
    const auto echo = next_echo();
    auto response = co_await transport().send_action(
        protocol().serialize_get_group_file_url_request(
            parts.components[1], parts.components[4], echo),
        echo);
    auto parsed = parse_onebot11_operation_response(response, false);
    if (!parsed.ok())
      co_return provider_failure<Result>(parsed);
    co_return bot::BotOperationResult<Result>::success(
        {request.media, optional_string(parsed_value(parsed), "url")});
  }
  auto execute(const onebot11::bot::ResolveOneBotPrivateFileRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<onebot11::bot::ResolvedOneBotPrivateFile>> {
    using Result = onebot11::bot::ResolvedOneBotPrivateFile;
    const auto parts = codec().unpack(request.media.id);
    if (parts.kind != "file" || parts.components[0] != "private")
      co_return unavailable<Result>("QQ private-file scope mismatch");
    const auto echo = next_echo();
    auto response = co_await transport().send_action(
        protocol().serialize_get_private_file_url_request(
            parts.components[1], parts.components[4], echo),
        echo);
    auto parsed = parse_onebot11_operation_response(response, false);
    if (!parsed.ok())
      co_return provider_failure<Result>(parsed);
    co_return bot::BotOperationResult<Result>::success(
        {request.media, optional_string(parsed_value(parsed), "url")});
  }

  auto execute(
      const obcx::onebot11::bot::SetOneBotMessageEmojiLikeRequest &request)
      -> boost::asio::awaitable<bot::BotOperationResult<
          obcx::onebot11::bot::OneBotMessageEmojiLikeResult>> {
    using Result = obcx::onebot11::bot::OneBotMessageEmojiLikeResult;
    codec().validate(request.message);
    const auto echo = next_echo();
    const auto response = co_await transport().send_action(
        protocol().serialize_set_msg_emoji_like_request(
            codec().unpack(request.message.id).components[0], request.emoji_id,
            request.set, echo),
        echo);
    const auto parsed = parse_onebot11_operation_response(response, true);
    if (!parsed.ok()) {
      co_return provider_failure<Result>(parsed);
    }
    // LLOneBot acknowledges this mutation with null data, not a message
    // receipt.
    co_return bot::BotOperationResult<Result>::success(
        {.message = request.message,
         .emoji_id = request.emoji_id,
         .set = request.set});
  }

  auto execute(const obcx::onebot11::bot::PokeOneBotGroupRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<obcx::onebot11::bot::OneBotGroupPokeResult>> {
    const auto echo = next_echo();
    const auto response = co_await transport().send_action(
        protocol().serialize_group_poke_request(
            group_id(request.target),
            codec().unpack(request.user.id).components[0], echo),
        echo);
    const auto parsed = parse_onebot11_operation_response(response, true);
    if (!parsed.ok()) {
      co_return provider_failure<obcx::onebot11::bot::OneBotGroupPokeResult>(
          parsed);
    }
    co_return bot::
        BotOperationResult<obcx::onebot11::bot::OneBotGroupPokeResult>::success(
            {.target = request.target, .user = request.user});
  }

private:
  auto verified_send_id(const bot::Json &data,
                        const bot::ConversationRef &target) const
      -> std::optional<std::string> {
    const auto scope = codec().unpack(target.id);
    if (data.contains("message_type") && (!data["message_type"].is_string() ||
                                          data["message_type"] != scope.kind))
      return std::nullopt;
    if (data.contains("group_id") &&
        (scope.kind != "group" ||
         numeric_identity(data, "group_id") != scope.components[0]))
      return std::nullopt;
    if (scope.kind == "private" && data.contains("target_id") &&
        numeric_identity(data, "target_id") != scope.components[0])
      return std::nullopt;
    return numeric_identity(data, "message_id");
  }
  template <class T>
  auto until(boost::asio::awaitable<T> operation,
             std::chrono::steady_clock::time_point deadline)
      -> boost::asio::awaitable<T> {
    if (std::chrono::steady_clock::now() >= deadline)
      throw boost::system::system_error(boost::asio::error::operation_aborted,
                                        "QQ operation deadline exceeded");
    const auto executor = co_await boost::asio::this_coro::executor;
    co_return co_await boost::asio::co_spawn(
        executor, std::move(operation),
        boost::asio::cancel_at(deadline, boost::asio::use_awaitable));
  }
  auto read_http_image(const std::string &url, std::size_t maximum,
                       std::chrono::steady_clock::time_point deadline)
      -> boost::asio::awaitable<bot::BytesSource> {
    bot::HttpSource{url, {}}.validate();
    const auto remaining =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
    if (remaining.count() <= 0)
      throw boost::system::system_error(boost::asio::error::operation_aborted,
                                        "QQ operation deadline exceeded");
    auto response = co_await until(
        network::fetch_image({url, remaining, maximum, std::nullopt}),
        deadline);
    // No Range was requested. A partial response is not a complete source,
    // even when its prefix happens to have a supported image signature.
    if (response.status_code != 200)
      throw network::ImageFetchError(network::ImageFetchFailure::Transport);
    if (response.body.size() > maximum)
      throw network::ImageFetchError(network::ImageFetchFailure::OverLimit);
    bot::BytesSource bytes{{response.body.begin(), response.body.end()}, {}};
    bytes.metadata.size_bytes = bytes.bytes.size();
    bytes.validate();
    co_return bytes;
  }
  auto group_id(const bot::ConversationRef &target) const -> std::string {
    codec().validate(target);
    const auto parts = codec().unpack(target.id);
    if (parts.kind != "group")
      throw std::invalid_argument("QQ group operation requires group scope");
    return parts.components[0];
  }
  // Borrowed, synchronous whole-body preflight. No preparation/provider I/O
  // may start until every node's native users, sources and recipes pass it.
  void preflight_segments(const bot::ConversationRef &target,
                          const bot::MessageBody &body,
                          const std::optional<bot::MessageRef> &reply_to,
                          const bot::SendLimits &limits) const {
    std::size_t cumulative_known_bytes = 0;
    bot::validate_message_body(target, body, limits, cumulative_known_bytes);
    codec().validate(target);
    if (reply_to) {
      codec().validate(*reply_to);
      if (reply_to->conversation != target)
        throw std::invalid_argument("QQ reply scope mismatch");
    }
    // The installed QQ recipe emits one ordered segment envelope. It never
    // splits or silently changes styles/formats to make a request fit.
    const auto check_text = [&](const bot::RichText &text) {
      for (const auto &part : text) {
        if (const auto *t = std::get_if<bot::Text>(&part)) {
          if (!t->styles.empty())
            throw std::invalid_argument(
                "QQ native text styles are unsupported");
        } else {
          codec().validate(std::get<bot::Mention>(part).user);
        }
      }
    };
    for (const auto &block : body) {
      std::visit(
          [&](const auto &value) {
            using T = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<T, bot::RichText>)
              check_text(value);
            else if constexpr (std::same_as<T, bot::Image> ||
                               std::same_as<T, bot::Animation> ||
                               std::same_as<T, bot::Video> ||
                               std::same_as<T, bot::Audio>) {
              if constexpr (std::same_as<T, bot::Audio>)
                if (value.kind != bot::AudioKind::Voice)
                  throw std::invalid_argument(
                      "QQ ordinary audio has no installed send recipe");
              check_text(value.caption);
              if (!limits.maximum_media_item_bytes)
                throw std::invalid_argument(
                    "QQ media requires a positive byte budget");
              if (const auto *ref = std::get_if<bot::MediaRef>(&value.source)) {
                codec().validate(*ref);
                const auto parts = codec().unpack(ref->id);
                // Voice/video references carry no size declaration and have
                // no installed lossless size query. Do not invent a budget.
                if (!std::same_as<T, bot::Image> ||
                    (parts.kind != "image" && parts.kind != "forward-image" &&
                     parts.kind != "received-image" &&
                     parts.kind != "queried-forward-image"))
                  throw std::invalid_argument(
                      "QQ native media kind cannot satisfy this send");
              } else if (const auto *bytes =
                             std::get_if<bot::BytesSource>(&value.source)) {
                if constexpr (std::same_as<T, bot::Image> ||
                              std::same_as<T, bot::Animation>)
                  validate_image_format(*bytes,
                                        std::same_as<T, bot::Animation>);
              } else if (const auto *path =
                             std::get_if<bot::LocalFile>(&value.source)) {
                onebot11::bot::detail::validate_media_path(path->path);
              }
            } else
              throw std::invalid_argument(
                  "QQ media kind has no installed send recipe");
          },
          block);
    }
  }
  auto prepare_segments(
      const bot::ConversationRef &target, const bot::MessageBody &body,
      const std::optional<bot::MessageRef> &reply_to,
      const bot::SendLimits &limits,
      std::chrono::steady_clock::time_point deadline,
      std::size_t *prepared_total = nullptr,
      onebot11::bot::detail::ForwardPreflight *envelope_budget = nullptr)
      -> boost::asio::awaitable<common::Message> {
    preflight_segments(target, body, reply_to, limits);
    common::Message segments;
    if (reply_to)
      segments.push_back(
          {"reply", {{"id", codec().unpack(reply_to->id).components[0]}}});
    const auto append_text = [&](const bot::RichText &text) {
      for (const auto &part : text)
        std::visit(
            [&](const auto &value) {
              using T = std::remove_cvref_t<decltype(value)>;
              if constexpr (std::same_as<T, bot::Text>)
                segments.push_back({"text", {{"text", value.content}}});
              else
                segments.push_back(
                    {"at",
                     {{"qq", codec().unpack(value.user.id).components[0]}}});
            },
            part);
    };
    std::size_t total = 0;
    for (const auto &block : body) {
      if (const auto *text = std::get_if<bot::RichText>(&block)) {
        append_text(*text);
        continue;
      }
      const bool animation = std::holds_alternative<bot::Animation>(block);
      const bool image = animation || std::holds_alternative<bot::Image>(block);
      const auto [source, caption] = std::visit(
          [](const auto &value)
              -> std::pair<const bot::MediaSource &, const bot::RichText &> {
            using T = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<T, bot::Image> ||
                          std::same_as<T, bot::Animation> ||
                          std::same_as<T, bot::Video> ||
                          std::same_as<T, bot::Audio>)
              return {value.source, value.caption};
            else
              throw std::invalid_argument(
                  "QQ media kind has no installed send recipe");
          },
          block);
      auto maximum = std::min(limits.maximum_media_item_bytes,
                              limits.maximum_media_total_bytes - total);
      if (!maximum)
        throw std::invalid_argument("QQ aggregate media budget exhausted");
      std::string file;
      const auto *ref = std::get_if<bot::MediaRef>(&source);
      const bool received_image = ref && [&] {
        const auto kind = codec().unpack(ref->id).kind;
        return kind == "received-image" || kind == "queried-forward-image";
      }();
      if (ref && !received_image) {
        const auto parts = codec().unpack(ref->id);
        auto info = co_await until(image_info(*ref), deadline);
        info.validate();
        if (!info.ok())
          throw SendPreparationFailure(std::move(*info.error));
        if (!info.value->file_size || !*info.value->file_size ||
            *info.value->file_size > maximum)
          throw std::invalid_argument(
              "QQ native image size is unavailable or exceeds budget");
        total += *info.value->file_size;
        file = parts.components.back();
      } else if (const auto *path = std::get_if<bot::LocalFile>(&source)) {
        // Declared sizes consume the known-size budget, but an absent size
        // stays unknown. We neither read nor claim to measure provider files.
        if (path->metadata.size_bytes) {
          if (*path->metadata.size_bytes > maximum)
            throw std::invalid_argument("QQ media exceeds byte budget");
          total += *path->metadata.size_bytes;
        }
        file = onebot11::bot::detail::media_file_uri(path->path);
      } else {
        const auto known = std::visit(
            [](const auto &s) -> std::optional<std::uint64_t> {
              using T = std::remove_cvref_t<decltype(s)>;
              if constexpr (std::same_as<T, bot::BytesSource>)
                return s.bytes.size();
              else if constexpr (!std::same_as<T, bot::MediaRef>)
                return s.metadata.size_bytes;
              else
                return std::nullopt;
            },
            source);
        if (envelope_budget)
          maximum =
              std::min(maximum, envelope_budget->maximum_owned_bytes(known));
        if (!maximum)
          throw std::invalid_argument(
              "QQ forward envelope has no media preparation budget");
        std::optional<bot::BytesSource> loaded;
        const auto *bytes = std::get_if<bot::BytesSource>(&source);
        if (!bytes) {
          if (received_image) {
            if (!received_media_)
              throw std::invalid_argument("QQ received image is unavailable");
            auto result =
                co_await received_media_->read(*ref, maximum, deadline);
            result.validate();
            if (!result.ok())
              throw SendPreparationFailure(std::move(*result.error));
            loaded = std::move(result.value->source);
          } else {
            const auto &http = std::get<bot::HttpSource>(source);
            loaded = co_await read_http_image(http.url, maximum, deadline);
            if (http.metadata.size_bytes &&
                *http.metadata.size_bytes != loaded->bytes.size())
              throw std::invalid_argument(
                  "QQ HTTP media size declaration mismatch");
          }
          bytes = &*loaded;
        }
        bytes->validate();
        if (bytes->bytes.size() > maximum)
          throw std::invalid_argument("QQ media exceeds byte budget");
        if (image)
          validate_image_format(*bytes, animation);
        if (envelope_budget) {
          envelope_budget->prepared_media(
              9 + 4 * ((bytes->bytes.size() + 2) / 3), known);
        }
        total += bytes->bytes.size();
        file = "base64://" + base64(bytes->bytes);
      }
      if (envelope_budget && ref && !received_image)
        envelope_budget->prepared_media(
            onebot11::bot::detail::ForwardPreflight::escaped_size(file),
            std::nullopt);
      const auto type = image                                       ? "image"
                        : std::holds_alternative<bot::Video>(block) ? "video"
                                                                    : "record";
      segments.push_back({type, {{"file", std::move(file)}}});
      append_text(caption);
    }
    if (prepared_total)
      *prepared_total += total;
    co_return segments;
  }
  template <class Result>
  static auto unavailable(std::string message)
      -> bot::BotOperationResult<Result> {
    return bot::BotOperationResult<Result>::failure(
        {.code = bot::BotOperationErrorCode::UnsupportedAction,
         .message = std::move(message),
         .retryable = false,
         .submission_safety = bot::SubmissionSafety::DefinitelyNotSubmitted});
  }
  static auto base64(const std::vector<std::uint8_t> &bytes) -> std::string {
    constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    result.reserve((bytes.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
      unsigned value = unsigned(bytes[i]) << 16;
      if (i + 1 < bytes.size())
        value |= unsigned(bytes[i + 1]) << 8;
      if (i + 2 < bytes.size())
        value |= bytes[i + 2];
      result.push_back(alphabet[(value >> 18) & 63]);
      result.push_back(alphabet[(value >> 12) & 63]);
      result.push_back(i + 1 < bytes.size() ? alphabet[(value >> 6) & 63]
                                            : '=');
      result.push_back(i + 2 < bytes.size() ? alphabet[value & 63] : '=');
    }
    return result;
  }
  static void validate_image_format(const bot::BytesSource &source,
                                    bool animation) {
    const auto &b = source.bytes;
    const auto prefix = [&](std::string_view p) {
      return b.size() >= p.size() && std::equal(p.begin(), p.end(), b.begin());
    };
    const bool gif = prefix("GIF87a") || prefix("GIF89a");
    // Match the common Telegram recipe's conservative APNG rejection. A PNG
    // signature alone does not prove static content; never submit possible
    // animation through Image and rely on the provider to flatten it.
    const bool png =
        b.size() >= 8 && b[0] == 137 &&
        std::equal(b.begin() + 1, b.begin() + 8, "PNG\r\n\x1a\n") &&
        std::ranges::search(b, std::string_view{"acTL"}).empty();
    const bool jpeg =
        b.size() >= 3 && b[0] == 255 && b[1] == 216 && b[2] == 255;
    if ((animation && !gif) || (!animation && !png && !jpeg))
      throw std::invalid_argument("QQ image format has no installed recipe");
  }
  auto image_info(const bot::MediaRef &ref)
      -> boost::asio::awaitable<bot::BotOperationResult<NativeImageMetadata>> {
    codec().validate(ref);
    const auto parts = codec().unpack(ref.id);
    if (ref.installation != installation_ ||
        (parts.kind != "image" && parts.kind != "forward-image"))
      co_return unavailable<NativeImageMetadata>(
          "QQ media is not a native image");
    co_return co_await query_native_image(parts.components.back());
  }
  [[nodiscard]] auto next_echo() noexcept -> std::uint64_t {
    return echo_.fetch_add(1, std::memory_order_relaxed);
  }
  [[nodiscard]] auto protocol() const -> adapter::onebot11::ProtocolAdapter & {
    if (protocol_ == nullptr) {
      throw BotComponentRuntimeError("OneBot operations are not prepared");
    }
    return *protocol_;
  }
  [[nodiscard]] auto transport() const -> OneBot11Transport & {
    if (transport_ == nullptr) {
      throw BotComponentRuntimeError("OneBot operations are not prepared");
    }
    return *transport_;
  }

  auto codec() const -> const bot::binding::NativeReferenceCodec & {
    return binding_->codec();
  }
  bot::BotInstallationRef installation_;
  std::shared_ptr<const PlatformDomainBinding> binding_;
  std::chrono::milliseconds action_timeout_;
  using ReplyChannel = boost::asio::experimental::concurrent_channel<void(
      boost::system::error_code, bot::BotOperationResult<bot::ReplyInfo>)>;
  std::mutex reply_mutex_;
  std::map<std::string, std::vector<std::shared_ptr<ReplyChannel>>>
      reply_flights_;
  std::shared_ptr<ReplyEvidenceStore> evidence_;
  std::shared_ptr<adapter::onebot11::ProtocolAdapter> protocol_;
  std::shared_ptr<OneBot11Transport> transport_;
  std::shared_ptr<SelfIdentityCache> self_identity_;
  std::shared_ptr<onebot11::bot::detail::ReceivedMediaStore> received_media_;
  std::shared_ptr<standard_detail::LoginCoordinator> login_coordinator_;
  std::atomic_uint64_t echo_{};
};

auto OneBot11OperationsComponent::Endpoint::supports_standard_send_like() const
    -> bool {
  return std::dynamic_pointer_cast<OneBot11TransportCapability>(transport_) !=
         nullptr;
}

auto OneBot11OperationsComponent::Endpoint::execute_standard_send_like(
    const standard::SendLikeRequest &request)
    -> boost::asio::awaitable<StructuredOperationCompletion> {
  // Synchronous preparation: no borrowed request or Endpoint crosses await.
  using Code = bot::BotOperationErrorCode;
  auto identity = self_identity_;
  const auto origin = identity ? identity->capture_origin() : std::nullopt;
  const auto deadline =
      standard_detail::StandardRequestAccess::deadline(request);
  if (!deadline || std::chrono::steady_clock::now() >= *deadline)
    return reject_send_like(
        Code::Cancelled,
        "standard send_like dispatch deadline unavailable or elapsed");
  if (!origin)
    return reject_send_like(Code::TransportFailure,
                            "standard send_like authority is unavailable");
  auto facade =
      std::dynamic_pointer_cast<OneBot11TransportCapability>(transport_);
  if (!facade)
    return reject_send_like(Code::UnsupportedAction,
                            "standard send_like requires native transport");
  try {
    request.validate();
    if (!binding_ || request.parameters.user.installation != installation_)
      return reject_send_like(Code::InvalidRequest,
                              "standard send_like installation is invalid");
    codec().validate(installation_);
    codec().validate(request.parameters.user);
    const auto parameters = standard_detail::encode_parameters(
        request.parameters, installation_, request.value_limits);
    auto serialized = serialize_send_like_parameters(
        parameters, request.control.limits.maximum_parameter_bytes);
    auto call = std::make_shared<standard_detail::StandardCallState>(
        request.control.limits, *deadline);
    const auto mode = request.control.execution;
    const auto values = request.value_limits;
    auto installation = installation_;
    auto action = bot::OperationTraits<standard::SendLikeRequest>::action();
    const auto echo = next_echo();
    auto captured = standard_detail::StandardTransportAccess::send(
        *facade, standard_detail::NativeAction::SendLike, mode,
        std::move(serialized), echo, call, identity, *origin);
    return finish_send_like(std::make_shared<PreparedSendLike>(PreparedSendLike{
        std::move(facade), std::move(identity), std::move(call), mode, values,
        echo, std::move(installation), std::move(action),
        std::move(captured)}));
  } catch (const BotComponentRuntimeError &) {
    return reject_send_like(
        Code::UnsupportedAction,
        "standard send_like native transport is unavailable");
  } catch (...) {
    return reject_send_like(Code::InvalidRequest,
                            "standard send_like preparation failed validation");
  }
}

auto OneBot11OperationsComponent::Endpoint::supports_standard_mutations() const
    -> bool {
  return std::dynamic_pointer_cast<OneBot11TransportCapability>(transport_) !=
         nullptr;
}

auto OneBot11OperationsComponent::Endpoint::execute_standard_delete_msg(
    const standard::DeleteMsgRequest &request)
    -> boost::asio::awaitable<StructuredOperationCompletion> {
  // Synchronous preparation: no borrowed request or Endpoint crosses await.
  using Code = bot::BotOperationErrorCode;
  auto identity = self_identity_;
  const auto origin = identity ? identity->capture_origin() : std::nullopt;
  const auto deadline =
      standard_detail::StandardRequestAccess::deadline(request);
  if (!deadline || std::chrono::steady_clock::now() >= *deadline)
    return reject_mutation(
        Code::Cancelled,
        "standard delete_msg dispatch deadline unavailable or elapsed");
  if (!origin)
    return reject_mutation(Code::TransportFailure,
                           "standard delete_msg authority is unavailable");
  auto facade =
      std::dynamic_pointer_cast<OneBot11TransportCapability>(transport_);
  if (!facade)
    return reject_mutation(Code::UnsupportedAction,
                           "standard delete_msg requires native transport");
  try {
    request.validate();
    if (!binding_ ||
        request.parameters.message.conversation.installation != installation_)
      return reject_mutation(Code::InvalidRequest,
                             "standard delete_msg installation is invalid");
    codec().validate(installation_);
    codec().validate(request.parameters.message);
    const auto parameters = standard_detail::encode_parameters(
        request.parameters, installation_, request.value_limits);
    auto serialized = serialize_mutation_parameters(
        parameters, request.control.limits.maximum_parameter_bytes);
    auto call = std::make_shared<standard_detail::StandardCallState>(
        request.control.limits, *deadline);
    const auto mode = request.control.execution;
    const auto values = request.value_limits;
    auto installation = installation_;
    auto action = bot::OperationTraits<standard::DeleteMsgRequest>::action();
    const auto echo = next_echo();
    auto captured = standard_detail::StandardTransportAccess::send(
        *facade, standard_detail::NativeAction::DeleteMsg, mode,
        std::move(serialized), echo, call, identity, *origin);
    return finish_mutation(std::make_shared<PreparedMutation>(PreparedMutation{
        std::move(facade), std::move(identity), std::move(call),
        standard_detail::NativeAction::DeleteMsg, mode, values, echo,
        std::move(installation), std::move(action), std::move(captured)}));
  } catch (const BotComponentRuntimeError &) {
    return reject_mutation(
        Code::UnsupportedAction,
        "standard delete_msg native transport is unavailable");
  } catch (...) {
    return reject_mutation(Code::InvalidRequest,
                           "standard delete_msg preparation failed validation");
  }
}

auto OneBot11OperationsComponent::Endpoint::execute_standard_set_group_kick(
    const standard::SetGroupKickRequest &request)
    -> boost::asio::awaitable<StructuredOperationCompletion> {
  // Synchronous preparation: no borrowed request or Endpoint crosses await.
  using Code = bot::BotOperationErrorCode;
  auto identity = self_identity_;
  const auto origin = identity ? identity->capture_origin() : std::nullopt;
  const auto deadline =
      standard_detail::StandardRequestAccess::deadline(request);
  if (!deadline || std::chrono::steady_clock::now() >= *deadline)
    return reject_mutation(
        Code::Cancelled,
        "standard set_group_kick dispatch deadline unavailable or elapsed");
  if (!origin)
    return reject_mutation(Code::TransportFailure,
                           "standard set_group_kick authority is unavailable");
  auto facade =
      std::dynamic_pointer_cast<OneBot11TransportCapability>(transport_);
  if (!facade)
    return reject_mutation(Code::UnsupportedAction,
                           "standard set_group_kick requires native transport");
  try {
    request.validate();
    if (!binding_ || request.parameters.group.installation != installation_)
      return reject_mutation(Code::InvalidRequest,
                             "standard set_group_kick installation is invalid");
    codec().validate(installation_);
    codec().validate(request.parameters.group);
    codec().validate(request.parameters.user);
    const auto parameters = standard_detail::encode_parameters(
        request.parameters, installation_, request.value_limits);
    auto serialized = serialize_mutation_parameters(
        parameters, request.control.limits.maximum_parameter_bytes);
    auto call = std::make_shared<standard_detail::StandardCallState>(
        request.control.limits, *deadline);
    const auto mode = request.control.execution;
    const auto values = request.value_limits;
    auto installation = installation_;
    auto action = bot::OperationTraits<standard::SetGroupKickRequest>::action();
    const auto echo = next_echo();
    auto captured = standard_detail::StandardTransportAccess::send(
        *facade, standard_detail::NativeAction::SetGroupKick, mode,
        std::move(serialized), echo, call, identity, *origin);
    return finish_mutation(std::make_shared<PreparedMutation>(PreparedMutation{
        std::move(facade), std::move(identity), std::move(call),
        standard_detail::NativeAction::SetGroupKick, mode, values, echo,
        std::move(installation), std::move(action), std::move(captured)}));
  } catch (const BotComponentRuntimeError &) {
    return reject_mutation(
        Code::UnsupportedAction,
        "standard set_group_kick native transport is unavailable");
  } catch (...) {
    return reject_mutation(
        Code::InvalidRequest,
        "standard set_group_kick preparation failed validation");
  }
}

auto OneBot11OperationsComponent::Endpoint::
    execute_standard_set_group_anonymous_ban(
        const standard::SetGroupAnonymousBanRequest &request)
        -> boost::asio::awaitable<StructuredOperationCompletion> {
  using Code = bot::BotOperationErrorCode;
  const auto deadline =
      standard_detail::StandardRequestAccess::deadline(request);
  if (!deadline || std::chrono::steady_clock::now() >= *deadline)
    return reject_mutation(Code::Cancelled,
                           "standard event action deadline elapsed");
  auto identity = self_identity_;
  auto facade =
      std::dynamic_pointer_cast<OneBot11TransportCapability>(transport_);
  if (!facade)
    return reject_mutation(Code::UnsupportedAction,
                           "standard event action requires native transport");
  try {
    request.validate();
    const auto &context = standard_detail::StandardEventAccess::context(
        request.parameters.source);
    if (!binding_ || context.installation != installation_ ||
        context.identity != identity)
      return reject_mutation(Code::InvalidRequest,
                             "standard event action installation mismatch");
    standard_detail::StandardEventAccess::validate(request.parameters.source);
    codec().validate(installation_);
    const auto parameters = standard_detail::encode_event_action(request);
    auto serialized = serialize_mutation_parameters(
        parameters, request.control.limits.maximum_parameter_bytes);
    auto call = std::make_shared<standard_detail::StandardCallState>(
        request.control.limits, *deadline);
    const auto mode = request.control.execution;
    const auto values = request.value_limits;
    auto installation = installation_;
    auto action =
        bot::OperationTraits<standard::SetGroupAnonymousBanRequest>::action();
    const auto echo = next_echo();
    // Never capture a fresh origin for work backed by a received event.
    auto captured = standard_detail::StandardTransportAccess::send(
        *facade, standard_detail::NativeAction::SetGroupAnonymousBan, mode,
        std::move(serialized), echo, call, identity, context.origin);
    return finish_mutation(std::make_shared<PreparedMutation>(PreparedMutation{
        std::move(facade), std::move(identity), std::move(call),
        standard_detail::NativeAction::SetGroupAnonymousBan, mode, values, echo,
        std::move(installation), std::move(action), std::move(captured)}));
  } catch (const BotComponentRuntimeError &) {
    return reject_mutation(
        Code::UnsupportedAction,
        "standard event action native transport unavailable");
  } catch (...) {
    return reject_mutation(
        Code::InvalidRequest,
        "standard event action preparation failed validation");
  }
}

auto OneBot11OperationsComponent::Endpoint::
    execute_standard_set_friend_add_request(
        const standard::SetFriendAddRequestRequest &request)
        -> boost::asio::awaitable<StructuredOperationCompletion> {
  using Code = bot::BotOperationErrorCode;
  const auto deadline =
      standard_detail::StandardRequestAccess::deadline(request);
  if (!deadline || std::chrono::steady_clock::now() >= *deadline)
    return reject_mutation(Code::Cancelled,
                           "standard event action deadline elapsed");
  auto identity = self_identity_;
  auto facade =
      std::dynamic_pointer_cast<OneBot11TransportCapability>(transport_);
  if (!facade)
    return reject_mutation(Code::UnsupportedAction,
                           "standard event action requires native transport");
  try {
    request.validate();
    const auto &context = standard_detail::StandardEventAccess::context(
        request.parameters.source);
    if (!binding_ || context.installation != installation_ ||
        context.identity != identity)
      return reject_mutation(Code::InvalidRequest,
                             "standard event action installation mismatch");
    standard_detail::StandardEventAccess::validate(request.parameters.source);
    codec().validate(installation_);
    const auto parameters = standard_detail::encode_event_action(request);
    auto serialized = serialize_mutation_parameters(
        parameters, request.control.limits.maximum_parameter_bytes);
    auto call = std::make_shared<standard_detail::StandardCallState>(
        request.control.limits, *deadline);
    const auto mode = request.control.execution;
    const auto values = request.value_limits;
    auto installation = installation_;
    auto action =
        bot::OperationTraits<standard::SetFriendAddRequestRequest>::action();
    const auto echo = next_echo();
    // Never capture a fresh origin for work backed by a received event.
    auto captured = standard_detail::StandardTransportAccess::send(
        *facade, standard_detail::NativeAction::SetFriendAddRequest, mode,
        std::move(serialized), echo, call, identity, context.origin);
    return finish_mutation(std::make_shared<PreparedMutation>(PreparedMutation{
        std::move(facade), std::move(identity), std::move(call),
        standard_detail::NativeAction::SetFriendAddRequest, mode, values, echo,
        std::move(installation), std::move(action), std::move(captured)}));
  } catch (const BotComponentRuntimeError &) {
    return reject_mutation(
        Code::UnsupportedAction,
        "standard event action native transport unavailable");
  } catch (...) {
    return reject_mutation(
        Code::InvalidRequest,
        "standard event action preparation failed validation");
  }
}

auto OneBot11OperationsComponent::Endpoint::
    execute_standard_set_group_add_request(
        const standard::SetGroupAddRequestRequest &request)
        -> boost::asio::awaitable<StructuredOperationCompletion> {
  using Code = bot::BotOperationErrorCode;
  const auto deadline =
      standard_detail::StandardRequestAccess::deadline(request);
  if (!deadline || std::chrono::steady_clock::now() >= *deadline)
    return reject_mutation(Code::Cancelled,
                           "standard event action deadline elapsed");
  auto identity = self_identity_;
  auto facade =
      std::dynamic_pointer_cast<OneBot11TransportCapability>(transport_);
  if (!facade)
    return reject_mutation(Code::UnsupportedAction,
                           "standard event action requires native transport");
  try {
    request.validate();
    const auto &context = standard_detail::StandardEventAccess::context(
        request.parameters.source);
    if (!binding_ || context.installation != installation_ ||
        context.identity != identity)
      return reject_mutation(Code::InvalidRequest,
                             "standard event action installation mismatch");
    standard_detail::StandardEventAccess::validate(request.parameters.source);
    codec().validate(installation_);
    const auto parameters = standard_detail::encode_event_action(request);
    auto serialized = serialize_mutation_parameters(
        parameters, request.control.limits.maximum_parameter_bytes);
    auto call = std::make_shared<standard_detail::StandardCallState>(
        request.control.limits, *deadline);
    const auto mode = request.control.execution;
    const auto values = request.value_limits;
    auto installation = installation_;
    auto action =
        bot::OperationTraits<standard::SetGroupAddRequestRequest>::action();
    const auto echo = next_echo();
    // Never capture a fresh origin for work backed by a received event.
    auto captured = standard_detail::StandardTransportAccess::send(
        *facade, standard_detail::NativeAction::SetGroupAddRequest, mode,
        std::move(serialized), echo, call, identity, context.origin);
    return finish_mutation(std::make_shared<PreparedMutation>(PreparedMutation{
        std::move(facade), std::move(identity), std::move(call),
        standard_detail::NativeAction::SetGroupAddRequest, mode, values, echo,
        std::move(installation), std::move(action), std::move(captured)}));
  } catch (const BotComponentRuntimeError &) {
    return reject_mutation(
        Code::UnsupportedAction,
        "standard event action native transport unavailable");
  } catch (...) {
    return reject_mutation(
        Code::InvalidRequest,
        "standard event action preparation failed validation");
  }
}

auto OneBot11OperationsComponent::Endpoint::
    execute_standard_handle_quick_operation(
        const standard::HandleQuickOperationRequest &request)
        -> boost::asio::awaitable<StructuredOperationCompletion> {
  using Code = bot::BotOperationErrorCode;
  const auto deadline =
      standard_detail::StandardRequestAccess::deadline(request);
  if (!deadline || std::chrono::steady_clock::now() >= *deadline)
    return reject_mutation(Code::Cancelled,
                           "standard event action deadline elapsed");
  auto identity = self_identity_;
  auto facade =
      std::dynamic_pointer_cast<OneBot11TransportCapability>(transport_);
  if (!facade)
    return reject_mutation(Code::UnsupportedAction,
                           "standard event action requires native transport");
  try {
    request.validate();
    const auto &context = standard_detail::StandardEventAccess::context(
        request.parameters.source);
    if (!binding_ || context.installation != installation_ ||
        context.identity != identity)
      return reject_mutation(Code::InvalidRequest,
                             "standard event action installation mismatch");
    standard_detail::StandardEventAccess::validate(request.parameters.source);
    codec().validate(installation_);
    const auto parameters = standard_detail::encode_event_action(request);
    auto serialized = serialize_mutation_parameters(
        parameters, request.control.limits.maximum_parameter_bytes);
    auto call = std::make_shared<standard_detail::StandardCallState>(
        request.control.limits, *deadline);
    const auto mode = request.control.execution;
    const auto values = request.value_limits;
    auto installation = installation_;
    auto action =
        bot::OperationTraits<standard::HandleQuickOperationRequest>::action();
    const auto echo = next_echo();
    // Never capture a fresh origin for work backed by a received event.
    auto captured = standard_detail::StandardTransportAccess::send(
        *facade, standard_detail::NativeAction::HandleQuickOperation, mode,
        std::move(serialized), echo, call, identity, context.origin);
    return finish_mutation(std::make_shared<PreparedMutation>(PreparedMutation{
        std::move(facade), std::move(identity), std::move(call),
        standard_detail::NativeAction::HandleQuickOperation, mode, values, echo,
        std::move(installation), std::move(action), std::move(captured)}));
  } catch (const BotComponentRuntimeError &) {
    return reject_mutation(
        Code::UnsupportedAction,
        "standard event action native transport unavailable");
  } catch (...) {
    return reject_mutation(
        Code::InvalidRequest,
        "standard event action preparation failed validation");
  }
}

auto OneBot11OperationsComponent::Endpoint::execute_standard_set_group_ban(
    const standard::SetGroupBanRequest &request)
    -> boost::asio::awaitable<StructuredOperationCompletion> {
  // Synchronous preparation: no borrowed request or Endpoint crosses await.
  using Code = bot::BotOperationErrorCode;
  auto identity = self_identity_;
  const auto origin = identity ? identity->capture_origin() : std::nullopt;
  const auto deadline =
      standard_detail::StandardRequestAccess::deadline(request);
  if (!deadline || std::chrono::steady_clock::now() >= *deadline)
    return reject_mutation(
        Code::Cancelled,
        "standard set_group_ban dispatch deadline unavailable or elapsed");
  if (!origin)
    return reject_mutation(Code::TransportFailure,
                           "standard set_group_ban authority is unavailable");
  auto facade =
      std::dynamic_pointer_cast<OneBot11TransportCapability>(transport_);
  if (!facade)
    return reject_mutation(Code::UnsupportedAction,
                           "standard set_group_ban requires native transport");
  try {
    request.validate();
    if (!binding_ || request.parameters.group.installation != installation_)
      return reject_mutation(Code::InvalidRequest,
                             "standard set_group_ban installation is invalid");
    codec().validate(installation_);
    codec().validate(request.parameters.group);
    codec().validate(request.parameters.user);
    const auto parameters = standard_detail::encode_parameters(
        request.parameters, installation_, request.value_limits);
    auto serialized = serialize_mutation_parameters(
        parameters, request.control.limits.maximum_parameter_bytes);
    auto call = std::make_shared<standard_detail::StandardCallState>(
        request.control.limits, *deadline);
    const auto mode = request.control.execution;
    const auto values = request.value_limits;
    auto installation = installation_;
    auto action = bot::OperationTraits<standard::SetGroupBanRequest>::action();
    const auto echo = next_echo();
    auto captured = standard_detail::StandardTransportAccess::send(
        *facade, standard_detail::NativeAction::SetGroupBan, mode,
        std::move(serialized), echo, call, identity, *origin);
    return finish_mutation(std::make_shared<PreparedMutation>(PreparedMutation{
        std::move(facade), std::move(identity), std::move(call),
        standard_detail::NativeAction::SetGroupBan, mode, values, echo,
        std::move(installation), std::move(action), std::move(captured)}));
  } catch (const BotComponentRuntimeError &) {
    return reject_mutation(
        Code::UnsupportedAction,
        "standard set_group_ban native transport is unavailable");
  } catch (...) {
    return reject_mutation(
        Code::InvalidRequest,
        "standard set_group_ban preparation failed validation");
  }
}

auto OneBot11OperationsComponent::Endpoint::
    execute_standard_set_group_whole_ban(
        const standard::SetGroupWholeBanRequest &request)
        -> boost::asio::awaitable<StructuredOperationCompletion> {
  // Synchronous preparation: no borrowed request or Endpoint crosses await.
  using Code = bot::BotOperationErrorCode;
  auto identity = self_identity_;
  const auto origin = identity ? identity->capture_origin() : std::nullopt;
  const auto deadline =
      standard_detail::StandardRequestAccess::deadline(request);
  if (!deadline || std::chrono::steady_clock::now() >= *deadline)
    return reject_mutation(Code::Cancelled,
                           "standard set_group_whole_ban dispatch deadline "
                           "unavailable or elapsed");
  if (!origin)
    return reject_mutation(
        Code::TransportFailure,
        "standard set_group_whole_ban authority is unavailable");
  auto facade =
      std::dynamic_pointer_cast<OneBot11TransportCapability>(transport_);
  if (!facade)
    return reject_mutation(
        Code::UnsupportedAction,
        "standard set_group_whole_ban requires native transport");
  try {
    request.validate();
    if (!binding_ || request.parameters.group.installation != installation_)
      return reject_mutation(
          Code::InvalidRequest,
          "standard set_group_whole_ban installation is invalid");
    codec().validate(installation_);
    codec().validate(request.parameters.group);
    const auto parameters = standard_detail::encode_parameters(
        request.parameters, installation_, request.value_limits);
    auto serialized = serialize_mutation_parameters(
        parameters, request.control.limits.maximum_parameter_bytes);
    auto call = std::make_shared<standard_detail::StandardCallState>(
        request.control.limits, *deadline);
    const auto mode = request.control.execution;
    const auto values = request.value_limits;
    auto installation = installation_;
    auto action =
        bot::OperationTraits<standard::SetGroupWholeBanRequest>::action();
    const auto echo = next_echo();
    auto captured = standard_detail::StandardTransportAccess::send(
        *facade, standard_detail::NativeAction::SetGroupWholeBan, mode,
        std::move(serialized), echo, call, identity, *origin);
    return finish_mutation(std::make_shared<PreparedMutation>(PreparedMutation{
        std::move(facade), std::move(identity), std::move(call),
        standard_detail::NativeAction::SetGroupWholeBan, mode, values, echo,
        std::move(installation), std::move(action), std::move(captured)}));
  } catch (const BotComponentRuntimeError &) {
    return reject_mutation(
        Code::UnsupportedAction,
        "standard set_group_whole_ban native transport is unavailable");
  } catch (...) {
    return reject_mutation(
        Code::InvalidRequest,
        "standard set_group_whole_ban preparation failed validation");
  }
}

auto OneBot11OperationsComponent::Endpoint::execute_standard_set_group_admin(
    const standard::SetGroupAdminRequest &request)
    -> boost::asio::awaitable<StructuredOperationCompletion> {
  // Synchronous preparation: no borrowed request or Endpoint crosses await.
  using Code = bot::BotOperationErrorCode;
  auto identity = self_identity_;
  const auto origin = identity ? identity->capture_origin() : std::nullopt;
  const auto deadline =
      standard_detail::StandardRequestAccess::deadline(request);
  if (!deadline || std::chrono::steady_clock::now() >= *deadline)
    return reject_mutation(
        Code::Cancelled,
        "standard set_group_admin dispatch deadline unavailable or elapsed");
  if (!origin)
    return reject_mutation(Code::TransportFailure,
                           "standard set_group_admin authority is unavailable");
  auto facade =
      std::dynamic_pointer_cast<OneBot11TransportCapability>(transport_);
  if (!facade)
    return reject_mutation(
        Code::UnsupportedAction,
        "standard set_group_admin requires native transport");
  try {
    request.validate();
    if (!binding_ || request.parameters.group.installation != installation_)
      return reject_mutation(
          Code::InvalidRequest,
          "standard set_group_admin installation is invalid");
    codec().validate(installation_);
    codec().validate(request.parameters.group);
    codec().validate(request.parameters.user);
    const auto parameters = standard_detail::encode_parameters(
        request.parameters, installation_, request.value_limits);
    auto serialized = serialize_mutation_parameters(
        parameters, request.control.limits.maximum_parameter_bytes);
    auto call = std::make_shared<standard_detail::StandardCallState>(
        request.control.limits, *deadline);
    const auto mode = request.control.execution;
    const auto values = request.value_limits;
    auto installation = installation_;
    auto action =
        bot::OperationTraits<standard::SetGroupAdminRequest>::action();
    const auto echo = next_echo();
    auto captured = standard_detail::StandardTransportAccess::send(
        *facade, standard_detail::NativeAction::SetGroupAdmin, mode,
        std::move(serialized), echo, call, identity, *origin);
    return finish_mutation(std::make_shared<PreparedMutation>(PreparedMutation{
        std::move(facade), std::move(identity), std::move(call),
        standard_detail::NativeAction::SetGroupAdmin, mode, values, echo,
        std::move(installation), std::move(action), std::move(captured)}));
  } catch (const BotComponentRuntimeError &) {
    return reject_mutation(
        Code::UnsupportedAction,
        "standard set_group_admin native transport is unavailable");
  } catch (...) {
    return reject_mutation(
        Code::InvalidRequest,
        "standard set_group_admin preparation failed validation");
  }
}

auto OneBot11OperationsComponent::Endpoint::
    execute_standard_set_group_anonymous(
        const standard::SetGroupAnonymousRequest &request)
        -> boost::asio::awaitable<StructuredOperationCompletion> {
  // Synchronous preparation: no borrowed request or Endpoint crosses await.
  using Code = bot::BotOperationErrorCode;
  auto identity = self_identity_;
  const auto origin = identity ? identity->capture_origin() : std::nullopt;
  const auto deadline =
      standard_detail::StandardRequestAccess::deadline(request);
  if (!deadline || std::chrono::steady_clock::now() >= *deadline)
    return reject_mutation(Code::Cancelled,
                           "standard set_group_anonymous dispatch deadline "
                           "unavailable or elapsed");
  if (!origin)
    return reject_mutation(
        Code::TransportFailure,
        "standard set_group_anonymous authority is unavailable");
  auto facade =
      std::dynamic_pointer_cast<OneBot11TransportCapability>(transport_);
  if (!facade)
    return reject_mutation(
        Code::UnsupportedAction,
        "standard set_group_anonymous requires native transport");
  try {
    request.validate();
    if (!binding_ || request.parameters.group.installation != installation_)
      return reject_mutation(
          Code::InvalidRequest,
          "standard set_group_anonymous installation is invalid");
    codec().validate(installation_);
    codec().validate(request.parameters.group);
    const auto parameters = standard_detail::encode_parameters(
        request.parameters, installation_, request.value_limits);
    auto serialized = serialize_mutation_parameters(
        parameters, request.control.limits.maximum_parameter_bytes);
    auto call = std::make_shared<standard_detail::StandardCallState>(
        request.control.limits, *deadline);
    const auto mode = request.control.execution;
    const auto values = request.value_limits;
    auto installation = installation_;
    auto action =
        bot::OperationTraits<standard::SetGroupAnonymousRequest>::action();
    const auto echo = next_echo();
    auto captured = standard_detail::StandardTransportAccess::send(
        *facade, standard_detail::NativeAction::SetGroupAnonymous, mode,
        std::move(serialized), echo, call, identity, *origin);
    return finish_mutation(std::make_shared<PreparedMutation>(PreparedMutation{
        std::move(facade), std::move(identity), std::move(call),
        standard_detail::NativeAction::SetGroupAnonymous, mode, values, echo,
        std::move(installation), std::move(action), std::move(captured)}));
  } catch (const BotComponentRuntimeError &) {
    return reject_mutation(
        Code::UnsupportedAction,
        "standard set_group_anonymous native transport is unavailable");
  } catch (...) {
    return reject_mutation(
        Code::InvalidRequest,
        "standard set_group_anonymous preparation failed validation");
  }
}

auto OneBot11OperationsComponent::Endpoint::execute_standard_set_group_card(
    const standard::SetGroupCardRequest &request)
    -> boost::asio::awaitable<StructuredOperationCompletion> {
  // Synchronous preparation: no borrowed request or Endpoint crosses await.
  using Code = bot::BotOperationErrorCode;
  auto identity = self_identity_;
  const auto origin = identity ? identity->capture_origin() : std::nullopt;
  const auto deadline =
      standard_detail::StandardRequestAccess::deadline(request);
  if (!deadline || std::chrono::steady_clock::now() >= *deadline)
    return reject_mutation(
        Code::Cancelled,
        "standard set_group_card dispatch deadline unavailable or elapsed");
  if (!origin)
    return reject_mutation(Code::TransportFailure,
                           "standard set_group_card authority is unavailable");
  auto facade =
      std::dynamic_pointer_cast<OneBot11TransportCapability>(transport_);
  if (!facade)
    return reject_mutation(Code::UnsupportedAction,
                           "standard set_group_card requires native transport");
  try {
    request.validate();
    if (!binding_ || request.parameters.group.installation != installation_)
      return reject_mutation(Code::InvalidRequest,
                             "standard set_group_card installation is invalid");
    codec().validate(installation_);
    codec().validate(request.parameters.group);
    codec().validate(request.parameters.user);
    const auto parameters = standard_detail::encode_parameters(
        request.parameters, installation_, request.value_limits);
    auto serialized = serialize_mutation_parameters(
        parameters, request.control.limits.maximum_parameter_bytes);
    auto call = std::make_shared<standard_detail::StandardCallState>(
        request.control.limits, *deadline);
    const auto mode = request.control.execution;
    const auto values = request.value_limits;
    auto installation = installation_;
    auto action = bot::OperationTraits<standard::SetGroupCardRequest>::action();
    const auto echo = next_echo();
    auto captured = standard_detail::StandardTransportAccess::send(
        *facade, standard_detail::NativeAction::SetGroupCard, mode,
        std::move(serialized), echo, call, identity, *origin);
    return finish_mutation(std::make_shared<PreparedMutation>(PreparedMutation{
        std::move(facade), std::move(identity), std::move(call),
        standard_detail::NativeAction::SetGroupCard, mode, values, echo,
        std::move(installation), std::move(action), std::move(captured)}));
  } catch (const BotComponentRuntimeError &) {
    return reject_mutation(
        Code::UnsupportedAction,
        "standard set_group_card native transport is unavailable");
  } catch (...) {
    return reject_mutation(
        Code::InvalidRequest,
        "standard set_group_card preparation failed validation");
  }
}

auto OneBot11OperationsComponent::Endpoint::execute_standard_set_group_name(
    const standard::SetGroupNameRequest &request)
    -> boost::asio::awaitable<StructuredOperationCompletion> {
  // Synchronous preparation: no borrowed request or Endpoint crosses await.
  using Code = bot::BotOperationErrorCode;
  auto identity = self_identity_;
  const auto origin = identity ? identity->capture_origin() : std::nullopt;
  const auto deadline =
      standard_detail::StandardRequestAccess::deadline(request);
  if (!deadline || std::chrono::steady_clock::now() >= *deadline)
    return reject_mutation(
        Code::Cancelled,
        "standard set_group_name dispatch deadline unavailable or elapsed");
  if (!origin)
    return reject_mutation(Code::TransportFailure,
                           "standard set_group_name authority is unavailable");
  auto facade =
      std::dynamic_pointer_cast<OneBot11TransportCapability>(transport_);
  if (!facade)
    return reject_mutation(Code::UnsupportedAction,
                           "standard set_group_name requires native transport");
  try {
    request.validate();
    if (!binding_ || request.parameters.group.installation != installation_)
      return reject_mutation(Code::InvalidRequest,
                             "standard set_group_name installation is invalid");
    codec().validate(installation_);
    codec().validate(request.parameters.group);
    const auto parameters = standard_detail::encode_parameters(
        request.parameters, installation_, request.value_limits);
    auto serialized = serialize_mutation_parameters(
        parameters, request.control.limits.maximum_parameter_bytes);
    auto call = std::make_shared<standard_detail::StandardCallState>(
        request.control.limits, *deadline);
    const auto mode = request.control.execution;
    const auto values = request.value_limits;
    auto installation = installation_;
    auto action = bot::OperationTraits<standard::SetGroupNameRequest>::action();
    const auto echo = next_echo();
    auto captured = standard_detail::StandardTransportAccess::send(
        *facade, standard_detail::NativeAction::SetGroupName, mode,
        std::move(serialized), echo, call, identity, *origin);
    return finish_mutation(std::make_shared<PreparedMutation>(PreparedMutation{
        std::move(facade), std::move(identity), std::move(call),
        standard_detail::NativeAction::SetGroupName, mode, values, echo,
        std::move(installation), std::move(action), std::move(captured)}));
  } catch (const BotComponentRuntimeError &) {
    return reject_mutation(
        Code::UnsupportedAction,
        "standard set_group_name native transport is unavailable");
  } catch (...) {
    return reject_mutation(
        Code::InvalidRequest,
        "standard set_group_name preparation failed validation");
  }
}

auto OneBot11OperationsComponent::Endpoint::execute_standard_set_group_leave(
    const standard::SetGroupLeaveRequest &request)
    -> boost::asio::awaitable<StructuredOperationCompletion> {
  // Synchronous preparation: no borrowed request or Endpoint crosses await.
  using Code = bot::BotOperationErrorCode;
  auto identity = self_identity_;
  const auto origin = identity ? identity->capture_origin() : std::nullopt;
  const auto deadline =
      standard_detail::StandardRequestAccess::deadline(request);
  if (!deadline || std::chrono::steady_clock::now() >= *deadline)
    return reject_mutation(
        Code::Cancelled,
        "standard set_group_leave dispatch deadline unavailable or elapsed");
  if (!origin)
    return reject_mutation(Code::TransportFailure,
                           "standard set_group_leave authority is unavailable");
  auto facade =
      std::dynamic_pointer_cast<OneBot11TransportCapability>(transport_);
  if (!facade)
    return reject_mutation(
        Code::UnsupportedAction,
        "standard set_group_leave requires native transport");
  try {
    request.validate();
    if (!binding_ || request.parameters.group.installation != installation_)
      return reject_mutation(
          Code::InvalidRequest,
          "standard set_group_leave installation is invalid");
    codec().validate(installation_);
    codec().validate(request.parameters.group);
    const auto parameters = standard_detail::encode_parameters(
        request.parameters, installation_, request.value_limits);
    auto serialized = serialize_mutation_parameters(
        parameters, request.control.limits.maximum_parameter_bytes);
    auto call = std::make_shared<standard_detail::StandardCallState>(
        request.control.limits, *deadline);
    const auto mode = request.control.execution;
    const auto values = request.value_limits;
    auto installation = installation_;
    auto action =
        bot::OperationTraits<standard::SetGroupLeaveRequest>::action();
    const auto echo = next_echo();
    auto captured = standard_detail::StandardTransportAccess::send(
        *facade, standard_detail::NativeAction::SetGroupLeave, mode,
        std::move(serialized), echo, call, identity, *origin);
    return finish_mutation(std::make_shared<PreparedMutation>(PreparedMutation{
        std::move(facade), std::move(identity), std::move(call),
        standard_detail::NativeAction::SetGroupLeave, mode, values, echo,
        std::move(installation), std::move(action), std::move(captured)}));
  } catch (const BotComponentRuntimeError &) {
    return reject_mutation(
        Code::UnsupportedAction,
        "standard set_group_leave native transport is unavailable");
  } catch (...) {
    return reject_mutation(
        Code::InvalidRequest,
        "standard set_group_leave preparation failed validation");
  }
}

auto OneBot11OperationsComponent::Endpoint::
    execute_standard_set_group_special_title(
        const standard::SetGroupSpecialTitleRequest &request)
        -> boost::asio::awaitable<StructuredOperationCompletion> {
  // Synchronous preparation: no borrowed request or Endpoint crosses await.
  using Code = bot::BotOperationErrorCode;
  auto identity = self_identity_;
  const auto origin = identity ? identity->capture_origin() : std::nullopt;
  const auto deadline =
      standard_detail::StandardRequestAccess::deadline(request);
  if (!deadline || std::chrono::steady_clock::now() >= *deadline)
    return reject_mutation(Code::Cancelled,
                           "standard set_group_special_title dispatch deadline "
                           "unavailable or elapsed");
  if (!origin)
    return reject_mutation(
        Code::TransportFailure,
        "standard set_group_special_title authority is unavailable");
  auto facade =
      std::dynamic_pointer_cast<OneBot11TransportCapability>(transport_);
  if (!facade)
    return reject_mutation(
        Code::UnsupportedAction,
        "standard set_group_special_title requires native transport");
  try {
    request.validate();
    if (!binding_ || request.parameters.group.installation != installation_)
      return reject_mutation(
          Code::InvalidRequest,
          "standard set_group_special_title installation is invalid");
    codec().validate(installation_);
    codec().validate(request.parameters.group);
    codec().validate(request.parameters.user);
    const auto parameters = standard_detail::encode_parameters(
        request.parameters, installation_, request.value_limits);
    auto serialized = serialize_mutation_parameters(
        parameters, request.control.limits.maximum_parameter_bytes);
    auto call = std::make_shared<standard_detail::StandardCallState>(
        request.control.limits, *deadline);
    const auto mode = request.control.execution;
    const auto values = request.value_limits;
    auto installation = installation_;
    auto action =
        bot::OperationTraits<standard::SetGroupSpecialTitleRequest>::action();
    const auto echo = next_echo();
    auto captured = standard_detail::StandardTransportAccess::send(
        *facade, standard_detail::NativeAction::SetGroupSpecialTitle, mode,
        std::move(serialized), echo, call, identity, *origin);
    return finish_mutation(std::make_shared<PreparedMutation>(PreparedMutation{
        std::move(facade), std::move(identity), std::move(call),
        standard_detail::NativeAction::SetGroupSpecialTitle, mode, values, echo,
        std::move(installation), std::move(action), std::move(captured)}));
  } catch (const BotComponentRuntimeError &) {
    return reject_mutation(
        Code::UnsupportedAction,
        "standard set_group_special_title native transport is unavailable");
  } catch (...) {
    return reject_mutation(
        Code::InvalidRequest,
        "standard set_group_special_title preparation failed validation");
  }
}

auto OneBot11OperationsComponent::Endpoint::supports_standard_send_messages()
    const -> bool {
  return std::dynamic_pointer_cast<OneBot11TransportCapability>(transport_) !=
         nullptr;
}

auto OneBot11OperationsComponent::Endpoint::execute_standard_send_private_msg(
    const standard::SendPrivateMsgRequest &request)
    -> boost::asio::awaitable<StructuredOperationCompletion> {
  // Synchronous preparation: no borrowed request or Endpoint crosses await.
  using Code = bot::BotOperationErrorCode;
  auto binding = binding_;
  auto identity = self_identity_;
  const auto origin = identity ? identity->capture_origin() : std::nullopt;
  const auto deadline =
      standard_detail::StandardRequestAccess::deadline(request);
  if (!deadline || std::chrono::steady_clock::now() >= *deadline)
    return reject_send_message(
        Code::Cancelled,
        "standard send_private_msg dispatch deadline unavailable or elapsed");
  if (!origin)
    return reject_send_message(
        Code::TransportFailure,
        "standard send_private_msg authority is unavailable");
  auto facade =
      std::dynamic_pointer_cast<OneBot11TransportCapability>(transport_);
  if (!facade)
    return reject_send_message(
        Code::UnsupportedAction,
        "standard send_private_msg requires native transport");
  try {
    request.validate();
    if (!binding || request.parameters.target.installation != installation_)
      return reject_send_message(
          Code::InvalidRequest,
          "standard send_private_msg installation is invalid");
    binding->codec().validate(installation_);
    binding->codec().validate(request.parameters.target);
    const auto parameters = standard_detail::encode_send_private_msg(
        request.parameters, binding->codec(), request.value_limits);
    auto serialized = serialize_mutation_parameters(
        parameters, request.control.limits.maximum_parameter_bytes);
    auto call = std::make_shared<standard_detail::StandardCallState>(
        request.control.limits, *deadline);
    const auto mode = request.control.execution;
    const auto values = request.value_limits;
    auto installation = installation_;
    auto action =
        bot::OperationTraits<standard::SendPrivateMsgRequest>::action();
    standard::SendPrivateMsgRequest validation_context = request;
    const auto echo = next_echo();
    auto captured = standard_detail::StandardTransportAccess::send(
        *facade, standard_detail::NativeAction::SendPrivateMsg, mode,
        std::move(serialized), echo, call, identity, *origin);
    return finish_send_message(
        std::make_shared<PreparedSendMessage<standard::SendPrivateMsgRequest>>(
            PreparedSendMessage<standard::SendPrivateMsgRequest>{
                std::move(binding), std::move(validation_context),
                std::move(facade), std::move(identity), std::move(call),
                standard_detail::NativeAction::SendPrivateMsg, mode, values,
                echo, std::move(installation), std::move(action),
                std::move(captured)}));
  } catch (const BotComponentRuntimeError &) {
    return reject_send_message(
        Code::UnsupportedAction,
        "standard send_private_msg native transport is unavailable");
  } catch (...) {
    return reject_send_message(
        Code::InvalidRequest,
        "standard send_private_msg preparation failed validation");
  }
}

auto OneBot11OperationsComponent::Endpoint::execute_standard_send_group_msg(
    const standard::SendGroupMsgRequest &request)
    -> boost::asio::awaitable<StructuredOperationCompletion> {
  // Synchronous preparation: no borrowed request or Endpoint crosses await.
  using Code = bot::BotOperationErrorCode;
  auto binding = binding_;
  auto identity = self_identity_;
  const auto origin = identity ? identity->capture_origin() : std::nullopt;
  const auto deadline =
      standard_detail::StandardRequestAccess::deadline(request);
  if (!deadline || std::chrono::steady_clock::now() >= *deadline)
    return reject_send_message(
        Code::Cancelled,
        "standard send_group_msg dispatch deadline unavailable or elapsed");
  if (!origin)
    return reject_send_message(
        Code::TransportFailure,
        "standard send_group_msg authority is unavailable");
  auto facade =
      std::dynamic_pointer_cast<OneBot11TransportCapability>(transport_);
  if (!facade)
    return reject_send_message(
        Code::UnsupportedAction,
        "standard send_group_msg requires native transport");
  try {
    request.validate();
    if (!binding || request.parameters.target.installation != installation_)
      return reject_send_message(
          Code::InvalidRequest,
          "standard send_group_msg installation is invalid");
    binding->codec().validate(installation_);
    binding->codec().validate(request.parameters.target);
    const auto parameters = standard_detail::encode_send_group_msg(
        request.parameters, binding->codec(), request.value_limits);
    auto serialized = serialize_mutation_parameters(
        parameters, request.control.limits.maximum_parameter_bytes);
    auto call = std::make_shared<standard_detail::StandardCallState>(
        request.control.limits, *deadline);
    const auto mode = request.control.execution;
    const auto values = request.value_limits;
    auto installation = installation_;
    auto action = bot::OperationTraits<standard::SendGroupMsgRequest>::action();
    standard::SendGroupMsgRequest validation_context = request;
    const auto echo = next_echo();
    auto captured = standard_detail::StandardTransportAccess::send(
        *facade, standard_detail::NativeAction::SendGroupMsg, mode,
        std::move(serialized), echo, call, identity, *origin);
    return finish_send_message(
        std::make_shared<PreparedSendMessage<standard::SendGroupMsgRequest>>(
            PreparedSendMessage<standard::SendGroupMsgRequest>{
                std::move(binding), std::move(validation_context),
                std::move(facade), std::move(identity), std::move(call),
                standard_detail::NativeAction::SendGroupMsg, mode, values, echo,
                std::move(installation), std::move(action),
                std::move(captured)}));
  } catch (const BotComponentRuntimeError &) {
    return reject_send_message(
        Code::UnsupportedAction,
        "standard send_group_msg native transport is unavailable");
  } catch (...) {
    return reject_send_message(
        Code::InvalidRequest,
        "standard send_group_msg preparation failed validation");
  }
}

auto OneBot11OperationsComponent::Endpoint::execute_standard_send_msg(
    const standard::SendMsgRequest &request)
    -> boost::asio::awaitable<StructuredOperationCompletion> {
  // Synchronous preparation: no borrowed request or Endpoint crosses await.
  using Code = bot::BotOperationErrorCode;
  auto binding = binding_;
  auto identity = self_identity_;
  const auto origin = identity ? identity->capture_origin() : std::nullopt;
  const auto deadline =
      standard_detail::StandardRequestAccess::deadline(request);
  if (!deadline || std::chrono::steady_clock::now() >= *deadline)
    return reject_send_message(
        Code::Cancelled,
        "standard send_msg dispatch deadline unavailable or elapsed");
  if (!origin)
    return reject_send_message(Code::TransportFailure,
                               "standard send_msg authority is unavailable");
  auto facade =
      std::dynamic_pointer_cast<OneBot11TransportCapability>(transport_);
  if (!facade)
    return reject_send_message(Code::UnsupportedAction,
                               "standard send_msg requires native transport");
  try {
    request.validate();
    if (!binding || request.parameters.target.installation != installation_)
      return reject_send_message(Code::InvalidRequest,
                                 "standard send_msg installation is invalid");
    binding->codec().validate(installation_);
    binding->codec().validate(request.parameters.target);
    const auto parameters = standard_detail::encode_send_msg(
        request.parameters, binding->codec(), request.value_limits);
    auto serialized = serialize_mutation_parameters(
        parameters, request.control.limits.maximum_parameter_bytes);
    auto call = std::make_shared<standard_detail::StandardCallState>(
        request.control.limits, *deadline);
    const auto mode = request.control.execution;
    const auto values = request.value_limits;
    auto installation = installation_;
    auto action = bot::OperationTraits<standard::SendMsgRequest>::action();
    standard::SendMsgRequest validation_context = request;
    const auto echo = next_echo();
    auto captured = standard_detail::StandardTransportAccess::send(
        *facade, standard_detail::NativeAction::SendMsg, mode,
        std::move(serialized), echo, call, identity, *origin);
    return finish_send_message(
        std::make_shared<PreparedSendMessage<standard::SendMsgRequest>>(
            PreparedSendMessage<standard::SendMsgRequest>{
                std::move(binding), std::move(validation_context),
                std::move(facade), std::move(identity), std::move(call),
                standard_detail::NativeAction::SendMsg, mode, values, echo,
                std::move(installation), std::move(action),
                std::move(captured)}));
  } catch (const BotComponentRuntimeError &) {
    return reject_send_message(
        Code::UnsupportedAction,
        "standard send_msg native transport is unavailable");
  } catch (...) {
    return reject_send_message(
        Code::InvalidRequest,
        "standard send_msg preparation failed validation");
  }
}

auto OneBot11OperationsComponent::Endpoint::execute_standard_get_msg(
    const standard::GetMsgRequest &request)
    -> boost::asio::awaitable<StructuredOperationCompletion> {
  // Synchronous preparation: no borrowed request or Endpoint crosses await.
  using Code = bot::BotOperationErrorCode;
  auto binding = binding_;
  auto identity = self_identity_;
  const auto origin = identity ? identity->capture_origin() : std::nullopt;
  const auto deadline =
      standard_detail::StandardRequestAccess::deadline(request);
  if (!deadline || std::chrono::steady_clock::now() >= *deadline)
    return reject_query(
        Code::Cancelled,
        "standard get_msg dispatch deadline unavailable or elapsed");
  if (!origin)
    return reject_query(Code::TransportFailure,
                        "standard get_msg authority is unavailable");
  auto facade =
      std::dynamic_pointer_cast<OneBot11TransportCapability>(transport_);
  if (!facade)
    return reject_query(Code::UnsupportedAction,
                        "standard get_msg requires native transport");
  try {
    request.validate();
    if (!binding || request.parameters.message.installation() != installation_)
      return reject_query(Code::InvalidRequest,
                          "standard get_msg installation is invalid");
    binding->codec().validate(installation_);
    const auto parameters =
        standard_detail::encode_get_msg(request.parameters, installation_,
                                        binding->codec(), request.value_limits);
    auto serialized = serialize_mutation_parameters(
        parameters, request.control.limits.maximum_parameter_bytes);
    auto call = std::make_shared<standard_detail::StandardCallState>(
        request.control.limits, *deadline);
    const auto mode = request.control.execution;
    const auto values = request.value_limits;
    auto installation = installation_;
    auto action = bot::OperationTraits<standard::GetMsgRequest>::action();
    standard::GetMsgRequest validation_context = request;
    const auto echo = next_echo();
    auto captured = standard_detail::StandardTransportAccess::send(
        *facade, standard_detail::NativeAction::GetMsg, mode,
        std::move(serialized), echo, call, identity, *origin);
    return finish_query(
        std::make_shared<PreparedQuery<standard::GetMsgRequest>>(
            PreparedQuery<standard::GetMsgRequest>{
                std::move(binding), std::move(validation_context),
                std::move(facade), std::move(identity), std::move(call),
                standard_detail::NativeAction::GetMsg, mode, values, echo,
                std::move(installation), std::move(action),
                std::move(captured)}));
  } catch (const BotComponentRuntimeError &) {
    return reject_query(Code::UnsupportedAction,
                        "standard get_msg native transport is unavailable");
  } catch (...) {
    return reject_query(Code::InvalidRequest,
                        "standard get_msg preparation failed validation");
  }
}

auto OneBot11OperationsComponent::Endpoint::execute_standard_get_forward_msg(
    const standard::GetForwardMsgRequest &request)
    -> boost::asio::awaitable<StructuredOperationCompletion> {
  // Synchronous preparation: no borrowed request or Endpoint crosses await.
  using Code = bot::BotOperationErrorCode;
  auto binding = binding_;
  auto identity = self_identity_;
  const auto origin = identity ? identity->capture_origin() : std::nullopt;
  const auto deadline =
      standard_detail::StandardRequestAccess::deadline(request);
  if (!deadline || std::chrono::steady_clock::now() >= *deadline)
    return reject_query(
        Code::Cancelled,
        "standard get_forward_msg dispatch deadline unavailable or elapsed");
  if (!origin)
    return reject_query(Code::TransportFailure,
                        "standard get_forward_msg authority is unavailable");
  auto facade =
      std::dynamic_pointer_cast<OneBot11TransportCapability>(transport_);
  if (!facade)
    return reject_query(Code::UnsupportedAction,
                        "standard get_forward_msg requires native transport");
  try {
    request.validate();
    if (!binding || request.parameters.forward.installation() != installation_)
      return reject_query(Code::InvalidRequest,
                          "standard get_forward_msg installation is invalid");
    binding->codec().validate(installation_);
    const auto parameters = standard_detail::encode_get_forward_msg(
        request.parameters, installation_, binding->codec(),
        request.value_limits);
    auto serialized = serialize_mutation_parameters(
        parameters, request.control.limits.maximum_parameter_bytes);
    auto call = std::make_shared<standard_detail::StandardCallState>(
        request.control.limits, *deadline);
    const auto mode = request.control.execution;
    const auto values = request.value_limits;
    auto installation = installation_;
    auto action =
        bot::OperationTraits<standard::GetForwardMsgRequest>::action();
    standard::GetForwardMsgRequest validation_context = request;
    const auto echo = next_echo();
    auto captured = standard_detail::StandardTransportAccess::send(
        *facade, standard_detail::NativeAction::GetForwardMsg, mode,
        std::move(serialized), echo, call, identity, *origin);
    return finish_query(
        std::make_shared<PreparedQuery<standard::GetForwardMsgRequest>>(
            PreparedQuery<standard::GetForwardMsgRequest>{
                std::move(binding), std::move(validation_context),
                std::move(facade), std::move(identity), std::move(call),
                standard_detail::NativeAction::GetForwardMsg, mode, values,
                echo, std::move(installation), std::move(action),
                std::move(captured)}));
  } catch (const BotComponentRuntimeError &) {
    return reject_query(
        Code::UnsupportedAction,
        "standard get_forward_msg native transport is unavailable");
  } catch (...) {
    return reject_query(
        Code::InvalidRequest,
        "standard get_forward_msg preparation failed validation");
  }
}

auto OneBot11OperationsComponent::Endpoint::supports_standard_queries() const
    -> bool {
  return std::dynamic_pointer_cast<OneBot11TransportCapability>(transport_) !=
         nullptr;
}

auto OneBot11OperationsComponent::Endpoint::execute_standard_get_stranger_info(
    const standard::GetStrangerInfoRequest &request)
    -> boost::asio::awaitable<StructuredOperationCompletion> {
  // Synchronous preparation: no borrowed request or Endpoint crosses await.
  using Code = bot::BotOperationErrorCode;
  auto binding = binding_;
  auto identity = self_identity_;
  const auto origin = identity ? identity->capture_origin() : std::nullopt;
  const auto deadline =
      standard_detail::StandardRequestAccess::deadline(request);
  if (!deadline || std::chrono::steady_clock::now() >= *deadline)
    return reject_query(
        Code::Cancelled,
        "standard get_stranger_info dispatch deadline unavailable or elapsed");
  if (!origin)
    return reject_query(Code::TransportFailure,
                        "standard get_stranger_info authority is unavailable");
  auto facade =
      std::dynamic_pointer_cast<OneBot11TransportCapability>(transport_);
  if (!facade)
    return reject_query(Code::UnsupportedAction,
                        "standard get_stranger_info requires native transport");
  try {
    request.validate();
    if (!binding || request.parameters.user.installation != installation_)
      return reject_query(Code::InvalidRequest,
                          "standard get_stranger_info installation is invalid");
    binding->codec().validate(installation_);
    binding->codec().validate(request.parameters.user);
    const auto parameters = standard_detail::encode_get_stranger_info(
        request.parameters, binding->codec(), request.value_limits);
    auto serialized = serialize_mutation_parameters(
        parameters, request.control.limits.maximum_parameter_bytes);
    auto call = std::make_shared<standard_detail::StandardCallState>(
        request.control.limits, *deadline);
    const auto mode = request.control.execution;
    const auto values = request.value_limits;
    auto installation = installation_;
    auto action =
        bot::OperationTraits<standard::GetStrangerInfoRequest>::action();
    standard::GetStrangerInfoRequest validation_context = request;
    const auto echo = next_echo();
    auto captured = standard_detail::StandardTransportAccess::send(
        *facade, standard_detail::NativeAction::GetStrangerInfo, mode,
        std::move(serialized), echo, call, identity, *origin);
    return finish_query(
        std::make_shared<PreparedQuery<standard::GetStrangerInfoRequest>>(
            PreparedQuery<standard::GetStrangerInfoRequest>{
                std::move(binding), std::move(validation_context),
                std::move(facade), std::move(identity), std::move(call),
                standard_detail::NativeAction::GetStrangerInfo, mode, values,
                echo, std::move(installation), std::move(action),
                std::move(captured)}));
  } catch (const BotComponentRuntimeError &) {
    return reject_query(
        Code::UnsupportedAction,
        "standard get_stranger_info native transport is unavailable");
  } catch (...) {
    return reject_query(
        Code::InvalidRequest,
        "standard get_stranger_info preparation failed validation");
  }
}

auto OneBot11OperationsComponent::Endpoint::supports_standard_login() const
    -> bool {
  return login_coordinator_ &&
         std::dynamic_pointer_cast<OneBot11TransportCapability>(transport_) !=
             nullptr;
}

auto OneBot11OperationsComponent::Endpoint::execute_standard_get_login_info(
    const standard::GetLoginInfoRequest &request)
    -> boost::asio::awaitable<StructuredOperationCompletion> {
  // Synchronous preparation: no borrowed request or Endpoint crosses await.
  using Code = bot::BotOperationErrorCode;
  auto binding = binding_;
  auto identity = self_identity_;
  auto coordinator = login_coordinator_;
  const auto origin =
      coordinator ? coordinator->capture_origin() : std::nullopt;
  const auto deadline =
      standard_detail::StandardRequestAccess::deadline(request);
  if (!deadline || std::chrono::steady_clock::now() >= *deadline)
    return reject_query(
        Code::Cancelled,
        "standard get_login_info dispatch deadline unavailable or elapsed");
  if (!origin)
    return reject_query(Code::TransportFailure,
                        "standard get_login_info authority is unavailable");
  auto facade =
      std::dynamic_pointer_cast<OneBot11TransportCapability>(transport_);
  if (!facade)
    return reject_query(Code::UnsupportedAction,
                        "standard get_login_info requires native transport");
  try {
    request.validate();
    if (!binding || request.parameters.installation != installation_)
      return reject_query(Code::InvalidRequest,
                          "standard get_login_info installation is invalid");
    binding->codec().validate(installation_);
    binding->codec().validate(request.parameters.installation);
    const auto parameters = standard_detail::encode_get_login_info(
        request.parameters, binding->codec(), request.value_limits);
    auto serialized = serialize_mutation_parameters(
        parameters, request.control.limits.maximum_parameter_bytes);
    auto call = std::make_shared<standard_detail::StandardCallState>(
        request.control.limits, *deadline);
    const auto mode = request.control.execution;
    const auto values = request.value_limits;
    auto installation = installation_;
    auto action = bot::OperationTraits<standard::GetLoginInfoRequest>::action();
    standard::GetLoginInfoRequest validation_context = request;
    const auto echo = next_echo();
    auto captured = coordinator->send(*facade, mode, std::move(serialized),
                                      echo, call, *origin);
    return finish_login(
        std::make_shared<PreparedQuery<standard::GetLoginInfoRequest>>(
            PreparedQuery<standard::GetLoginInfoRequest>{
                std::move(binding), std::move(validation_context),
                std::move(facade), std::move(identity), std::move(call),
                standard_detail::NativeAction::GetLoginInfo, mode, values, echo,
                std::move(installation), std::move(action),
                std::move(captured)}),
        std::move(coordinator), *origin);
  } catch (const BotComponentRuntimeError &) {
    return reject_query(
        Code::UnsupportedAction,
        "standard get_login_info native transport is unavailable");
  } catch (...) {
    return reject_query(
        Code::InvalidRequest,
        "standard get_login_info preparation failed validation");
  }
}

auto OneBot11OperationsComponent::Endpoint::execute_standard_get_friend_list(
    const standard::GetFriendListRequest &request)
    -> boost::asio::awaitable<StructuredOperationCompletion> {
  // Synchronous preparation: no borrowed request or Endpoint crosses await.
  using Code = bot::BotOperationErrorCode;
  auto binding = binding_;
  auto identity = self_identity_;
  const auto origin = identity ? identity->capture_origin() : std::nullopt;
  const auto deadline =
      standard_detail::StandardRequestAccess::deadline(request);
  if (!deadline || std::chrono::steady_clock::now() >= *deadline)
    return reject_query(
        Code::Cancelled,
        "standard get_friend_list dispatch deadline unavailable or elapsed");
  if (!origin)
    return reject_query(Code::TransportFailure,
                        "standard get_friend_list authority is unavailable");
  auto facade =
      std::dynamic_pointer_cast<OneBot11TransportCapability>(transport_);
  if (!facade)
    return reject_query(Code::UnsupportedAction,
                        "standard get_friend_list requires native transport");
  try {
    request.validate();
    if (!binding || request.parameters.installation != installation_)
      return reject_query(Code::InvalidRequest,
                          "standard get_friend_list installation is invalid");
    binding->codec().validate(installation_);
    binding->codec().validate(request.parameters.installation);
    const auto parameters = standard_detail::encode_get_friend_list(
        request.parameters, binding->codec(), request.value_limits);
    auto serialized = serialize_mutation_parameters(
        parameters, request.control.limits.maximum_parameter_bytes);
    auto call = std::make_shared<standard_detail::StandardCallState>(
        request.control.limits, *deadline);
    const auto mode = request.control.execution;
    const auto values = request.value_limits;
    auto installation = installation_;
    auto action =
        bot::OperationTraits<standard::GetFriendListRequest>::action();
    standard::GetFriendListRequest validation_context = request;
    const auto echo = next_echo();
    auto captured = standard_detail::StandardTransportAccess::send(
        *facade, standard_detail::NativeAction::GetFriendList, mode,
        std::move(serialized), echo, call, identity, *origin);
    return finish_query(
        std::make_shared<PreparedQuery<standard::GetFriendListRequest>>(
            PreparedQuery<standard::GetFriendListRequest>{
                std::move(binding), std::move(validation_context),
                std::move(facade), std::move(identity), std::move(call),
                standard_detail::NativeAction::GetFriendList, mode, values,
                echo, std::move(installation), std::move(action),
                std::move(captured)}));
  } catch (const BotComponentRuntimeError &) {
    return reject_query(
        Code::UnsupportedAction,
        "standard get_friend_list native transport is unavailable");
  } catch (...) {
    return reject_query(
        Code::InvalidRequest,
        "standard get_friend_list preparation failed validation");
  }
}

auto OneBot11OperationsComponent::Endpoint::execute_standard_get_group_info(
    const standard::GetGroupInfoRequest &request)
    -> boost::asio::awaitable<StructuredOperationCompletion> {
  // Synchronous preparation: no borrowed request or Endpoint crosses await.
  using Code = bot::BotOperationErrorCode;
  auto binding = binding_;
  auto identity = self_identity_;
  const auto origin = identity ? identity->capture_origin() : std::nullopt;
  const auto deadline =
      standard_detail::StandardRequestAccess::deadline(request);
  if (!deadline || std::chrono::steady_clock::now() >= *deadline)
    return reject_query(
        Code::Cancelled,
        "standard get_group_info dispatch deadline unavailable or elapsed");
  if (!origin)
    return reject_query(Code::TransportFailure,
                        "standard get_group_info authority is unavailable");
  auto facade =
      std::dynamic_pointer_cast<OneBot11TransportCapability>(transport_);
  if (!facade)
    return reject_query(Code::UnsupportedAction,
                        "standard get_group_info requires native transport");
  try {
    request.validate();
    if (!binding || request.parameters.group.installation != installation_)
      return reject_query(Code::InvalidRequest,
                          "standard get_group_info installation is invalid");
    binding->codec().validate(installation_);
    binding->codec().validate(request.parameters.group);
    const auto parameters = standard_detail::encode_get_group_info(
        request.parameters, binding->codec(), request.value_limits);
    auto serialized = serialize_mutation_parameters(
        parameters, request.control.limits.maximum_parameter_bytes);
    auto call = std::make_shared<standard_detail::StandardCallState>(
        request.control.limits, *deadline);
    const auto mode = request.control.execution;
    const auto values = request.value_limits;
    auto installation = installation_;
    auto action = bot::OperationTraits<standard::GetGroupInfoRequest>::action();
    standard::GetGroupInfoRequest validation_context = request;
    const auto echo = next_echo();
    auto captured = standard_detail::StandardTransportAccess::send(
        *facade, standard_detail::NativeAction::GetGroupInfo, mode,
        std::move(serialized), echo, call, identity, *origin);
    return finish_query(
        std::make_shared<PreparedQuery<standard::GetGroupInfoRequest>>(
            PreparedQuery<standard::GetGroupInfoRequest>{
                std::move(binding), std::move(validation_context),
                std::move(facade), std::move(identity), std::move(call),
                standard_detail::NativeAction::GetGroupInfo, mode, values, echo,
                std::move(installation), std::move(action),
                std::move(captured)}));
  } catch (const BotComponentRuntimeError &) {
    return reject_query(
        Code::UnsupportedAction,
        "standard get_group_info native transport is unavailable");
  } catch (...) {
    return reject_query(
        Code::InvalidRequest,
        "standard get_group_info preparation failed validation");
  }
}

auto OneBot11OperationsComponent::Endpoint::execute_standard_get_group_list(
    const standard::GetGroupListRequest &request)
    -> boost::asio::awaitable<StructuredOperationCompletion> {
  // Synchronous preparation: no borrowed request or Endpoint crosses await.
  using Code = bot::BotOperationErrorCode;
  auto binding = binding_;
  auto identity = self_identity_;
  const auto origin = identity ? identity->capture_origin() : std::nullopt;
  const auto deadline =
      standard_detail::StandardRequestAccess::deadline(request);
  if (!deadline || std::chrono::steady_clock::now() >= *deadline)
    return reject_query(
        Code::Cancelled,
        "standard get_group_list dispatch deadline unavailable or elapsed");
  if (!origin)
    return reject_query(Code::TransportFailure,
                        "standard get_group_list authority is unavailable");
  auto facade =
      std::dynamic_pointer_cast<OneBot11TransportCapability>(transport_);
  if (!facade)
    return reject_query(Code::UnsupportedAction,
                        "standard get_group_list requires native transport");
  try {
    request.validate();
    if (!binding || request.parameters.installation != installation_)
      return reject_query(Code::InvalidRequest,
                          "standard get_group_list installation is invalid");
    binding->codec().validate(installation_);
    binding->codec().validate(request.parameters.installation);
    const auto parameters = standard_detail::encode_get_group_list(
        request.parameters, binding->codec(), request.value_limits);
    auto serialized = serialize_mutation_parameters(
        parameters, request.control.limits.maximum_parameter_bytes);
    auto call = std::make_shared<standard_detail::StandardCallState>(
        request.control.limits, *deadline);
    const auto mode = request.control.execution;
    const auto values = request.value_limits;
    auto installation = installation_;
    auto action = bot::OperationTraits<standard::GetGroupListRequest>::action();
    standard::GetGroupListRequest validation_context = request;
    const auto echo = next_echo();
    auto captured = standard_detail::StandardTransportAccess::send(
        *facade, standard_detail::NativeAction::GetGroupList, mode,
        std::move(serialized), echo, call, identity, *origin);
    return finish_query(
        std::make_shared<PreparedQuery<standard::GetGroupListRequest>>(
            PreparedQuery<standard::GetGroupListRequest>{
                std::move(binding), std::move(validation_context),
                std::move(facade), std::move(identity), std::move(call),
                standard_detail::NativeAction::GetGroupList, mode, values, echo,
                std::move(installation), std::move(action),
                std::move(captured)}));
  } catch (const BotComponentRuntimeError &) {
    return reject_query(
        Code::UnsupportedAction,
        "standard get_group_list native transport is unavailable");
  } catch (...) {
    return reject_query(
        Code::InvalidRequest,
        "standard get_group_list preparation failed validation");
  }
}

auto OneBot11OperationsComponent::Endpoint::
    execute_standard_get_group_member_info(
        const standard::GetGroupMemberInfoRequest &request)
        -> boost::asio::awaitable<StructuredOperationCompletion> {
  // Synchronous preparation: no borrowed request or Endpoint crosses await.
  using Code = bot::BotOperationErrorCode;
  auto binding = binding_;
  auto identity = self_identity_;
  const auto origin = identity ? identity->capture_origin() : std::nullopt;
  const auto deadline =
      standard_detail::StandardRequestAccess::deadline(request);
  if (!deadline || std::chrono::steady_clock::now() >= *deadline)
    return reject_query(Code::Cancelled,
                        "standard get_group_member_info dispatch deadline "
                        "unavailable or elapsed");
  if (!origin)
    return reject_query(
        Code::TransportFailure,
        "standard get_group_member_info authority is unavailable");
  auto facade =
      std::dynamic_pointer_cast<OneBot11TransportCapability>(transport_);
  if (!facade)
    return reject_query(
        Code::UnsupportedAction,
        "standard get_group_member_info requires native transport");
  try {
    request.validate();
    if (!binding || request.parameters.group.installation != installation_)
      return reject_query(
          Code::InvalidRequest,
          "standard get_group_member_info installation is invalid");
    binding->codec().validate(installation_);
    binding->codec().validate(request.parameters.group);
    binding->codec().validate(request.parameters.user);
    const auto parameters = standard_detail::encode_get_group_member_info(
        request.parameters, binding->codec(), request.value_limits);
    auto serialized = serialize_mutation_parameters(
        parameters, request.control.limits.maximum_parameter_bytes);
    auto call = std::make_shared<standard_detail::StandardCallState>(
        request.control.limits, *deadline);
    const auto mode = request.control.execution;
    const auto values = request.value_limits;
    auto installation = installation_;
    auto action =
        bot::OperationTraits<standard::GetGroupMemberInfoRequest>::action();
    standard::GetGroupMemberInfoRequest validation_context = request;
    const auto echo = next_echo();
    auto captured = standard_detail::StandardTransportAccess::send(
        *facade, standard_detail::NativeAction::GetGroupMemberInfo, mode,
        std::move(serialized), echo, call, identity, *origin);
    return finish_query(
        std::make_shared<PreparedQuery<standard::GetGroupMemberInfoRequest>>(
            PreparedQuery<standard::GetGroupMemberInfoRequest>{
                std::move(binding), std::move(validation_context),
                std::move(facade), std::move(identity), std::move(call),
                standard_detail::NativeAction::GetGroupMemberInfo, mode, values,
                echo, std::move(installation), std::move(action),
                std::move(captured)}));
  } catch (const BotComponentRuntimeError &) {
    return reject_query(
        Code::UnsupportedAction,
        "standard get_group_member_info native transport is unavailable");
  } catch (...) {
    return reject_query(
        Code::InvalidRequest,
        "standard get_group_member_info preparation failed validation");
  }
}

auto OneBot11OperationsComponent::Endpoint::
    execute_standard_get_group_member_list(
        const standard::GetGroupMemberListRequest &request)
        -> boost::asio::awaitable<StructuredOperationCompletion> {
  // Synchronous preparation: no borrowed request or Endpoint crosses await.
  using Code = bot::BotOperationErrorCode;
  auto binding = binding_;
  auto identity = self_identity_;
  const auto origin = identity ? identity->capture_origin() : std::nullopt;
  const auto deadline =
      standard_detail::StandardRequestAccess::deadline(request);
  if (!deadline || std::chrono::steady_clock::now() >= *deadline)
    return reject_query(Code::Cancelled,
                        "standard get_group_member_list dispatch deadline "
                        "unavailable or elapsed");
  if (!origin)
    return reject_query(
        Code::TransportFailure,
        "standard get_group_member_list authority is unavailable");
  auto facade =
      std::dynamic_pointer_cast<OneBot11TransportCapability>(transport_);
  if (!facade)
    return reject_query(
        Code::UnsupportedAction,
        "standard get_group_member_list requires native transport");
  try {
    request.validate();
    if (!binding || request.parameters.group.installation != installation_)
      return reject_query(
          Code::InvalidRequest,
          "standard get_group_member_list installation is invalid");
    binding->codec().validate(installation_);
    binding->codec().validate(request.parameters.group);
    const auto parameters = standard_detail::encode_get_group_member_list(
        request.parameters, binding->codec(), request.value_limits);
    auto serialized = serialize_mutation_parameters(
        parameters, request.control.limits.maximum_parameter_bytes);
    auto call = std::make_shared<standard_detail::StandardCallState>(
        request.control.limits, *deadline);
    const auto mode = request.control.execution;
    const auto values = request.value_limits;
    auto installation = installation_;
    auto action =
        bot::OperationTraits<standard::GetGroupMemberListRequest>::action();
    standard::GetGroupMemberListRequest validation_context = request;
    const auto echo = next_echo();
    auto captured = standard_detail::StandardTransportAccess::send(
        *facade, standard_detail::NativeAction::GetGroupMemberList, mode,
        std::move(serialized), echo, call, identity, *origin);
    return finish_query(
        std::make_shared<PreparedQuery<standard::GetGroupMemberListRequest>>(
            PreparedQuery<standard::GetGroupMemberListRequest>{
                std::move(binding), std::move(validation_context),
                std::move(facade), std::move(identity), std::move(call),
                standard_detail::NativeAction::GetGroupMemberList, mode, values,
                echo, std::move(installation), std::move(action),
                std::move(captured)}));
  } catch (const BotComponentRuntimeError &) {
    return reject_query(
        Code::UnsupportedAction,
        "standard get_group_member_list native transport is unavailable");
  } catch (...) {
    return reject_query(
        Code::InvalidRequest,
        "standard get_group_member_list preparation failed validation");
  }
}

auto OneBot11OperationsComponent::Endpoint::
    execute_standard_get_group_honor_info(
        const standard::GetGroupHonorInfoRequest &request)
        -> boost::asio::awaitable<StructuredOperationCompletion> {
  // Synchronous preparation: no borrowed request or Endpoint crosses await.
  using Code = bot::BotOperationErrorCode;
  auto binding = binding_;
  auto identity = self_identity_;
  const auto origin = identity ? identity->capture_origin() : std::nullopt;
  const auto deadline =
      standard_detail::StandardRequestAccess::deadline(request);
  if (!deadline || std::chrono::steady_clock::now() >= *deadline)
    return reject_query(Code::Cancelled,
                        "standard get_group_honor_info dispatch deadline "
                        "unavailable or elapsed");
  if (!origin)
    return reject_query(
        Code::TransportFailure,
        "standard get_group_honor_info authority is unavailable");
  auto facade =
      std::dynamic_pointer_cast<OneBot11TransportCapability>(transport_);
  if (!facade)
    return reject_query(
        Code::UnsupportedAction,
        "standard get_group_honor_info requires native transport");
  try {
    request.validate();
    if (!binding || request.parameters.group.installation != installation_)
      return reject_query(
          Code::InvalidRequest,
          "standard get_group_honor_info installation is invalid");
    binding->codec().validate(installation_);
    binding->codec().validate(request.parameters.group);
    const auto parameters = standard_detail::encode_get_group_honor_info(
        request.parameters, binding->codec(), request.value_limits);
    auto serialized = serialize_mutation_parameters(
        parameters, request.control.limits.maximum_parameter_bytes);
    auto call = std::make_shared<standard_detail::StandardCallState>(
        request.control.limits, *deadline);
    const auto mode = request.control.execution;
    const auto values = request.value_limits;
    auto installation = installation_;
    auto action =
        bot::OperationTraits<standard::GetGroupHonorInfoRequest>::action();
    standard::GetGroupHonorInfoRequest validation_context = request;
    const auto echo = next_echo();
    auto captured = standard_detail::StandardTransportAccess::send(
        *facade, standard_detail::NativeAction::GetGroupHonorInfo, mode,
        std::move(serialized), echo, call, identity, *origin);
    return finish_query(
        std::make_shared<PreparedQuery<standard::GetGroupHonorInfoRequest>>(
            PreparedQuery<standard::GetGroupHonorInfoRequest>{
                std::move(binding), std::move(validation_context),
                std::move(facade), std::move(identity), std::move(call),
                standard_detail::NativeAction::GetGroupHonorInfo, mode, values,
                echo, std::move(installation), std::move(action),
                std::move(captured)}));
  } catch (const BotComponentRuntimeError &) {
    return reject_query(
        Code::UnsupportedAction,
        "standard get_group_honor_info native transport is unavailable");
  } catch (...) {
    return reject_query(
        Code::InvalidRequest,
        "standard get_group_honor_info preparation failed validation");
  }
}

auto OneBot11OperationsComponent::Endpoint::supports_standard_services() const
    -> bool {
  return std::dynamic_pointer_cast<OneBot11TransportCapability>(transport_) !=
         nullptr;
}

auto OneBot11OperationsComponent::Endpoint::execute_standard_get_cookies(
    const standard::GetCookiesRequest &request)
    -> boost::asio::awaitable<StructuredOperationCompletion> {
  // Synchronous preparation: no borrowed request or Endpoint crosses await.
  using Code = bot::BotOperationErrorCode;
  auto binding = binding_;
  auto identity = self_identity_;
  const auto origin = identity ? identity->capture_origin() : std::nullopt;
  const auto deadline =
      standard_detail::StandardRequestAccess::deadline(request);
  if (!deadline || std::chrono::steady_clock::now() >= *deadline)
    return reject_service(
        Code::Cancelled,
        "standard get_cookies dispatch deadline unavailable or elapsed");
  if (!origin)
    return reject_service(Code::TransportFailure,
                          "standard get_cookies authority is unavailable");
  auto facade =
      std::dynamic_pointer_cast<OneBot11TransportCapability>(transport_);
  if (!facade)
    return reject_service(Code::UnsupportedAction,
                          "standard get_cookies requires native transport");
  try {
    request.validate();
    if (!binding || request.installation != installation_)
      return reject_service(Code::InvalidRequest,
                            "standard get_cookies installation is invalid");
    binding->codec().validate(installation_);
    binding->codec().validate(request.installation);
    const auto parameters = standard_detail::encode_parameters(
        request.parameters, request.value_limits);
    auto serialized = serialize_mutation_parameters(
        parameters, request.control.limits.maximum_parameter_bytes);
    auto call = std::make_shared<standard_detail::StandardCallState>(
        request.control.limits, *deadline);
    const auto mode = request.control.execution;
    const auto values = request.value_limits;
    auto installation = installation_;
    auto action = bot::OperationTraits<standard::GetCookiesRequest>::action();
    standard::GetCookiesRequest validation_context = request;
    const auto echo = next_echo();
    auto captured = standard_detail::StandardTransportAccess::send(
        *facade, standard_detail::NativeAction::GetCookies, mode,
        std::move(serialized), echo, call, identity, *origin);
    return finish_service(
        std::make_shared<PreparedService<standard::GetCookiesRequest>>(
            PreparedService<standard::GetCookiesRequest>{
                std::move(binding), std::move(validation_context),
                std::move(facade), std::move(identity), std::move(call),
                standard_detail::NativeAction::GetCookies, mode, values, echo,
                std::move(installation), std::move(action),
                std::move(captured)}));
  } catch (const BotComponentRuntimeError &) {
    return reject_service(
        Code::UnsupportedAction,
        "standard get_cookies native transport is unavailable");
  } catch (...) {
    return reject_service(Code::InvalidRequest,
                          "standard get_cookies preparation failed validation");
  }
}

auto OneBot11OperationsComponent::Endpoint::execute_standard_get_csrf_token(
    const standard::GetCsrfTokenRequest &request)
    -> boost::asio::awaitable<StructuredOperationCompletion> {
  // Synchronous preparation: no borrowed request or Endpoint crosses await.
  using Code = bot::BotOperationErrorCode;
  auto binding = binding_;
  auto identity = self_identity_;
  const auto origin = identity ? identity->capture_origin() : std::nullopt;
  const auto deadline =
      standard_detail::StandardRequestAccess::deadline(request);
  if (!deadline || std::chrono::steady_clock::now() >= *deadline)
    return reject_service(
        Code::Cancelled,
        "standard get_csrf_token dispatch deadline unavailable or elapsed");
  if (!origin)
    return reject_service(Code::TransportFailure,
                          "standard get_csrf_token authority is unavailable");
  auto facade =
      std::dynamic_pointer_cast<OneBot11TransportCapability>(transport_);
  if (!facade)
    return reject_service(Code::UnsupportedAction,
                          "standard get_csrf_token requires native transport");
  try {
    request.validate();
    if (!binding || request.installation != installation_)
      return reject_service(Code::InvalidRequest,
                            "standard get_csrf_token installation is invalid");
    binding->codec().validate(installation_);
    binding->codec().validate(request.installation);
    const auto parameters = standard_detail::encode_parameters(
        request.parameters, request.value_limits);
    auto serialized = serialize_mutation_parameters(
        parameters, request.control.limits.maximum_parameter_bytes);
    auto call = std::make_shared<standard_detail::StandardCallState>(
        request.control.limits, *deadline);
    const auto mode = request.control.execution;
    const auto values = request.value_limits;
    auto installation = installation_;
    auto action = bot::OperationTraits<standard::GetCsrfTokenRequest>::action();
    standard::GetCsrfTokenRequest validation_context = request;
    const auto echo = next_echo();
    auto captured = standard_detail::StandardTransportAccess::send(
        *facade, standard_detail::NativeAction::GetCsrfToken, mode,
        std::move(serialized), echo, call, identity, *origin);
    return finish_service(
        std::make_shared<PreparedService<standard::GetCsrfTokenRequest>>(
            PreparedService<standard::GetCsrfTokenRequest>{
                std::move(binding), std::move(validation_context),
                std::move(facade), std::move(identity), std::move(call),
                standard_detail::NativeAction::GetCsrfToken, mode, values, echo,
                std::move(installation), std::move(action),
                std::move(captured)}));
  } catch (const BotComponentRuntimeError &) {
    return reject_service(
        Code::UnsupportedAction,
        "standard get_csrf_token native transport is unavailable");
  } catch (...) {
    return reject_service(
        Code::InvalidRequest,
        "standard get_csrf_token preparation failed validation");
  }
}

auto OneBot11OperationsComponent::Endpoint::execute_standard_get_credentials(
    const standard::GetCredentialsRequest &request)
    -> boost::asio::awaitable<StructuredOperationCompletion> {
  // Synchronous preparation: no borrowed request or Endpoint crosses await.
  using Code = bot::BotOperationErrorCode;
  auto binding = binding_;
  auto identity = self_identity_;
  const auto origin = identity ? identity->capture_origin() : std::nullopt;
  const auto deadline =
      standard_detail::StandardRequestAccess::deadline(request);
  if (!deadline || std::chrono::steady_clock::now() >= *deadline)
    return reject_service(
        Code::Cancelled,
        "standard get_credentials dispatch deadline unavailable or elapsed");
  if (!origin)
    return reject_service(Code::TransportFailure,
                          "standard get_credentials authority is unavailable");
  auto facade =
      std::dynamic_pointer_cast<OneBot11TransportCapability>(transport_);
  if (!facade)
    return reject_service(Code::UnsupportedAction,
                          "standard get_credentials requires native transport");
  try {
    request.validate();
    if (!binding || request.installation != installation_)
      return reject_service(Code::InvalidRequest,
                            "standard get_credentials installation is invalid");
    binding->codec().validate(installation_);
    binding->codec().validate(request.installation);
    const auto parameters = standard_detail::encode_parameters(
        request.parameters, request.value_limits);
    auto serialized = serialize_mutation_parameters(
        parameters, request.control.limits.maximum_parameter_bytes);
    auto call = std::make_shared<standard_detail::StandardCallState>(
        request.control.limits, *deadline);
    const auto mode = request.control.execution;
    const auto values = request.value_limits;
    auto installation = installation_;
    auto action =
        bot::OperationTraits<standard::GetCredentialsRequest>::action();
    standard::GetCredentialsRequest validation_context = request;
    const auto echo = next_echo();
    auto captured = standard_detail::StandardTransportAccess::send(
        *facade, standard_detail::NativeAction::GetCredentials, mode,
        std::move(serialized), echo, call, identity, *origin);
    return finish_service(
        std::make_shared<PreparedService<standard::GetCredentialsRequest>>(
            PreparedService<standard::GetCredentialsRequest>{
                std::move(binding), std::move(validation_context),
                std::move(facade), std::move(identity), std::move(call),
                standard_detail::NativeAction::GetCredentials, mode, values,
                echo, std::move(installation), std::move(action),
                std::move(captured)}));
  } catch (const BotComponentRuntimeError &) {
    return reject_service(
        Code::UnsupportedAction,
        "standard get_credentials native transport is unavailable");
  } catch (...) {
    return reject_service(
        Code::InvalidRequest,
        "standard get_credentials preparation failed validation");
  }
}

auto OneBot11OperationsComponent::Endpoint::execute_standard_get_status(
    const standard::GetStatusRequest &request)
    -> boost::asio::awaitable<StructuredOperationCompletion> {
  // Synchronous preparation: no borrowed request or Endpoint crosses await.
  using Code = bot::BotOperationErrorCode;
  auto binding = binding_;
  auto identity = self_identity_;
  const auto origin = identity ? identity->capture_origin() : std::nullopt;
  const auto deadline =
      standard_detail::StandardRequestAccess::deadline(request);
  if (!deadline || std::chrono::steady_clock::now() >= *deadline)
    return reject_service(
        Code::Cancelled,
        "standard get_status dispatch deadline unavailable or elapsed");
  if (!origin)
    return reject_service(Code::TransportFailure,
                          "standard get_status authority is unavailable");
  auto facade =
      std::dynamic_pointer_cast<OneBot11TransportCapability>(transport_);
  if (!facade)
    return reject_service(Code::UnsupportedAction,
                          "standard get_status requires native transport");
  try {
    request.validate();
    if (!binding || request.installation != installation_)
      return reject_service(Code::InvalidRequest,
                            "standard get_status installation is invalid");
    binding->codec().validate(installation_);
    binding->codec().validate(request.installation);
    const auto parameters = standard_detail::encode_parameters(
        request.parameters, request.value_limits);
    auto serialized = serialize_mutation_parameters(
        parameters, request.control.limits.maximum_parameter_bytes);
    auto call = std::make_shared<standard_detail::StandardCallState>(
        request.control.limits, *deadline);
    const auto mode = request.control.execution;
    const auto values = request.value_limits;
    auto installation = installation_;
    auto action = bot::OperationTraits<standard::GetStatusRequest>::action();
    standard::GetStatusRequest validation_context = request;
    const auto echo = next_echo();
    auto captured = standard_detail::StandardTransportAccess::send(
        *facade, standard_detail::NativeAction::GetStatus, mode,
        std::move(serialized), echo, call, identity, *origin);
    return finish_service(
        std::make_shared<PreparedService<standard::GetStatusRequest>>(
            PreparedService<standard::GetStatusRequest>{
                std::move(binding), std::move(validation_context),
                std::move(facade), std::move(identity), std::move(call),
                standard_detail::NativeAction::GetStatus, mode, values, echo,
                std::move(installation), std::move(action),
                std::move(captured)}));
  } catch (const BotComponentRuntimeError &) {
    return reject_service(
        Code::UnsupportedAction,
        "standard get_status native transport is unavailable");
  } catch (...) {
    return reject_service(Code::InvalidRequest,
                          "standard get_status preparation failed validation");
  }
}

auto OneBot11OperationsComponent::Endpoint::execute_standard_get_version_info(
    const standard::GetVersionInfoRequest &request)
    -> boost::asio::awaitable<StructuredOperationCompletion> {
  // Synchronous preparation: no borrowed request or Endpoint crosses await.
  using Code = bot::BotOperationErrorCode;
  auto binding = binding_;
  auto identity = self_identity_;
  const auto origin = identity ? identity->capture_origin() : std::nullopt;
  const auto deadline =
      standard_detail::StandardRequestAccess::deadline(request);
  if (!deadline || std::chrono::steady_clock::now() >= *deadline)
    return reject_service(
        Code::Cancelled,
        "standard get_version_info dispatch deadline unavailable or elapsed");
  if (!origin)
    return reject_service(Code::TransportFailure,
                          "standard get_version_info authority is unavailable");
  auto facade =
      std::dynamic_pointer_cast<OneBot11TransportCapability>(transport_);
  if (!facade)
    return reject_service(
        Code::UnsupportedAction,
        "standard get_version_info requires native transport");
  try {
    request.validate();
    if (!binding || request.installation != installation_)
      return reject_service(
          Code::InvalidRequest,
          "standard get_version_info installation is invalid");
    binding->codec().validate(installation_);
    binding->codec().validate(request.installation);
    const auto parameters = standard_detail::encode_parameters(
        request.parameters, request.value_limits);
    auto serialized = serialize_mutation_parameters(
        parameters, request.control.limits.maximum_parameter_bytes);
    auto call = std::make_shared<standard_detail::StandardCallState>(
        request.control.limits, *deadline);
    const auto mode = request.control.execution;
    const auto values = request.value_limits;
    auto installation = installation_;
    auto action =
        bot::OperationTraits<standard::GetVersionInfoRequest>::action();
    standard::GetVersionInfoRequest validation_context = request;
    const auto echo = next_echo();
    auto captured = standard_detail::StandardTransportAccess::send(
        *facade, standard_detail::NativeAction::GetVersionInfo, mode,
        std::move(serialized), echo, call, identity, *origin);
    return finish_service(
        std::make_shared<PreparedService<standard::GetVersionInfoRequest>>(
            PreparedService<standard::GetVersionInfoRequest>{
                std::move(binding), std::move(validation_context),
                std::move(facade), std::move(identity), std::move(call),
                standard_detail::NativeAction::GetVersionInfo, mode, values,
                echo, std::move(installation), std::move(action),
                std::move(captured)}));
  } catch (const BotComponentRuntimeError &) {
    return reject_service(
        Code::UnsupportedAction,
        "standard get_version_info native transport is unavailable");
  } catch (...) {
    return reject_service(
        Code::InvalidRequest,
        "standard get_version_info preparation failed validation");
  }
}

auto OneBot11OperationsComponent::Endpoint::execute_standard_can_send_image(
    const standard::CanSendImageRequest &request)
    -> boost::asio::awaitable<StructuredOperationCompletion> {
  // Synchronous preparation: no borrowed request or Endpoint crosses await.
  using Code = bot::BotOperationErrorCode;
  auto binding = binding_;
  auto identity = self_identity_;
  const auto origin = identity ? identity->capture_origin() : std::nullopt;
  const auto deadline =
      standard_detail::StandardRequestAccess::deadline(request);
  if (!deadline || std::chrono::steady_clock::now() >= *deadline)
    return reject_service(
        Code::Cancelled,
        "standard can_send_image dispatch deadline unavailable or elapsed");
  if (!origin)
    return reject_service(Code::TransportFailure,
                          "standard can_send_image authority is unavailable");
  auto facade =
      std::dynamic_pointer_cast<OneBot11TransportCapability>(transport_);
  if (!facade)
    return reject_service(Code::UnsupportedAction,
                          "standard can_send_image requires native transport");
  try {
    request.validate();
    if (!binding || request.installation != installation_)
      return reject_service(Code::InvalidRequest,
                            "standard can_send_image installation is invalid");
    binding->codec().validate(installation_);
    binding->codec().validate(request.installation);
    const auto parameters = standard_detail::encode_parameters(
        request.parameters, request.value_limits);
    auto serialized = serialize_mutation_parameters(
        parameters, request.control.limits.maximum_parameter_bytes);
    auto call = std::make_shared<standard_detail::StandardCallState>(
        request.control.limits, *deadline);
    const auto mode = request.control.execution;
    const auto values = request.value_limits;
    auto installation = installation_;
    auto action = bot::OperationTraits<standard::CanSendImageRequest>::action();
    standard::CanSendImageRequest validation_context = request;
    const auto echo = next_echo();
    auto captured = standard_detail::StandardTransportAccess::send(
        *facade, standard_detail::NativeAction::CanSendImage, mode,
        std::move(serialized), echo, call, identity, *origin);
    return finish_service(
        std::make_shared<PreparedService<standard::CanSendImageRequest>>(
            PreparedService<standard::CanSendImageRequest>{
                std::move(binding), std::move(validation_context),
                std::move(facade), std::move(identity), std::move(call),
                standard_detail::NativeAction::CanSendImage, mode, values, echo,
                std::move(installation), std::move(action),
                std::move(captured)}));
  } catch (const BotComponentRuntimeError &) {
    return reject_service(
        Code::UnsupportedAction,
        "standard can_send_image native transport is unavailable");
  } catch (...) {
    return reject_service(
        Code::InvalidRequest,
        "standard can_send_image preparation failed validation");
  }
}

auto OneBot11OperationsComponent::Endpoint::execute_standard_can_send_record(
    const standard::CanSendRecordRequest &request)
    -> boost::asio::awaitable<StructuredOperationCompletion> {
  // Synchronous preparation: no borrowed request or Endpoint crosses await.
  using Code = bot::BotOperationErrorCode;
  auto binding = binding_;
  auto identity = self_identity_;
  const auto origin = identity ? identity->capture_origin() : std::nullopt;
  const auto deadline =
      standard_detail::StandardRequestAccess::deadline(request);
  if (!deadline || std::chrono::steady_clock::now() >= *deadline)
    return reject_service(
        Code::Cancelled,
        "standard can_send_record dispatch deadline unavailable or elapsed");
  if (!origin)
    return reject_service(Code::TransportFailure,
                          "standard can_send_record authority is unavailable");
  auto facade =
      std::dynamic_pointer_cast<OneBot11TransportCapability>(transport_);
  if (!facade)
    return reject_service(Code::UnsupportedAction,
                          "standard can_send_record requires native transport");
  try {
    request.validate();
    if (!binding || request.installation != installation_)
      return reject_service(Code::InvalidRequest,
                            "standard can_send_record installation is invalid");
    binding->codec().validate(installation_);
    binding->codec().validate(request.installation);
    const auto parameters = standard_detail::encode_parameters(
        request.parameters, request.value_limits);
    auto serialized = serialize_mutation_parameters(
        parameters, request.control.limits.maximum_parameter_bytes);
    auto call = std::make_shared<standard_detail::StandardCallState>(
        request.control.limits, *deadline);
    const auto mode = request.control.execution;
    const auto values = request.value_limits;
    auto installation = installation_;
    auto action =
        bot::OperationTraits<standard::CanSendRecordRequest>::action();
    standard::CanSendRecordRequest validation_context = request;
    const auto echo = next_echo();
    auto captured = standard_detail::StandardTransportAccess::send(
        *facade, standard_detail::NativeAction::CanSendRecord, mode,
        std::move(serialized), echo, call, identity, *origin);
    return finish_service(
        std::make_shared<PreparedService<standard::CanSendRecordRequest>>(
            PreparedService<standard::CanSendRecordRequest>{
                std::move(binding), std::move(validation_context),
                std::move(facade), std::move(identity), std::move(call),
                standard_detail::NativeAction::CanSendRecord, mode, values,
                echo, std::move(installation), std::move(action),
                std::move(captured)}));
  } catch (const BotComponentRuntimeError &) {
    return reject_service(
        Code::UnsupportedAction,
        "standard can_send_record native transport is unavailable");
  } catch (...) {
    return reject_service(
        Code::InvalidRequest,
        "standard can_send_record preparation failed validation");
  }
}

auto OneBot11OperationsComponent::Endpoint::execute_standard_set_restart(
    const standard::SetRestartRequest &request)
    -> boost::asio::awaitable<StructuredOperationCompletion> {
  // Synchronous preparation: no borrowed request or Endpoint crosses await.
  using Code = bot::BotOperationErrorCode;
  auto binding = binding_;
  auto identity = self_identity_;
  const auto origin = identity ? identity->capture_origin() : std::nullopt;
  const auto deadline =
      standard_detail::StandardRequestAccess::deadline(request);
  if (!deadline || std::chrono::steady_clock::now() >= *deadline)
    return reject_service(
        Code::Cancelled,
        "standard set_restart dispatch deadline unavailable or elapsed");
  if (!origin)
    return reject_service(Code::TransportFailure,
                          "standard set_restart authority is unavailable");
  auto facade =
      std::dynamic_pointer_cast<OneBot11TransportCapability>(transport_);
  if (!facade)
    return reject_service(Code::UnsupportedAction,
                          "standard set_restart requires native transport");
  try {
    request.validate();
    if (!binding || request.installation != installation_)
      return reject_service(Code::InvalidRequest,
                            "standard set_restart installation is invalid");
    binding->codec().validate(installation_);
    binding->codec().validate(request.installation);
    const auto parameters = standard_detail::encode_parameters(
        request.parameters, request.value_limits);
    auto serialized = serialize_mutation_parameters(
        parameters, request.control.limits.maximum_parameter_bytes);
    auto call = std::make_shared<standard_detail::StandardCallState>(
        request.control.limits, *deadline);
    const auto mode = request.control.execution;
    const auto values = request.value_limits;
    auto installation = installation_;
    auto action = bot::OperationTraits<standard::SetRestartRequest>::action();
    standard::SetRestartRequest validation_context = request;
    const auto echo = next_echo();
    auto captured = standard_detail::StandardTransportAccess::send(
        *facade, standard_detail::NativeAction::SetRestart, mode,
        std::move(serialized), echo, call, identity, *origin);
    return finish_service(
        std::make_shared<PreparedService<standard::SetRestartRequest>>(
            PreparedService<standard::SetRestartRequest>{
                std::move(binding), std::move(validation_context),
                std::move(facade), std::move(identity), std::move(call),
                standard_detail::NativeAction::SetRestart, mode, values, echo,
                std::move(installation), std::move(action),
                std::move(captured)}));
  } catch (const BotComponentRuntimeError &) {
    return reject_service(
        Code::UnsupportedAction,
        "standard set_restart native transport is unavailable");
  } catch (...) {
    return reject_service(Code::InvalidRequest,
                          "standard set_restart preparation failed validation");
  }
}

auto OneBot11OperationsComponent::Endpoint::execute_standard_clean_cache(
    const standard::CleanCacheRequest &request)
    -> boost::asio::awaitable<StructuredOperationCompletion> {
  // Synchronous preparation: no borrowed request or Endpoint crosses await.
  using Code = bot::BotOperationErrorCode;
  auto binding = binding_;
  auto identity = self_identity_;
  const auto origin = identity ? identity->capture_origin() : std::nullopt;
  const auto deadline =
      standard_detail::StandardRequestAccess::deadline(request);
  if (!deadline || std::chrono::steady_clock::now() >= *deadline)
    return reject_service(
        Code::Cancelled,
        "standard clean_cache dispatch deadline unavailable or elapsed");
  if (!origin)
    return reject_service(Code::TransportFailure,
                          "standard clean_cache authority is unavailable");
  auto facade =
      std::dynamic_pointer_cast<OneBot11TransportCapability>(transport_);
  if (!facade)
    return reject_service(Code::UnsupportedAction,
                          "standard clean_cache requires native transport");
  try {
    request.validate();
    if (!binding || request.installation != installation_)
      return reject_service(Code::InvalidRequest,
                            "standard clean_cache installation is invalid");
    binding->codec().validate(installation_);
    binding->codec().validate(request.installation);
    const auto parameters = standard_detail::encode_parameters(
        request.parameters, request.value_limits);
    auto serialized = serialize_mutation_parameters(
        parameters, request.control.limits.maximum_parameter_bytes);
    auto call = std::make_shared<standard_detail::StandardCallState>(
        request.control.limits, *deadline);
    const auto mode = request.control.execution;
    const auto values = request.value_limits;
    auto installation = installation_;
    auto action = bot::OperationTraits<standard::CleanCacheRequest>::action();
    standard::CleanCacheRequest validation_context = request;
    const auto echo = next_echo();
    auto captured = standard_detail::StandardTransportAccess::send(
        *facade, standard_detail::NativeAction::CleanCache, mode,
        std::move(serialized), echo, call, identity, *origin);
    return finish_service(
        std::make_shared<PreparedService<standard::CleanCacheRequest>>(
            PreparedService<standard::CleanCacheRequest>{
                std::move(binding), std::move(validation_context),
                std::move(facade), std::move(identity), std::move(call),
                standard_detail::NativeAction::CleanCache, mode, values, echo,
                std::move(installation), std::move(action),
                std::move(captured)}));
  } catch (const BotComponentRuntimeError &) {
    return reject_service(
        Code::UnsupportedAction,
        "standard clean_cache native transport is unavailable");
  } catch (...) {
    return reject_service(Code::InvalidRequest,
                          "standard clean_cache preparation failed validation");
  }
}

auto OneBot11OperationsComponent::Endpoint::execute_standard_get_image(
    const standard::GetImageRequest &request)
    -> boost::asio::awaitable<StructuredOperationCompletion> {
  // Synchronous preparation: no borrowed request or Endpoint crosses await.
  using Code = bot::BotOperationErrorCode;
  auto binding = binding_;
  auto identity = self_identity_;
  const auto origin = identity ? identity->capture_origin() : std::nullopt;
  const auto deadline =
      standard_detail::StandardRequestAccess::deadline(request);
  if (!deadline || std::chrono::steady_clock::now() >= *deadline)
    return reject_media(
        Code::Cancelled,
        "standard get_image dispatch deadline unavailable or elapsed");
  if (!origin)
    return reject_media(Code::TransportFailure,
                        "standard get_image authority is unavailable");
  auto facade =
      std::dynamic_pointer_cast<OneBot11TransportCapability>(transport_);
  if (!facade)
    return reject_media(Code::UnsupportedAction,
                        "standard get_image requires native transport");
  try {
    request.validate();
    if (!binding || request.parameters.image.installation() != installation_)
      return reject_media(Code::InvalidRequest,
                          "standard get_image installation is invalid");
    binding->codec().validate(installation_);
    binding->codec().validate(request.parameters.image.installation());
    const auto parameters = standard_detail::encode_get_image(
        request.parameters, installation_, binding->codec(),
        request.value_limits);
    auto serialized = serialize_mutation_parameters(
        parameters, request.control.limits.maximum_parameter_bytes);
    auto call = std::make_shared<standard_detail::StandardCallState>(
        request.control.limits, *deadline);
    const auto mode = request.control.execution;
    const auto values = request.value_limits;
    auto installation = installation_;
    auto action = bot::OperationTraits<standard::GetImageRequest>::action();
    const auto validation_context =
        standard_detail::media_result_context(request);
    const auto echo = next_echo();
    auto captured = standard_detail::StandardTransportAccess::send(
        *facade, standard_detail::NativeAction::GetImage, mode,
        std::move(serialized), echo, call, identity, *origin);
    return finish_media(
        std::make_shared<PreparedMedia<standard::GetImageRequest>>(
            PreparedMedia<standard::GetImageRequest>{
                std::move(binding), std::move(validation_context),
                std::move(facade), std::move(identity), std::move(call),
                standard_detail::NativeAction::GetImage, mode, values, echo,
                std::move(installation), std::move(action),
                std::move(captured)}));
  } catch (const BotComponentRuntimeError &) {
    return reject_media(Code::UnsupportedAction,
                        "standard get_image native transport is unavailable");
  } catch (...) {
    return reject_media(Code::InvalidRequest,
                        "standard get_image preparation failed validation");
  }
}

auto OneBot11OperationsComponent::Endpoint::execute_standard_get_record(
    const standard::GetRecordRequest &request)
    -> boost::asio::awaitable<StructuredOperationCompletion> {
  // Synchronous preparation: no borrowed request or Endpoint crosses await.
  using Code = bot::BotOperationErrorCode;
  auto binding = binding_;
  auto identity = self_identity_;
  const auto origin = identity ? identity->capture_origin() : std::nullopt;
  const auto deadline =
      standard_detail::StandardRequestAccess::deadline(request);
  if (!deadline || std::chrono::steady_clock::now() >= *deadline)
    return reject_media(
        Code::Cancelled,
        "standard get_record dispatch deadline unavailable or elapsed");
  if (!origin)
    return reject_media(Code::TransportFailure,
                        "standard get_record authority is unavailable");
  auto facade =
      std::dynamic_pointer_cast<OneBot11TransportCapability>(transport_);
  if (!facade)
    return reject_media(Code::UnsupportedAction,
                        "standard get_record requires native transport");
  try {
    request.validate();
    if (!binding || request.parameters.record.installation() != installation_)
      return reject_media(Code::InvalidRequest,
                          "standard get_record installation is invalid");
    binding->codec().validate(installation_);
    binding->codec().validate(request.parameters.record.installation());
    const auto parameters = standard_detail::encode_get_record(
        request.parameters, installation_, binding->codec(),
        request.value_limits);
    auto serialized = serialize_mutation_parameters(
        parameters, request.control.limits.maximum_parameter_bytes);
    auto call = std::make_shared<standard_detail::StandardCallState>(
        request.control.limits, *deadline);
    const auto mode = request.control.execution;
    const auto values = request.value_limits;
    auto installation = installation_;
    auto action = bot::OperationTraits<standard::GetRecordRequest>::action();
    const auto validation_context =
        standard_detail::media_result_context(request);
    const auto echo = next_echo();
    auto captured = standard_detail::StandardTransportAccess::send(
        *facade, standard_detail::NativeAction::GetRecord, mode,
        std::move(serialized), echo, call, identity, *origin);
    return finish_media(
        std::make_shared<PreparedMedia<standard::GetRecordRequest>>(
            PreparedMedia<standard::GetRecordRequest>{
                std::move(binding), std::move(validation_context),
                std::move(facade), std::move(identity), std::move(call),
                standard_detail::NativeAction::GetRecord, mode, values, echo,
                std::move(installation), std::move(action),
                std::move(captured)}));
  } catch (const BotComponentRuntimeError &) {
    return reject_media(Code::UnsupportedAction,
                        "standard get_record native transport is unavailable");
  } catch (...) {
    return reject_media(Code::InvalidRequest,
                        "standard get_record preparation failed validation");
  }
}

OneBot11OperationsComponent::OneBot11OperationsComponent(
    std::string installation_id, boost::asio::any_io_executor executor,
    std::chrono::milliseconds self_refresh_interval,
    std::shared_ptr<const PlatformDomainBinding> binding,
    std::chrono::milliseconds action_timeout)
    : endpoint_(std::make_shared<Endpoint>(std::move(installation_id), binding,
                                           action_timeout)),
      operations_(std::make_shared<OperationRegistry>(endpoint_->installation(),
                                                      std::move(binding))),
      self_identity_(std::make_shared<SelfIdentityCache>(
          std::move(executor), endpoint_->installation(),
          self_refresh_interval)) {}

auto OneBot11OperationsComponent::descriptor() const -> ComponentDescriptor {
  std::vector<CapabilityId> dependencies;
  for (const auto &id : obcx::onebot11::bot::operation_dependencies()) {
    dependencies.emplace_back(id);
  }
  return {.id = ComponentId{"onebot11.operations"},
          .provides = {CapabilityId{"bot.operations"}},
          .required = std::move(dependencies)};
}

void OneBot11OperationsComponent::install_capabilities(
    CapabilityRegistry &registry) {
  registry.install<OperationRegistry>(ComponentId{"onebot11.operations"},
                                      CapabilityId{"bot.operations"},
                                      operations_);
}

void OneBot11OperationsComponent::prepare(const CapabilityRegistry &registry) {
  auto transport = registry.get<OneBot11Transport>(CapabilityId{
      std::string{obcx::onebot11::bot::capability_ids::transport}});
  endpoint_->configure(
      registry.get<adapter::onebot11::ProtocolAdapter>(CapabilityId{
          std::string{obcx::onebot11::bot::capability_ids::protocol}}),
      transport, self_identity_, operations_->reply_evidence());
  self_identity_->configure(
      [weak = std::weak_ptr(endpoint_)](SelfIdentityOrigin origin)
          -> boost::asio::awaitable<SelfIdentityCache::Result> {
        auto endpoint = weak.lock();
        if (!endpoint)
          throw std::runtime_error("self identity endpoint retired");
        co_return co_await endpoint->fetch_self(std::move(origin));
      });
  operations_->set_self_identity_cache(self_identity_);
  const CapabilityId events_id{"bot.events"};
  if (std::dynamic_pointer_cast<OneBot11TransportCapability>(transport) &&
      registry.contains(events_id)) {
    endpoint_->configure_received_media(
        *operations_, registry.get<BotEventCapability>(events_id));
  }
  const CapabilityId standard_events_id{
      std::string{standard::standard_event_capability_v1}};
  if (auto native =
          std::dynamic_pointer_cast<OneBot11TransportCapability>(transport);
      native && registry.contains(standard_events_id)) {
    // All capabilities are installed before any component is prepared. Keep
    // operations-only/custom-transport recipes valid; do not invent an ingress.
    auto events =
        registry.get<standard::StandardEventCapabilityV1>(standard_events_id);
    // Explicit existing domain ceilings, not new configuration defaults.
    // Tighter transport read bounds remain independently enforced.
    const standard::ValueLimits event_limits{
        .maximum_bytes = bot::detail::domain_maximum_bytes,
        .maximum_nodes = bot::detail::domain_maximum_items,
        .maximum_depth = 32,
        .maximum_text_bytes = bot::detail::domain_maximum_bytes};
    standard_detail::StandardEventAccess::configure(
        *events, endpoint_->installation(), self_identity_, event_limits);
    native->set_standard_events(std::move(events));
  }
  obcx::onebot11::bot::for_each_operation([&](const auto &definition) {
    using Request =
        typename std::remove_cvref_t<decltype(definition)>::request_type;
    if constexpr (std::same_as<Request, bot::SendMessageRequest>) {
      operations_->install(definition.bind_with_author_evidence(
          [endpoint = endpoint_](const Request &request,
                                 ConfirmedAuthorEvidenceCollector &authors)
              -> boost::asio::awaitable<
                  bot::BotOperationResult<bot::SendOutcome>> {
            co_return co_await endpoint->execute(request, authors);
          }));
    } else if constexpr (
        std::same_as<Request,
                     onebot11::bot::SendOneBotGroupForwardMessageRequest>) {
      // Node authors/resource IDs do not prove the outer forward's author.
      // Keep this existing extension's recipe and physical effects unchanged.
      operations_->install(definition.bind(
          [endpoint = endpoint_](const Request &request)
              -> boost::asio::awaitable<bot::BotOperationResult<
                  typename bot::OperationTraits<Request>::result_type>> {
            co_return co_await endpoint->execute(request);
          }));
    } else {
      operations_->install(definition.bind(bind_operation_handler<Request>(
          endpoint_, obcx::onebot11::bot::redact_diagnostic)));
    }
  });
  if (endpoint_->supports_standard_send_like()) {
    operations_->install(obcx::onebot11::bot::standard::detail::bind_send_like(
        [endpoint = endpoint_](
            const obcx::onebot11::bot::standard::SendLikeRequest &request)
            -> boost::asio::awaitable<StructuredOperationCompletion> {
          return endpoint->execute_standard_send_like(request);
        }));
  }
  if (endpoint_->supports_standard_mutations()) {
    operations_->install(obcx::onebot11::bot::standard::detail::bind_delete_msg(
        [endpoint = endpoint_](
            const obcx::onebot11::bot::standard::DeleteMsgRequest &request)
            -> boost::asio::awaitable<StructuredOperationCompletion> {
          return endpoint->execute_standard_delete_msg(request);
        }));
    operations_->install(
        obcx::onebot11::bot::standard::detail::bind_set_group_kick(
            [endpoint = endpoint_](
                const obcx::onebot11::bot::standard::SetGroupKickRequest
                    &request)
                -> boost::asio::awaitable<StructuredOperationCompletion> {
              return endpoint->execute_standard_set_group_kick(request);
            }));
    operations_->install(
        obcx::onebot11::bot::standard::detail::bind_set_group_ban(
            [endpoint = endpoint_](
                const obcx::onebot11::bot::standard::SetGroupBanRequest
                    &request)
                -> boost::asio::awaitable<StructuredOperationCompletion> {
              return endpoint->execute_standard_set_group_ban(request);
            }));
    operations_->install(
        obcx::onebot11::bot::standard::detail::bind_set_group_whole_ban(
            [endpoint = endpoint_](
                const obcx::onebot11::bot::standard::SetGroupWholeBanRequest
                    &request)
                -> boost::asio::awaitable<StructuredOperationCompletion> {
              return endpoint->execute_standard_set_group_whole_ban(request);
            }));
    operations_->install(
        obcx::onebot11::bot::standard::detail::bind_set_group_admin(
            [endpoint = endpoint_](
                const obcx::onebot11::bot::standard::SetGroupAdminRequest
                    &request)
                -> boost::asio::awaitable<StructuredOperationCompletion> {
              return endpoint->execute_standard_set_group_admin(request);
            }));
    operations_->install(
        obcx::onebot11::bot::standard::detail::bind_set_group_anonymous(
            [endpoint = endpoint_](
                const obcx::onebot11::bot::standard::SetGroupAnonymousRequest
                    &request)
                -> boost::asio::awaitable<StructuredOperationCompletion> {
              return endpoint->execute_standard_set_group_anonymous(request);
            }));
    operations_->install(
        obcx::onebot11::bot::standard::detail::bind_set_group_card(
            [endpoint = endpoint_](
                const obcx::onebot11::bot::standard::SetGroupCardRequest
                    &request)
                -> boost::asio::awaitable<StructuredOperationCompletion> {
              return endpoint->execute_standard_set_group_card(request);
            }));
    operations_->install(
        obcx::onebot11::bot::standard::detail::bind_set_group_name(
            [endpoint = endpoint_](
                const obcx::onebot11::bot::standard::SetGroupNameRequest
                    &request)
                -> boost::asio::awaitable<StructuredOperationCompletion> {
              return endpoint->execute_standard_set_group_name(request);
            }));
    operations_->install(
        obcx::onebot11::bot::standard::detail::bind_set_group_leave(
            [endpoint = endpoint_](
                const obcx::onebot11::bot::standard::SetGroupLeaveRequest
                    &request)
                -> boost::asio::awaitable<StructuredOperationCompletion> {
              return endpoint->execute_standard_set_group_leave(request);
            }));
    operations_->install(
        obcx::onebot11::bot::standard::detail::bind_set_group_special_title(
            [endpoint = endpoint_](
                const obcx::onebot11::bot::standard::SetGroupSpecialTitleRequest
                    &request)
                -> boost::asio::awaitable<StructuredOperationCompletion> {
              return endpoint->execute_standard_set_group_special_title(
                  request);
            }));
  }
  if (endpoint_->supports_standard_queries()) {
    operations_->install(
        obcx::onebot11::bot::standard::detail::bind_get_stranger_info(
            [endpoint = endpoint_](
                const obcx::onebot11::bot::standard::GetStrangerInfoRequest
                    &request)
                -> boost::asio::awaitable<StructuredOperationCompletion> {
              return endpoint->execute_standard_get_stranger_info(request);
            }));
    operations_->install(
        obcx::onebot11::bot::standard::detail::bind_get_friend_list(
            [endpoint = endpoint_](
                const obcx::onebot11::bot::standard::GetFriendListRequest
                    &request)
                -> boost::asio::awaitable<StructuredOperationCompletion> {
              return endpoint->execute_standard_get_friend_list(request);
            }));
    operations_->install(
        obcx::onebot11::bot::standard::detail::bind_get_group_info(
            [endpoint = endpoint_](
                const obcx::onebot11::bot::standard::GetGroupInfoRequest
                    &request)
                -> boost::asio::awaitable<StructuredOperationCompletion> {
              return endpoint->execute_standard_get_group_info(request);
            }));
    operations_->install(
        obcx::onebot11::bot::standard::detail::bind_get_group_list(
            [endpoint = endpoint_](
                const obcx::onebot11::bot::standard::GetGroupListRequest
                    &request)
                -> boost::asio::awaitable<StructuredOperationCompletion> {
              return endpoint->execute_standard_get_group_list(request);
            }));
    operations_->install(
        obcx::onebot11::bot::standard::detail::bind_get_group_member_info(
            [endpoint = endpoint_](
                const obcx::onebot11::bot::standard::GetGroupMemberInfoRequest
                    &request)
                -> boost::asio::awaitable<StructuredOperationCompletion> {
              return endpoint->execute_standard_get_group_member_info(request);
            }));
    operations_->install(
        obcx::onebot11::bot::standard::detail::bind_get_group_member_list(
            [endpoint = endpoint_](
                const obcx::onebot11::bot::standard::GetGroupMemberListRequest
                    &request)
                -> boost::asio::awaitable<StructuredOperationCompletion> {
              return endpoint->execute_standard_get_group_member_list(request);
            }));
    operations_->install(
        obcx::onebot11::bot::standard::detail::bind_get_group_honor_info(
            [endpoint = endpoint_](
                const obcx::onebot11::bot::standard::GetGroupHonorInfoRequest
                    &request)
                -> boost::asio::awaitable<StructuredOperationCompletion> {
              return endpoint->execute_standard_get_group_honor_info(request);
            }));
  }
  if (endpoint_->supports_standard_services()) {
    operations_->install(
        obcx::onebot11::bot::standard::detail::bind_get_cookies(
            [endpoint = endpoint_](
                const obcx::onebot11::bot::standard::GetCookiesRequest &request)
                -> boost::asio::awaitable<StructuredOperationCompletion> {
              return endpoint->execute_standard_get_cookies(request);
            }));
    operations_->install(
        obcx::onebot11::bot::standard::detail::bind_get_csrf_token(
            [endpoint = endpoint_](
                const obcx::onebot11::bot::standard::GetCsrfTokenRequest
                    &request)
                -> boost::asio::awaitable<StructuredOperationCompletion> {
              return endpoint->execute_standard_get_csrf_token(request);
            }));
    operations_->install(
        obcx::onebot11::bot::standard::detail::bind_get_credentials(
            [endpoint = endpoint_](
                const obcx::onebot11::bot::standard::GetCredentialsRequest
                    &request)
                -> boost::asio::awaitable<StructuredOperationCompletion> {
              return endpoint->execute_standard_get_credentials(request);
            }));
    operations_->install(obcx::onebot11::bot::standard::detail::bind_get_status(
        [endpoint = endpoint_](
            const obcx::onebot11::bot::standard::GetStatusRequest &request)
            -> boost::asio::awaitable<StructuredOperationCompletion> {
          return endpoint->execute_standard_get_status(request);
        }));
    operations_->install(
        obcx::onebot11::bot::standard::detail::bind_get_version_info(
            [endpoint = endpoint_](
                const obcx::onebot11::bot::standard::GetVersionInfoRequest
                    &request)
                -> boost::asio::awaitable<StructuredOperationCompletion> {
              return endpoint->execute_standard_get_version_info(request);
            }));
    operations_->install(
        obcx::onebot11::bot::standard::detail::bind_can_send_image(
            [endpoint = endpoint_](
                const obcx::onebot11::bot::standard::CanSendImageRequest
                    &request)
                -> boost::asio::awaitable<StructuredOperationCompletion> {
              return endpoint->execute_standard_can_send_image(request);
            }));
    operations_->install(
        obcx::onebot11::bot::standard::detail::bind_can_send_record(
            [endpoint = endpoint_](
                const obcx::onebot11::bot::standard::CanSendRecordRequest
                    &request)
                -> boost::asio::awaitable<StructuredOperationCompletion> {
              return endpoint->execute_standard_can_send_record(request);
            }));
    operations_->install(
        obcx::onebot11::bot::standard::detail::bind_set_restart(
            [endpoint = endpoint_](
                const obcx::onebot11::bot::standard::SetRestartRequest &request)
                -> boost::asio::awaitable<StructuredOperationCompletion> {
              return endpoint->execute_standard_set_restart(request);
            }));
    operations_->install(
        obcx::onebot11::bot::standard::detail::bind_clean_cache(
            [endpoint = endpoint_](
                const obcx::onebot11::bot::standard::CleanCacheRequest &request)
                -> boost::asio::awaitable<StructuredOperationCompletion> {
              return endpoint->execute_standard_clean_cache(request);
            }));
  }
  if (endpoint_->supports_standard_send_messages()) {
    operations_->install(standard_detail::bind_send_private_msg(
        [endpoint = endpoint_](const standard::SendPrivateMsgRequest &request)
            -> boost::asio::awaitable<StructuredOperationCompletion> {
          return endpoint->execute_standard_send_private_msg(request);
        }));
    operations_->install(standard_detail::bind_send_group_msg(
        [endpoint = endpoint_](const standard::SendGroupMsgRequest &request)
            -> boost::asio::awaitable<StructuredOperationCompletion> {
          return endpoint->execute_standard_send_group_msg(request);
        }));
    operations_->install(standard_detail::bind_send_msg(
        [endpoint = endpoint_](const standard::SendMsgRequest &request)
            -> boost::asio::awaitable<StructuredOperationCompletion> {
          return endpoint->execute_standard_send_msg(request);
        }));
  }
  if (endpoint_->supports_standard_queries()) {
    operations_->install(standard_detail::bind_get_msg(
        [endpoint = endpoint_](const standard::GetMsgRequest &request)
            -> boost::asio::awaitable<StructuredOperationCompletion> {
          return endpoint->execute_standard_get_msg(request);
        }));
    operations_->install(standard_detail::bind_get_forward_msg(
        [endpoint = endpoint_](const standard::GetForwardMsgRequest &request)
            -> boost::asio::awaitable<StructuredOperationCompletion> {
          return endpoint->execute_standard_get_forward_msg(request);
        }));
    operations_->install(standard_detail::bind_get_image(
        [endpoint = endpoint_](const standard::GetImageRequest &request)
            -> boost::asio::awaitable<StructuredOperationCompletion> {
          return endpoint->execute_standard_get_image(request);
        }));
    operations_->install(standard_detail::bind_get_record(
        [endpoint = endpoint_](const standard::GetRecordRequest &request)
            -> boost::asio::awaitable<StructuredOperationCompletion> {
          return endpoint->execute_standard_get_record(request);
        }));
  }
  if (endpoint_->supports_standard_login()) {
    operations_->install(standard_detail::bind_get_login_info(
        [endpoint = endpoint_](const standard::GetLoginInfoRequest &request)
            -> boost::asio::awaitable<StructuredOperationCompletion> {
          return endpoint->execute_standard_get_login_info(request);
        }));
  }
  if (endpoint_->supports_standard_mutations()) {
    operations_->install(standard_detail::bind_set_group_anonymous_ban(
        [endpoint =
             endpoint_](const standard::SetGroupAnonymousBanRequest &request)
            -> boost::asio::awaitable<StructuredOperationCompletion> {
          return endpoint->execute_standard_set_group_anonymous_ban(request);
        }));
    operations_->install(standard_detail::bind_set_friend_add_request(
        [endpoint =
             endpoint_](const standard::SetFriendAddRequestRequest &request)
            -> boost::asio::awaitable<StructuredOperationCompletion> {
          return endpoint->execute_standard_set_friend_add_request(request);
        }));
    operations_->install(standard_detail::bind_set_group_add_request(
        [endpoint =
             endpoint_](const standard::SetGroupAddRequestRequest &request)
            -> boost::asio::awaitable<StructuredOperationCompletion> {
          return endpoint->execute_standard_set_group_add_request(request);
        }));
    operations_->install(standard_detail::bind_handle_quick_operation(
        [endpoint =
             endpoint_](const standard::HandleQuickOperationRequest &request)
            -> boost::asio::awaitable<StructuredOperationCompletion> {
          return endpoint->execute_standard_handle_quick_operation(request);
        }));
  }
  std::vector<std::string> available;
  for (const auto &id : obcx::onebot11::bot::operation_dependencies()) {
    if (registry.contains(CapabilityId{id})) {
      available.push_back(id);
    }
  }
  operations_->seal(endpoint_->installation(), available);
}

void OneBot11OperationsComponent::start() {}
void OneBot11OperationsComponent::stop() { operations_->close(); }

} // namespace obcx::core
