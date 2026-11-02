#pragma once

// Process-only policies. Opaque native outcomes are never serialized here.
#include "core/bot/owned_structured_operation_binding.hpp"

namespace obcx::core {

template <bot::TypedOperation Request> struct StructuredMessagePolicy {
  using Result = typename bot::OperationTraits<Request>::result_type;
  std::function<std::optional<bot::ConversationRef>(const Request &)> target;
  std::function<std::optional<bot::SendOutcome>(const Request &,
                                                const Result &)>
      project;
  std::function<void(const Request &, const Result &, const bot::SendOutcome &,
                     ConfirmedAuthorEvidenceCollector &)>
      collect_confirmed_authors;
};

namespace structured_operation_detail {

template <bot::TypedOperation Request>
void validate_message_policy(const StructuredMessagePolicy<Request> &policy) {
  using Traits = bot::OperationTraits<Request>;
  static_assert(Traits::requires_structured_result == true);
  static_assert(Traits::requires_outcome_client == true);
  static_assert(Traits::message_producing == true);
  static_assert(Traits::side_effecting == true);
  if (!policy.target || !policy.project)
    throw std::invalid_argument(
        "structured message operation requires both policies");
}

template <bot::TypedOperation Request>
auto message_target(const Request &request,
                    const StructuredMessagePolicy<Request> &policy)
    -> std::optional<bot::ConversationRef> {
  auto target = policy.target(request);
  if (target) {
    target->validate();
    if (target->installation !=
        bot::OperationTraits<Request>::installation(request))
      throw std::invalid_argument("message target installation mismatch");
  }
  return target;
}

template <bot::TypedOperation Request>
auto invoke_message_handler(
    const Request &request, const std::optional<bot::ConversationRef> &target,
    const StructuredMessagePolicy<Request> &policy,
    const std::function<boost::asio::awaitable<StructuredOperationCompletion>(
        const Request &)> &handler)
    -> boost::asio::awaitable<StructuredOperationCompletion> {
  using Traits = bot::OperationTraits<Request>;
  using Result = typename Traits::result_type;
  // Prepare conservative evidence before I/O. A projection cannot promise
  // recovery of an ID it throws before returning; the opaque result survives.
  std::optional<bot::SendOutcome> recovered;
  std::optional<StructuredOperationCompletion> unknown_completion;
  try {
    if (target)
      recovered.emplace(
          bot::send_failure(bot::detail::unknown_send_error(), {}, true));
    unknown_completion.emplace(failure(false));
  } catch (...) {
    co_return failure(true);
  }
  std::optional<StructuredOperationCompletion> completion;
  try {
    completion.emplace(co_await handler(request));
  } catch (...) {
    unknown_completion->message_outcome = std::move(recovered);
    co_return std::move(*unknown_completion);
  }
  // Handler-supplied sidecars are not policy evidence.
  completion->message_outcome.reset();
  std::vector<ConfirmedAuthorEvidence>{}.swap(completion->confirmed_authors);
  ConfirmedAuthorEvidenceCollector authors;
  try {
    if (completion->reply.value) {
      completion->reply.value->template visit<Result>(
          Traits::installation(request), Traits::action(),
          [&](const Result &result) {
            // Admit native request-target/mode/evidence before trusting any
            // projected physical reference. Outer wrapper validation is later.
            Traits::validate_result(request, result);
            auto projected = policy.project(request, result);
            if (target.has_value() != projected.has_value())
              throw std::invalid_argument(
                  "message target and effects presence mismatch");
            if (projected) {
              const MessageOutcomeContext context{*target, nullptr};
              bot::detail::SendEvidenceRecovery recovery(*target,
                                                         context.envelope);
              try {
                recovery.outcome(*projected);
              } catch (const bot::detail::DomainBudgetExceeded &) {
                // A bounded independently scoped prefix remains known.
              }
              recovered = recovery.finish();
              // The scoped physical checkpoint is independent of authorship
              // and survives later metadata/outer-wrapper validation failure.
              if (policy.collect_confirmed_authors &&
                  completion->disposition !=
                      StructuredCompletionDisposition::AcceptedPending) {
                try {
                  policy.collect_confirmed_authors(request, result, *recovered,
                                                   authors);
                } catch (...) {
                  // Optional proof cannot downgrade or retry the send.
                }
              }
              message_operation_detail::validate_effects(context, *projected);
              message_operation_detail::require_retained_references(*recovered,
                                                                    *projected);
            }
            completion->message_outcome = std::move(projected);
          });
    } else {
      validate_completion(*completion);
      if (target && completion->reply.error) {
        completion->message_outcome =
            bot::send_failure(*completion->reply.error, {}, false);
      }
    }
    validate_completion(*completion);
    if (completion->disposition ==
            StructuredCompletionDisposition::AcceptedPending ||
        (completion->disposition == StructuredCompletionDisposition::Failed &&
         completion->message_outcome &&
         std::holds_alternative<bot::SendReceipt>(
             *completion->message_outcome)))
      completion->message_outcome = std::move(recovered);
  } catch (...) {
    // Do not repair or erase the original carrier. Send clients can
    // independently validate stronger native evidence despite a malformed outer
    // wrapper.
    if (!completion->reply.value) {
      unknown_completion->message_outcome = std::move(recovered);
      co_return std::move(*unknown_completion);
    }
    completion->disposition = StructuredCompletionDisposition::Failed;
    completion->message_outcome = std::move(recovered);
  }
  completion->confirmed_authors = authors.take(completion->message_outcome);
  co_return std::move(*completion);
}

} // namespace structured_operation_detail

template <bot::TypedOperation Request>
class StructuredMessageOperationDefinition {
public:
  using request_type = Request;
  using Traits = bot::OperationTraits<Request>;
  using Result = typename Traits::result_type;
  using Handler =
      std::function<boost::asio::awaitable<StructuredOperationCompletion>(
          const Request &)>;

  explicit StructuredMessageOperationDefinition(
      std::vector<std::string> required_capabilities,
      StructuredMessagePolicy<Request> policy)
      : description_{Traits::action(), Traits::side_effecting,
                     std::move(required_capabilities), true},
        policy_(std::move(policy)) {
    if constexpr (requires { Traits::requires_owned_request; })
      static_assert(!Traits::requires_owned_request);
    structured_operation_detail::validate_message_policy(policy_);
    structured_operation_detail::validate_dependencies(description_);
  }

  [[nodiscard]] auto description() const -> const OperationDescription & {
    return description_;
  }

  [[nodiscard]] auto bind(Handler handler) const -> OperationBinding {
    if (!handler)
      throw std::invalid_argument("operation requires an executable handler");
    return OperationBinding{
        description_, &Traits::supports_surface, typeid(Result),
        [handler = std::move(handler),
         policy = policy_](bot::OperationEnvelope envelope)
            -> boost::asio::awaitable<StructuredOperationCompletion> {
          std::optional<Request> request;
          std::optional<bot::ConversationRef> target;
          try {
            envelope.validate();
            if (envelope.action != Traits::action())
              throw std::invalid_argument("operation binding action mismatch");
            request.emplace(bot::GatewayCodec<Request>::decode(
                std::move(envelope.payload)));
            request->validate();
            if (Traits::installation(*request) != envelope.installation)
              throw std::invalid_argument(
                  "operation request installation mismatch");
            target =
                structured_operation_detail::message_target(*request, policy);
          } catch (...) {
            co_return structured_operation_detail::failure(true);
          }
          co_return co_await structured_operation_detail::
              invoke_message_handler(*request, target, policy, handler);
        }};
  }

private:
  OperationDescription description_;
  StructuredMessagePolicy<Request> policy_;
};

template <bot::TypedOperation Request>
class OwnedStructuredMessageOperationDefinition {
public:
  using request_type = Request;
  using Traits = bot::OperationTraits<Request>;
  using Result = typename Traits::result_type;
  using Handler =
      std::function<boost::asio::awaitable<StructuredOperationCompletion>(
          const Request &)>;

  explicit OwnedStructuredMessageOperationDefinition(
      std::vector<std::string> required_capabilities,
      StructuredMessagePolicy<Request> policy)
      : description_{Traits::action(), Traits::side_effecting,
                     std::move(required_capabilities), true},
        policy_(std::move(policy)) {
    static_assert(Traits::requires_owned_request == true);
    structured_operation_detail::validate_message_policy(policy_);
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
        [handler = std::move(handler),
         policy = policy_](bot::OwnedStructuredOperationEnvelope envelope)
            -> boost::asio::awaitable<StructuredOperationCompletion> {
          std::unique_ptr<Request> request;
          std::optional<bot::ConversationRef> target;
          try {
            envelope.validate();
            if (envelope.action != Traits::action())
              throw std::invalid_argument("operation binding action mismatch");
            request = envelope.template take<Request>();
            request->validate();
            if (Traits::installation(*request) != envelope.installation)
              throw std::invalid_argument(
                  "operation request installation mismatch");
            target =
                structured_operation_detail::message_target(*request, policy);
          } catch (...) {
            co_return structured_operation_detail::failure(true);
          }
          co_return co_await structured_operation_detail::
              invoke_message_handler(*request, target, policy, handler);
        }};
  }

private:
  OperationDescription description_;
  StructuredMessagePolicy<Request> policy_;
};

} // namespace obcx::core
