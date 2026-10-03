#ifndef OBCX_INCLUDE_CORE_BOT_TYPED_OPERATION_HPP_
#define OBCX_INCLUDE_CORE_BOT_TYPED_OPERATION_HPP_

#include "core/bot/gateway_codec.hpp"
#include "core/bot/operation_gateway.hpp"
#include "core/bot/operation_traits.hpp"

#include <concepts>
#include <optional>
#include <utility>

namespace obcx::bot {

template <typename Request>
concept TypedOperation =
    requires(const Request &request,
             typename OperationTraits<Request>::result_type result) {
      { request.validate() } -> std::same_as<void>;
      { OperationTraits<Request>::action() } -> std::same_as<const ActionId &>;
      {
        OperationTraits<Request>::installation(request)
      } -> std::same_as<const BotInstallationRef &>;
      {
        OperationTraits<Request>::validate_result(request, result)
      } -> std::same_as<void>;
    };

template <TypedOperation Request>
[[nodiscard]] auto invoke(BotOperationGateway &gateway, Request request)
    -> boost::asio::awaitable<
        BotOperationResult<typename OperationTraits<Request>::result_type>> {
  using Traits = OperationTraits<Request>;
  using Result = typename Traits::result_type;
  using Reply = BotOperationResult<Result>;

  std::optional<OperationEnvelope> envelope;
  try {
    request.validate();
    envelope.emplace(
        OperationEnvelope{.installation = Traits::installation(request),
                          .action = Traits::action(),
                          .payload = GatewayCodec<Request>::encode(request)});
    envelope->validate();
  } catch (...) {
    co_return Reply::failure(
        {.code = BotOperationErrorCode::InvalidRequest,
         .message = "SDK request could not be validated or encoded",
         .retryable = false,
         .submission_safety = SubmissionSafety::DefinitelyNotSubmitted});
  }

  // No detached work or new executor. The caller awaits this coroutine through
  // its existing ActorContext/await_asio generation lifetime boundary.
  try {
    auto reply = co_await gateway.invoke(std::move(*envelope));
    reply.validate();
    if (!reply.ok()) {
      co_return Reply::failure(std::move(*reply.error));
    }
    auto result = GatewayCodec<Result>::decode(std::move(*reply.value));
    Traits::validate_result(request, result);
    co_return Reply::success(std::move(result));
  } catch (...) {
    // Once invocation begins, an exception or invalid success cannot establish
    // non-submission of a side effect. Never leak raw JSON/exception payloads.
    co_return Reply::failure(
        {.code = Traits::side_effecting
                     ? BotOperationErrorCode::OutcomeUnknown
                     : BotOperationErrorCode::MalformedResponse,
         .message = "SDK operation did not produce a valid typed result",
         .retryable = false,
         .submission_safety = Traits::side_effecting
                                  ? SubmissionSafety::PossiblySubmitted
                                  : SubmissionSafety::DefinitelyNotSubmitted});
  }
}

} // namespace obcx::bot

#endif // OBCX_INCLUDE_CORE_BOT_TYPED_OPERATION_HPP_
