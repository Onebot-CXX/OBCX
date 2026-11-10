#pragma once
#include "core/bot/bot_queries.hpp"
#include "core/bot/send_gateway_codec.hpp"

namespace obcx::bot {
// Thin generation-owned facade over the process-owned gateway. It does not own
// provider clients, spawn work, reset deadlines, retry, or maintain a cache.
// Deferred operations retain the gateway reference, not this facade. The
// gateway must outlive execution; the facade need not outlive its returned
// awaitable.
class BotClient {
public:
  explicit BotClient(BotOperationGateway &gateway) : gateway_(gateway) {}
  auto send(SendMessageRequest request) -> boost::asio::awaitable<SendOutcome> {
    return send_impl(gateway_, std::move(request), std::nullopt);
  }
  auto send(SendMessageRequest request, OperationCorrelation correlation)
      -> boost::asio::awaitable<SendOutcome> {
    return send_impl(gateway_, std::move(request), std::move(correlation));
  }
  auto delete_message(DeleteMessageRequest request)
      -> boost::asio::awaitable<BotOperationResult<DeleteReceipt>> {
    return invoke(gateway_, std::move(request));
  }
  auto get_self(GetSelfRequest request)
      -> boost::asio::awaitable<BotOperationResult<SelfInfo>> {
    return invoke(gateway_, std::move(request));
  }
  auto get_member(GetMemberRequest request)
      -> boost::asio::awaitable<BotOperationResult<MemberInfo>> {
    return invoke(gateway_, std::move(request));
  }
  auto resolve_reply(ResolveReplyRequest request)
      -> boost::asio::awaitable<BotOperationResult<ReplyInfo>> {
    return invoke(gateway_, std::move(request));
  }
  auto read_media(ReadMediaRequest request)
      -> boost::asio::awaitable<BotOperationResult<MediaBlob>> {
    return invoke(gateway_, std::move(request));
  }
  auto capabilities(GetCapabilitiesRequest request)
      -> boost::asio::awaitable<BotOperationResult<ConversationCapabilities>> {
    return invoke(gateway_, std::move(request));
  }

private:
  static auto send_impl(BotOperationGateway &gateway,
                        SendMessageRequest request,
                        std::optional<OperationCorrelation> correlation)
      -> boost::asio::awaitable<SendOutcome> {
    std::optional<OperationEnvelope> envelope;
    std::optional<SendOutcomeEnvelope> reply_allowance;
    try {
      if (correlation)
        correlation->validate();
      request.validate();
      if (!OperationTraits<SendMessageRequest>::supports_surface(
              request.target.installation.surface))
        throw std::invalid_argument("unsupported send surface");
      // Validate before the bounded copy; the envelope owns the supplied ID
      // before gateway suspension, independently of the caller's value.
      envelope.emplace(OperationEnvelope{
          request.target.installation, SendMessageRequest::action,
          GatewayCodec<SendMessageRequest>::encode(request), correlation});
      envelope->validate();
      // Capture the complete reply allowance before gateway suspension. Both
      // normal decoding and malformed-value recovery use this same boundary.
      reply_allowance.emplace(
          SendOutcomeEnvelope::for_reply(SendOutcomeEnvelope::standalone()));
    } catch (...) {
      co_return send_failure(
          {.code = BotOperationErrorCode::InvalidRequest,
           .message = "send request failed validation or encoding",
           .provider_code = std::nullopt,
           .retry_after = std::nullopt,
           .retryable = false,
           .submission_safety = SubmissionSafety::DefinitelyNotSubmitted},
          {}, false);
    }
    try {
      auto reply = co_await gateway.invoke(std::move(*envelope));
      // Even a malformed outer envelope with a value can retain confirmations.
      // Do not call ordinary invoke<SendMessageRequest>, which flattens errors.
      try {
        reply.validate();
      } catch (...) {
        if (reply.value) {
          auto salvaged =
              decode_send_outcome(*reply.value, request, *reply_allowance);
          auto details = std::visit(
              [](auto &v) { return std::move(v.output_details); }, salvaged);
          co_return send_failure(detail::unknown_send_error(),
                                 std::move(details), true);
        }
        co_return send_failure(detail::unknown_send_error(), {}, true);
      }
      if (!reply.ok()) {
        const auto unknown = reply.error->submission_safety ==
                             SubmissionSafety::PossiblySubmitted;
        co_return send_failure(std::move(*reply.error), {}, unknown);
      }
      co_return decode_send_outcome(*reply.value, request, *reply_allowance);
    } catch (...) {
      co_return send_failure(detail::unknown_send_error(), {}, true);
    }
  }
  BotOperationGateway &gateway_;
};
} // namespace obcx::bot
