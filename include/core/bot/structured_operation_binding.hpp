#pragma once

// Process-only executable definition. Never installed as an Actor SDK header.
#include "core/bot/operation_registry.hpp"

namespace obcx::core {

// Scalar outcomes only. Message-producing operations need a separate effects
// policy; a successful opaque carrier alone is not evidence of completion.
template <bot::TypedOperation Request> class StructuredOperationDefinition {
public:
  using request_type = Request;
  using Traits = bot::OperationTraits<Request>;
  using Result = typename Traits::result_type;
  using Handler =
      std::function<boost::asio::awaitable<StructuredOperationCompletion>(
          const Request &)>;

  static_assert(Traits::requires_structured_result == true);
  static_assert(Traits::requires_outcome_client == true);
  static_assert(Traits::message_producing == false);

  explicit StructuredOperationDefinition(
      std::vector<std::string> required_capabilities)
      : description_{Traits::action(), Traits::side_effecting,
                     std::move(required_capabilities), false} {
    for (const auto &dependency : description_.required_capabilities)
      (void)bot::ActionId{dependency};
    std::ranges::sort(description_.required_capabilities);
    if (std::ranges::adjacent_find(description_.required_capabilities) !=
        description_.required_capabilities.end())
      throw std::invalid_argument(
          "operation has duplicate capability dependencies");
  }

  [[nodiscard]] auto description() const -> const OperationDescription & {
    return description_;
  }

  // The process-owned handler derives disposition from its validated bot
  // outcome and retains native evidence through ordinary projection failures.
  [[nodiscard]] auto bind(Handler handler) const -> OperationBinding {
    if (!handler)
      throw std::invalid_argument("operation requires an executable handler");
    return OperationBinding{
        description_, &Traits::supports_surface, typeid(Result),
        [handler = std::move(handler)](bot::OperationEnvelope envelope)
            -> boost::asio::awaitable<StructuredOperationCompletion> {
          std::optional<Request> request;
          try {
            envelope.validate();
            if (envelope.action != Traits::action())
              throw std::invalid_argument("operation binding action mismatch");
            request.emplace(bot::GatewayCodec<Request>::decode(
                std::move(envelope.payload)));
            request->validate();
            if (Traits::installation(*request) != envelope.installation)
              throw std::invalid_argument(
                  "operation payload installation mismatch");
          } catch (...) {
            co_return StructuredOperationCompletion{
                bot::StructuredOperationReply::failure(
                    {.code = bot::BotOperationErrorCode::InvalidRequest,
                     .message =
                         "operation payload failed codec or scope validation",
                     .retryable = false,
                     .submission_safety =
                         bot::SubmissionSafety::DefinitelyNotSubmitted}),
                StructuredCompletionDisposition::Failed};
          }
          try {
            auto completion = co_await handler(*request);
            completion.reply.validate();
            if (completion.reply.value &&
                !completion.reply.value->template contains<Result>(
                    envelope.installation, envelope.action))
              throw std::invalid_argument(
                  "structured result type or scope mismatch");
            switch (completion.disposition) {
            case StructuredCompletionDisposition::Completed:
            case StructuredCompletionDisposition::AcceptedPending:
              if (!completion.reply.ok())
                throw std::invalid_argument(
                    "structured outcome requires a value");
              break;
            case StructuredCompletionDisposition::Failed:
              if (completion.reply.error) {
                const auto &error = *completion.reply.error;
                // Trusted handler admission assertion, not proof inferred from
                // provider data. Native failures remain whole typed outcomes.
                if (error.submission_safety !=
                        bot::SubmissionSafety::DefinitelyNotSubmitted ||
                    error.code ==
                        bot::BotOperationErrorCode::ProviderRejected ||
                    error.code ==
                        bot::BotOperationErrorCode::MalformedResponse ||
                    error.provider_code)
                  throw std::invalid_argument(
                      "structured error is not an admission error");
              }
              break;
            default:
              throw std::invalid_argument(
                  "invalid structured completion disposition");
            }
            co_return std::move(completion);
          } catch (...) {
            // This fallback cannot recover native evidence a broken handler
            // failed to return. It must never manufacture non-submission.
            co_return StructuredOperationCompletion{
                bot::StructuredOperationReply::failure(
                    {.code = bot::BotOperationErrorCode::OutcomeUnknown,
                     .message = "operation handler did not produce a valid "
                                "structured result",
                     .retryable = false,
                     .submission_safety =
                         bot::SubmissionSafety::PossiblySubmitted}),
                StructuredCompletionDisposition::Failed};
          }
        }};
  }

private:
  OperationDescription description_;
};

} // namespace obcx::core
