#pragma once

// Process-only outcome binding. Public gateway/result wire layouts are
// unchanged.
#include "core/bot/native_reference_codec.hpp"
#include "core/bot/send_gateway_codec.hpp"
#include "core/bot/typed_operation.hpp"

#include <algorithm>
#include <functional>
#include <optional>

namespace obcx::core {

// Invocation-local, identity-only metadata; never a wire/public result carrier.
struct ConfirmedAuthorEvidence {
  bot::MessageRef message;
  std::optional<bot::Sender> native_author;
  bool initiating_self_proven;
  bool author_conflicted;
};

namespace confirmed_author_detail {
inline auto same_author(const bot::Sender &left, const bot::Sender &right)
    -> bool {
  return std::visit(
      [&]<class Author>(const Author &author) {
        const auto *other = std::get_if<Author>(&right);
        if (!other)
          return false;
        if constexpr (std::same_as<Author, bot::KnownUser>)
          return author.user == other->user;
        else if constexpr (std::same_as<Author, bot::ConversationSender>)
          return author.conversation == other->conversation;
        else
          return false;
      },
      left);
}
inline auto known_in_installation(const bot::Sender &sender,
                                  const bot::BotInstallationRef &installation)
    -> bool {
  return std::visit(
      [&]<class Author>(const Author &author) {
        if constexpr (std::same_as<Author, bot::KnownUser>)
          return author.user.installation == installation;
        else if constexpr (std::same_as<Author, bot::ConversationSender>)
          return author.conversation.installation == installation;
        else
          return false;
      },
      sender);
}
inline auto messages(const bot::SendOutcome &outcome)
    -> const std::vector<bot::MessageRef> & {
  return std::visit(
      []<class Outcome>(
          const Outcome &value) -> const std::vector<bot::MessageRef> & {
        if constexpr (std::same_as<Outcome, bot::SendReceipt>)
          return value.messages;
        else
          return value.confirmed_messages;
      },
      outcome);
}
} // namespace confirmed_author_detail

class ConfirmedAuthorEvidenceCollector {
public:
  void initiating_self(const bot::MessageRef &message) noexcept {
    collect(message, nullptr);
  }
  // The owning platform validates native identity before supplying a claim.
  void native_author(const bot::MessageRef &message, const bot::Sender &author,
                     const bot::binding::NativeReferenceCodec &codec) noexcept {
    try {
      if (!confirmed_author_detail::known_in_installation(
              author, message.conversation.installation))
        return;
      bot::binding::validate_native_sender(codec, author);
      collect(message, &author);
    } catch (...) {
      // Optional proof must never change a physical send's result.
    }
  }

  auto take(const std::optional<bot::SendOutcome> &outcome) noexcept
      -> std::vector<ConfirmedAuthorEvidence> {
    if (!outcome)
      records_.clear();
    else {
      const auto &physical = confirmed_author_detail::messages(*outcome);
      std::erase_if(records_, [&](const auto &record) {
        return std::ranges::find(physical, record.message) == physical.end();
      });
    }
    return std::move(records_);
  }

private:
  template <class Reference>
  static void charge_reference(bot::detail::DomainWireBudget &budget,
                               const Reference &reference) {
    budget.nodes();
    budget.bytes(reference.installation.installation_id.size());
    budget.bytes(reference.installation.surface.value().size());
    budget.bytes(reference.id.serialized().size());
  }
  void collect(const bot::MessageRef &message,
               const bot::Sender *author) noexcept {
    try {
      // Compare before replacement copies or budget charges: conflicts stay
      // sticky even when the collector cannot retain another identity.
      auto found = std::ranges::find(records_, message,
                                     &ConfirmedAuthorEvidence::message);
      if (found != records_.end()) {
        if (!author) {
          found->initiating_self_proven = true;
          return;
        }
        if (found->native_author) {
          if (!confirmed_author_detail::same_author(*found->native_author,
                                                    *author))
            found->author_conflicted = true;
          return;
        }
      }
      message.validate();
      auto charged = budget_;
      if (found == records_.end()) {
        charged.nodes();
        charge_reference(charged, message.conversation);
        charged.bytes(message.id.serialized().size());
      }
      if (author)
        std::visit(
            [&]<class Author>(const Author &value) {
              if constexpr (std::same_as<Author, bot::KnownUser>)
                charge_reference(charged, value.user);
              else if constexpr (std::same_as<Author, bot::ConversationSender>)
                charge_reference(charged, value.conversation);
            },
            *author);
      // All identity/scope/item/byte checks precede retained copies.
      std::optional<bot::Sender> native;
      if (author)
        native = *author;
      if (found == records_.end())
        records_.push_back({message, std::move(native), !author, false});
      else
        found->native_author = std::move(native);
      budget_ = charged;
    } catch (...) {
      // Exhaustion loses only new optional evidence, never existing conflicts.
    }
  }
  bot::detail::DomainWireBudget budget_;
  std::vector<ConfirmedAuthorEvidence> records_;
};

struct OperationCompletion {
  bot::OperationReply reply;
  std::optional<bot::SendOutcome> message_outcome;
  std::vector<ConfirmedAuthorEvidence> confirmed_authors;
};

struct MessageOutcomeContext {
  const bot::ConversationRef &target;
  // Only the actual common send request may supply body associations.
  const bot::SendMessageRequest *common_request;
  bot::SendOutcomeEnvelope envelope;

  MessageOutcomeContext(const bot::ConversationRef &target_value,
                        const bot::SendMessageRequest *request)
      : MessageOutcomeContext(target_value, request,
                              bot::SendOutcomeEnvelope::for_reply(
                                  bot::SendOutcomeEnvelope::standalone())) {}
  MessageOutcomeContext(const bot::ConversationRef &target_value,
                        const bot::SendMessageRequest *request,
                        bot::SendOutcomeEnvelope result_envelope)
      : target(target_value), common_request(request),
        envelope(std::move(result_envelope)) {
    if (!envelope.is_reply())
      throw std::invalid_argument(
          "process outcome context requires a full reply envelope");
  }
};

template <bot::TypedOperation Request> struct MessageResultPolicy {
  using Result = typename bot::OperationTraits<Request>::result_type;
  std::function<MessageOutcomeContext(const Request &)> context;
  bot::SendOutcome &(*outcome)(Result &) noexcept;
  std::function<bot::SendOutcome(const Request &, bot::SendOutcome)>
      normalize_outcome;
  // original retains auxiliary fields, but its primary outcome has been moved.
  // Repair malformed optional fields selectively; do not throw for bad
  // metadata.
  std::function<Result(const Request &, const Result *original,
                       bot::SendOutcome)>
      normalize_result;
  // Last-resort callback-contract failure only: build a valid minimal typed
  // result retaining EVERY supplied effect within the full wrapper wire
  // budget. A policy unable to guarantee this is not eligible for registration.
  // No provider I/O, clipping, or invented facts.
  std::function<Result(const Request &, bot::SendFailure)> make_failure;

  void validate() const {
    if (!context || !outcome || !normalize_outcome || !normalize_result ||
        !make_failure)
      throw std::invalid_argument(
          "message operation requires a complete outcome policy");
  }
};

namespace message_operation_detail {
inline auto outputs(const bot::SendOutcome &outcome)
    -> const std::vector<bot::SendOutputDetails> & {
  return std::visit(
      [](const auto &value) -> const std::vector<bot::SendOutputDetails> & {
        return value.output_details;
      },
      outcome);
}

inline auto unknown(const bot::SendOutcome &retained) -> bot::SendOutcome {
  return bot::send_failure(bot::detail::unknown_send_error(), outputs(retained),
                           true);
}

inline void validate_effects(const MessageOutcomeContext &context,
                             const bot::SendOutcome &outcome) {
  // Full result allowance precedes validation/encoding copies, and includes
  // the unknown fallback needed if a later callback fails.
  (void)context.envelope.reserve_primary(outcome);
  std::visit([](const auto &value) { value.validate(); }, outcome);
  for (const auto &output : outputs(outcome)) {
    if (output.message.conversation != context.target)
      throw std::invalid_argument("message outcome target mismatch");
    if (!context.common_request) {
      if (output.content)
        throw std::invalid_argument("extension has no common body provenance");
      for (const auto &media : output.reusable_media)
        if (media.block_index)
          throw std::invalid_argument(
              "extension has no common media block index");
    }
  }
  if (context.common_request)
    bot::validate_send_outcome(*context.common_request, outcome);
}

// A normalizer must not erase a bounded, independently verified reference.
inline void require_retained_references(const bot::SendOutcome &retained,
                                        const bot::SendOutcome &candidate) {
  const auto &after = outputs(candidate);
  std::size_t next = 0;
  for (const auto &before : outputs(retained)) {
    while (next < after.size() && after[next].message != before.message)
      ++next;
    if (next == after.size())
      throw std::invalid_argument(
          "message normalizer discarded confirmed output");
    ++next;
  }
}

// Auxiliary normalization may downgrade success, never discard primary output
// metadata or upgrade a failure/unknown into success or permission to retry.
inline void require_preserved_outcome(const bot::SendOutcome &retained,
                                      const bot::SendOutcome &candidate) {
  const auto before = bot::GatewayCodec<bot::SendOutcome>::encode(retained);
  const auto after = bot::GatewayCodec<bot::SendOutcome>::encode(candidate);
  if (before.at("value").at("output_details") !=
      after.at("value").at("output_details"))
    throw std::invalid_argument(
        "result repair changed verified output metadata");
  if (const auto *failure = std::get_if<bot::SendFailure>(&retained)) {
    const auto *result = std::get_if<bot::SendFailure>(&candidate);
    if (!result ||
        (failure->may_have_unconfirmed_messages &&
         !result->may_have_unconfirmed_messages) ||
        (failure->error.submission_safety ==
             bot::SubmissionSafety::PossiblySubmitted &&
         result->error.submission_safety !=
             bot::SubmissionSafety::PossiblySubmitted) ||
        (!failure->error.retryable && result->error.retryable))
      throw std::invalid_argument(
          "result repair upgraded an uncertain or failed send");
  }
}
} // namespace message_operation_detail

template <bot::TypedOperation Request>
auto complete_message_operation(
    const Request &request, const MessageOutcomeContext &context,
    const MessageResultPolicy<Request> &policy,
    bot::BotOperationResult<typename bot::OperationTraits<Request>::result_type>
        result) -> OperationCompletion {
  using Traits = bot::OperationTraits<Request>;
  using Result = typename Traits::result_type;
  namespace detail = message_operation_detail;
  auto effects = bot::send_failure(bot::detail::unknown_send_error(), {}, true);
  Result *original = result.value ? &*result.value : nullptr;
  if (original) {
    // Inspect borrowed primary evidence under the shared SDK work/output
    // budgets BEFORE copying containers or calling any potentially throwing
    // policy.
    auto recovery = context.common_request
                        ? bot::detail::SendEvidenceRecovery(
                              *context.common_request, context.envelope)
                        : bot::detail::SendEvidenceRecovery(context.target,
                                                            context.envelope);
    try {
      recovery.outcome(policy.outcome(*original));
    } catch (const bot::detail::DomainBudgetExceeded &) {
      // A bounded prefix remains known; the remainder is unknown.
    }
    effects = recovery.finish();
    try {
      auto normalized = policy.normalize_outcome(
          request, std::move(policy.outcome(*original)));
      detail::validate_effects(context, normalized);
      // validate_effects reserved both forms under the SAME full-envelope
      // allowance before this checkpoint can expand.
      detail::require_retained_references(effects, normalized);
      effects = std::move(normalized);
    } catch (...) {
      // effects is already an independently recovered, encodable unknown.
    }
    if (result.error)
      effects = detail::unknown(effects);
  } else {
    try {
      result.validate();
      const bool uncertain = result.error->submission_safety ==
                             bot::SubmissionSafety::PossiblySubmitted;
      effects = bot::send_failure(std::move(*result.error), {}, uncertain);
    } catch (...) {
      // Missing/broken outer result is not evidence of non-submission.
    }
  }

  const auto encode = [&](Result candidate) -> OperationCompletion {
    const auto &actual = policy.outcome(candidate);
    detail::validate_effects(context, actual);
    detail::require_preserved_outcome(effects, actual);
    Traits::validate_result(request, candidate);
    auto wire = bot::GatewayCodec<Result>::encode(candidate);
    context.envelope.validate_result_wire(wire);
    return {bot::OperationReply::success(std::move(wire)), actual};
  };
  try {
    return encode(policy.normalize_result(request, original, effects));
  } catch (...) {
    effects = detail::unknown(effects);
  }
  try {
    // Keep borrowed auxiliary evidence available to the repairing callback;
    // malformed outer metadata or an encoder failure cannot erase it first.
    return encode(policy.normalize_result(request, original, effects));
  } catch (...) {
    // A policy that still fails its repair contract must at least preserve the
    // primary effects in a minimal, valid result of its own declared type.
  }
  try {
    return encode(
        policy.make_failure(request, std::get<bot::SendFailure>(effects)));
  } catch (...) {
    // Internal policy-contract failure, never an accepted provider path. The
    // caller cannot receive a valid arbitrary extension record, but must not
    // gain retry permission, and already captured observations must survive.
    auto error = bot::detail::unknown_send_error();
    error.message = "message operation fallback violated its result contract";
    return {bot::OperationReply::failure(std::move(error)), std::move(effects)};
  }
}

template <bot::TypedOperation Request>
auto default_message_result_policy()
    -> std::optional<MessageResultPolicy<Request>> {
  return std::nullopt;
}

template <>
inline auto default_message_result_policy<bot::SendMessageRequest>()
    -> std::optional<MessageResultPolicy<bot::SendMessageRequest>> {
  const auto envelope = bot::SendOutcomeEnvelope::for_reply(
      bot::SendOutcomeEnvelope::standalone());
  return MessageResultPolicy<bot::SendMessageRequest>{
      .context =
          [envelope](const bot::SendMessageRequest &request) {
            return MessageOutcomeContext{request.target, &request, envelope};
          },
      .outcome = [](bot::SendOutcome &value) noexcept -> bot::SendOutcome & {
        return value;
      },
      .normalize_outcome =
          [envelope](const bot::SendMessageRequest &request,
                     bot::SendOutcome value) {
            return bot::normalize_send_outcome(request, std::move(value),
                                               envelope);
          },
      .normalize_result = [](const bot::SendMessageRequest &,
                             const bot::SendOutcome *,
                             bot::SendOutcome value) { return value; },
      .make_failure = [](const bot::SendMessageRequest &,
                         bot::SendFailure failure) -> bot::SendOutcome {
        return failure;
      }};
}

} // namespace obcx::core
