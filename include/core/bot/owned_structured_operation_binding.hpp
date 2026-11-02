#pragma once

// Process-only executable definition. Never installed as an Actor SDK header.
#include "core/bot/operation_registry.hpp"

namespace obcx::core {
namespace structured_operation_detail {

inline auto failure(bool before_invocation) -> StructuredOperationCompletion {
  return {
      bot::StructuredOperationReply::failure(
          {.code = before_invocation
                       ? bot::BotOperationErrorCode::InvalidRequest
                       : bot::BotOperationErrorCode::OutcomeUnknown,
           .message =
               before_invocation
                   ? "structured request failed type or scope validation"
                   : "operation did not produce a valid structured result",
           .retryable = false,
           .submission_safety =
               before_invocation ? bot::SubmissionSafety::DefinitelyNotSubmitted
                                 : bot::SubmissionSafety::PossiblySubmitted}),
      StructuredCompletionDisposition::Failed};
}

inline void validate_dependencies(OperationDescription &description) {
  for (const auto &dependency : description.required_capabilities)
    (void)bot::ActionId{dependency};
  std::ranges::sort(description.required_capabilities);
  if (std::ranges::adjacent_find(description.required_capabilities) !=
      description.required_capabilities.end())
    throw std::invalid_argument(
        "operation has duplicate capability dependencies");
}

inline void validate_completion(
    const StructuredOperationCompletion &completion) {
  completion.reply.validate();
  switch (completion.disposition) {
  case StructuredCompletionDisposition::Completed:
  case StructuredCompletionDisposition::AcceptedPending:
    if (!completion.reply.ok())
      throw std::invalid_argument("structured outcome requires a value");
    break;
  case StructuredCompletionDisposition::Failed:
    if (completion.reply.error) {
      const auto &error = *completion.reply.error;
      if (error.submission_safety !=
              bot::SubmissionSafety::DefinitelyNotSubmitted ||
          error.code == bot::BotOperationErrorCode::ProviderRejected ||
          error.code == bot::BotOperationErrorCode::MalformedResponse ||
          error.provider_code)
        throw std::invalid_argument(
            "structured error is not an admission error");
    }
    break;
  default:
    throw std::invalid_argument("invalid structured completion disposition");
  }
}

} // namespace structured_operation_detail

// Scalar only: an owned carrier does not bypass message effect accounting.
template <bot::TypedOperation Request>
class OwnedStructuredOperationDefinition {
public:
  using request_type = Request;
  using Traits = bot::OperationTraits<Request>;
  using Result = typename Traits::result_type;
  using Handler =
      std::function<boost::asio::awaitable<StructuredOperationCompletion>(
          const Request &)>;

  static_assert(Traits::requires_owned_request == true);
  static_assert(Traits::requires_structured_result == true);
  static_assert(Traits::requires_outcome_client == true);
  static_assert(Traits::message_producing == false);

  explicit OwnedStructuredOperationDefinition(
      std::vector<std::string> required_capabilities)
      : description_{Traits::action(), Traits::side_effecting,
                     std::move(required_capabilities), false} {
    structured_operation_detail::validate_dependencies(description_);
  }

  [[nodiscard]] auto description() const -> const OperationDescription & {
    return description_;
  }

  [[nodiscard]] auto bind(Handler handler) const -> OperationBinding {
    if (!handler)
      throw std::invalid_argument("operation requires an executable handler");
    return OperationBinding{
        description_, &Traits::supports_surface, typeid(Request),
        typeid(Result),
        [handler =
             std::move(handler)](bot::OwnedStructuredOperationEnvelope envelope)
            -> boost::asio::awaitable<StructuredOperationCompletion> {
          std::unique_ptr<Request> request;
          try {
            envelope.validate();
            if (envelope.action != Traits::action())
              throw std::invalid_argument("operation binding action mismatch");
            request = envelope.template take<Request>();
            request->validate();
            if (Traits::installation(*request) != envelope.installation)
              throw std::invalid_argument(
                  "operation request installation mismatch");
          } catch (...) {
            co_return structured_operation_detail::failure(true);
          }
          std::optional<StructuredOperationCompletion> completion;
          try {
            completion.emplace(co_await handler(*request));
            structured_operation_detail::validate_completion(*completion);
            if (completion->message_outcome)
              throw std::invalid_argument(
                  "scalar operation returned message effects");
            if (completion->reply.value)
              completion->reply.value->template visit<Result>(
                  envelope.installation, envelope.action,
                  [&](const Result &result) {
                    Traits::validate_result(*request, result);
                  });
          } catch (...) {
            if (!completion || !completion->reply.value)
              co_return structured_operation_detail::failure(false);
            // Preserve the original carrier for independent caller recovery.
            completion->disposition = StructuredCompletionDisposition::Failed;
            completion->message_outcome.reset();
          }
          co_return std::move(*completion);
        }};
  }

private:
  OperationDescription description_;
};

} // namespace obcx::core
