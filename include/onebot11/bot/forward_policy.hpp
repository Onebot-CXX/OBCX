#pragma once

// Process-only registration; public request/result layouts remain schema 1.
#include "core/bot/message_operation_binding.hpp"
#include "onebot11/bot/forward_outcome.hpp"

namespace obcx::onebot11::bot {
inline auto forward_message_policy()
    -> core::MessageResultPolicy<SendOneBotGroupForwardMessageRequest> {
  const auto allowance = forward_outcome::envelope();
  return {.context =
              [allowance](const SendOneBotGroupForwardMessageRequest &request) {
                return core::MessageOutcomeContext{request.target, nullptr,
                                                   allowance};
              },
          .outcome = [](OneBotGroupForwardMessageResult &value) noexcept
              -> obcx::bot::SendOutcome & { return value.outcome; },
          .normalize_outcome =
              [allowance](const SendOneBotGroupForwardMessageRequest &request,
                          obcx::bot::SendOutcome outcome) {
                return obcx::bot::normalize_single_message_outcome(
                    request.target, std::move(outcome), allowance);
              },
          .normalize_result =
              [allowance](const SendOneBotGroupForwardMessageRequest &request,
                          const OneBotGroupForwardMessageResult *original,
                          obcx::bot::SendOutcome outcome) {
                return forward_outcome::assemble(request, original,
                                                 std::move(outcome), allowance);
              },
          .make_failure =
              [allowance](const SendOneBotGroupForwardMessageRequest &,
                          obcx::bot::SendFailure failure) {
                // Core supplied a checkpoint reserved for this exact minimal
                // wrapper. No clipping, secondary recovery, optional
                // duplication or new limits.
                auto result = forward_outcome::minimal(std::move(failure));
                allowance.validate_result_wire(
                    obcx::bot::GatewayCodec<
                        OneBotGroupForwardMessageResult>::encode(result));
                return result;
              }};
}
} // namespace obcx::onebot11::bot
