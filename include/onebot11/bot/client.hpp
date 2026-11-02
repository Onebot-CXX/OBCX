#pragma once

#include "core/bot/typed_operation.hpp"
#include "onebot11/bot/forward_observation.hpp"
#include "onebot11/bot/operations.hpp"

namespace obcx::onebot11::bot {

template <typename Request>
concept OwnedRequest =
    std::same_as<Request, GetOneBotUserInfoRequest> ||
    std::same_as<Request, GetOneBotGroupMemberRequest> ||
    std::same_as<Request, GetOneBotForwardMessageRequest> ||
    std::same_as<Request, ObserveOneBotForwardMessageRequest> ||
    std::same_as<Request, ReadOneBotFileRequest> ||
    std::same_as<Request, ResolveOneBotGroupFileRequest> ||
    std::same_as<Request, ResolveOneBotPrivateFileRequest> ||
    std::same_as<Request, PokeOneBotGroupRequest> ||
    std::same_as<Request, SetOneBotMessageEmojiLikeRequest>;

class Client {
public:
  explicit Client(obcx::bot::BotOperationGateway &gateway)
      : gateway_(gateway) {}

  template <OwnedRequest Request> auto execute(Request request) {
    return obcx::bot::invoke(gateway_, std::move(request));
  }

  auto execute(SendOneBotGroupForwardMessageRequest request)
      -> boost::asio::awaitable<OneBotGroupForwardMessageResult> {
    return execute_forward(gateway_, std::move(request), std::nullopt);
  }
  auto execute(SendOneBotGroupForwardMessageRequest request,
               obcx::bot::OperationCorrelation correlation)
      -> boost::asio::awaitable<OneBotGroupForwardMessageResult> {
    return execute_forward(gateway_, std::move(request),
                           std::move(correlation));
  }

private:
  // The gateway outlives execution; the lightweight facade need not.
  static auto execute_forward(
      obcx::bot::BotOperationGateway &gateway,
      SendOneBotGroupForwardMessageRequest request,
      std::optional<obcx::bot::OperationCorrelation> correlation)
      -> boost::asio::awaitable<OneBotGroupForwardMessageResult> {
    namespace d = obcx::bot;
    std::optional<d::OperationEnvelope> invocation;
    std::optional<d::SendOutcomeEnvelope> allowance;
    try {
      if (correlation)
        correlation->validate();
      request.validate();
      if (!d::OperationTraits<SendOneBotGroupForwardMessageRequest>::
              supports_surface(request.target.installation.surface))
        throw std::invalid_argument("unsupported forward surface");
      invocation.emplace(d::OperationEnvelope{
          request.target.installation, request.action,
          d::GatewayCodec<SendOneBotGroupForwardMessageRequest>::encode(
              request),
          correlation});
      invocation->validate();
      // Prepare the same full-reply recovery boundary BEFORE dispatch. Absent
      // correlation delegates generation to the process's unique attempt ID;
      // a supplied ID is preserved, never regenerated from outputs.
      allowance.emplace(forward_outcome::envelope());
    } catch (...) {
      co_return forward_outcome::minimal(d::send_failure(
          {.code = d::BotOperationErrorCode::InvalidRequest,
           .message = "QQ forward request failed validation or encoding",
           .retryable = false,
           .submission_safety = d::SubmissionSafety::DefinitelyNotSubmitted},
          {}, false));
    }
    try {
      auto reply = co_await gateway.invoke(std::move(*invocation));
      bool malformed = false;
      try {
        reply.validate();
      } catch (...) {
        malformed = true;
      }
      if (reply.value)
        co_return forward_outcome::decode(request, *reply.value, *allowance,
                                          malformed || reply.error.has_value());
      if (!malformed && reply.error) {
        const bool uncertain = reply.error->submission_safety ==
                               d::SubmissionSafety::PossiblySubmitted;
        co_return forward_outcome::minimal(
            d::send_failure(std::move(*reply.error), {}, uncertain));
      }
    } catch (...) {
    }
    co_return forward_outcome::minimal(
        d::send_failure(d::detail::unknown_send_error(), {}, true));
  }
  obcx::bot::BotOperationGateway &gateway_;
};

} // namespace obcx::onebot11::bot
