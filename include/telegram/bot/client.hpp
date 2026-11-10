#pragma once

#include "core/bot/typed_operation.hpp"
#include "telegram/bot/bot_operations.hpp"

namespace obcx::telegram::bot {

template <typename Request>
concept OwnedRequest = std::same_as<Request, EditTextRequest> ||
                       std::same_as<Request, EditFormattedTextRequest> ||
                       std::same_as<Request, CreateTopicRequest>;

class Client {
public:
  explicit Client(obcx::bot::BotOperationGateway &gateway)
      : gateway_(gateway) {}

  template <OwnedRequest Request> auto execute(Request request) {
    return obcx::bot::invoke(gateway_, std::move(request));
  }

  auto send_sticker(SendStickerRequest request)
      -> boost::asio::awaitable<obcx::bot::SendOutcome> {
    return send_sticker_impl(gateway_, std::move(request), std::nullopt);
  }
  auto send_sticker(SendStickerRequest request,
                    obcx::bot::OperationCorrelation correlation)
      -> boost::asio::awaitable<obcx::bot::SendOutcome> {
    return send_sticker_impl(gateway_, std::move(request),
                             std::move(correlation));
  }

private:
  // The gateway outlives execution; the lightweight facade need not.
  static auto send_sticker_impl(
      obcx::bot::BotOperationGateway &gateway, SendStickerRequest request,
      std::optional<obcx::bot::OperationCorrelation> correlation)
      -> boost::asio::awaitable<obcx::bot::SendOutcome> {
    std::optional<obcx::bot::OperationEnvelope> envelope;
    std::optional<obcx::bot::SendOutcomeEnvelope> reply_allowance;
    try {
      request.validate();
      if (correlation)
        correlation->validate();
      envelope.emplace(obcx::bot::OperationEnvelope{
          request.target.installation, request.action,
          obcx::bot::GatewayCodec<SendStickerRequest>::encode(request),
          correlation});
      envelope->validate();
      // Direct SendOutcome wire, including its real OperationReply shell.
      // Both normal decoding and malformed-outer recovery share this allowance.
      reply_allowance.emplace(obcx::bot::SendOutcomeEnvelope::for_reply(
          obcx::bot::SendOutcomeEnvelope::standalone()));
    } catch (...) {
      co_return obcx::bot::send_failure(
          {.code = obcx::bot::BotOperationErrorCode::InvalidRequest,
           .message = "invalid sticker request"},
          {}, false);
    }
    try {
      auto reply = co_await gateway.invoke(std::move(*envelope));
      if (reply.value) {
        auto outcome =
            decode_sticker_outcome(*reply.value, request, *reply_allowance);
        bool malformed_outer = false;
        try {
          reply.validate();
        } catch (...) {
          malformed_outer = true;
        }
        if (reply.error || malformed_outer) {
          auto details = std::visit(
              [](auto &v) { return std::move(v.output_details); }, outcome);
          co_return obcx::bot::send_failure(
              obcx::bot::detail::unknown_send_error(), std::move(details),
              true);
        }
        co_return outcome;
      }
      reply.validate();
      if (reply.error)
        co_return obcx::bot::send_failure(
            *reply.error, {},
            reply.error->submission_safety ==
                obcx::bot::SubmissionSafety::PossiblySubmitted);
    } catch (...) {
    }
    co_return obcx::bot::send_failure(obcx::bot::detail::unknown_send_error(),
                                      {}, true);
  }
  obcx::bot::BotOperationGateway &gateway_;
};

} // namespace obcx::telegram::bot
