#pragma once

#include "onebot11/bot/standard_common.hpp"

#include <variant>

namespace obcx::onebot11::bot::standard {

enum class Submission : std::uint8_t {
  NotSubmitted,
  PossiblySubmitted,
  Accepted
};

enum class NativeCompletion : std::uint8_t {
  NotObserved,
  AcceptedPending,
  ProviderReportedCompleted,
  ProviderReportedFailed
};

enum class FailureStage : std::uint8_t {
  BeforeSubmission,
  Transport,
  NativeReply,
  Projection
};

// Scalar evidence survives client projection failure. This is NOT message
// evidence, effect-subject authority, verified remote state or retry
// permission.
struct ExecutionEvidence {
  Submission submission;
  NativeCompletion completion;
  std::optional<std::int64_t> provider_retcode;

  void validate() const;
};

struct NoResponseData {};

template <typename T> struct Completed {
  T value;
};

// No result value, message ID, ticket, release time or eventual query result.
// The binding must checkpoint the async/1 header, then require native data to
// be present and null BEFORE producing this public value. validate() checks
// only the retained fields; it cannot establish that native data was valid.
struct AcceptedPending {
  ExecutionMode requested_mode;
  std::int64_t provider_retcode;

  void validate() const;
};

struct ExecutionFailure {
  obcx::bot::BotOperationError error;
  FailureStage stage;

  void validate(const ExecutionEvidence &evidence) const;
};

template <typename T> struct ExecutionOutcome {
  std::variant<Completed<T>, AcceptedPending, ExecutionFailure> state;
  ExecutionEvidence evidence;

  // Operation-specific validation of T is a separate registered policy. These
  // wrappers intentionally have no generic Json/reflection serialization.
  void validate() const {
    evidence.validate();
    if (state.valueless_by_exception())
      throw std::invalid_argument("execution outcome has no state");
    if (std::holds_alternative<Completed<T>>(state)) {
      if (evidence.completion != NativeCompletion::ProviderReportedCompleted)
        throw std::invalid_argument("completed result lacks native completion");
    } else if (const auto *pending = std::get_if<AcceptedPending>(&state)) {
      pending->validate();
      if (evidence.completion != NativeCompletion::AcceptedPending ||
          evidence.provider_retcode != pending->provider_retcode)
        throw std::invalid_argument("pending result disagrees with evidence");
    } else {
      std::get<ExecutionFailure>(state).validate(evidence);
    }
  }
};

} // namespace obcx::onebot11::bot::standard
