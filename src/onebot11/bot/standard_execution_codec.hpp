#pragma once

// Private production helpers: no transport, registry or payload projection.
#include "onebot11/bot/standard_execution.hpp"

namespace obcx::onebot11::bot::standard::detail {

enum class NativeAction : std::uint8_t {
  SendPrivateMsg,
  SendGroupMsg,
  SendMsg,
  DeleteMsg,
  GetMsg,
  GetForwardMsg,
  SendLike,
  SetGroupKick,
  SetGroupBan,
  SetGroupAnonymousBan,
  SetGroupWholeBan,
  SetGroupAdmin,
  SetGroupAnonymous,
  SetGroupCard,
  SetGroupName,
  SetGroupLeave,
  SetGroupSpecialTitle,
  SetFriendAddRequest,
  SetGroupAddRequest,
  GetLoginInfo,
  GetStrangerInfo,
  GetFriendList,
  GetGroupInfo,
  GetGroupList,
  GetGroupMemberInfo,
  GetGroupMemberList,
  GetGroupHonorInfo,
  GetCookies,
  GetCsrfToken,
  GetCredentials,
  GetRecord,
  GetImage,
  CanSendImage,
  CanSendRecord,
  GetStatus,
  GetVersionInfo,
  SetRestart,
  CleanCache,
  HandleQuickOperation
};

[[nodiscard]] auto native_action_name(NativeAction action) -> std::string_view;
[[nodiscard]] auto native_action_name(NativeAction action, ExecutionMode mode)
    -> std::string;
[[nodiscard]] auto standard_action_id(NativeAction action)
    -> obcx::bot::ActionId;

enum class NativeReplyKind : std::uint8_t {
  Completed,
  AcceptedPending,
  Rejected,
  Malformed
};

// Fixed private scalar bookkeeping is excluded from payload output-node
// accounting. Public control/evidence envelopes need separately reserved
// integration/CallLimits accounting; this exception never covers payload DTOs.
struct NativeReplyDisposition {
  NativeReplyKind kind;
  std::optional<std::int64_t> provider_retcode;
};

// Caller supplies an already-owned tree; parsing/transport wire limits and
// duplicate-key detection must precede this seam. Borrowed inspection charges
// ALL unknown fields before extracting any header, without copying payloads.
// ValueLimits follow BotWireBudget accounting (not serialized wire size):
// cumulative string/key/binary bytes, value nodes, root depth=0, per-text
// limit. This classifies headers only: ok/0, async/1, failed/neither 0 nor 1,
// subject to mode/restart restrictions. Signed-int64 retcodes are an SDK
// representation bound, not a protocol int32 rule.
// AFTER checkpointing the header, the binding MUST validate operation data.
// For async, data MUST be present and null before producing public pending;
// missing data is not null. Reject body/projection errors without erasing the
// accepted header evidence or restoring NotSubmitted. This helper alone does
// not validate the full response/data envelope.
[[nodiscard]] auto classify_native_reply(const obcx::bot::Json &reply,
                                         NativeAction action,
                                         ExecutionMode mode,
                                         const ValueLimits &limits)
    -> NativeReplyDisposition;

// Fixed-size, allocation-free evidence checkpoint, separate from T and its
// codec. Capture possible submission BEFORE entering a possibly-writing await;
// observe the bounded native disposition BEFORE projecting payloads. Neither
// later exceptions nor secondary observations can erase accepted/completed
// evidence. Its fixed private scalar bookkeeping has the same accounting
// exception as NativeReplyDisposition. It does not yet retain per-effect
// subjects or message references.
class ExecutionCheckpoint {
public:
  ExecutionCheckpoint() = default;
  void possibly_submitted() noexcept;
  void observe(const NativeReplyDisposition &disposition) noexcept;
  [[nodiscard]] auto evidence() const noexcept -> ExecutionEvidence;
  [[nodiscard]] auto failure(FailureStage stage) const -> ExecutionFailure;

private:
  ExecutionEvidence evidence_{Submission::NotSubmitted,
                              NativeCompletion::NotObserved, std::nullopt};
};

// Process-only notification disposition; never infer completion from an outer
// BotOperationResult::ok(). Projection failure remains Failed, with independent
// native completion retained in ExecutionEvidence.
enum class CompletionDisposition : std::uint8_t {
  Completed,
  AcceptedPending,
  Failed
};

template <typename T>
[[nodiscard]] auto completion_disposition(const ExecutionOutcome<T> &outcome)
    -> CompletionDisposition {
  outcome.validate();
  if (std::holds_alternative<Completed<T>>(outcome.state))
    return CompletionDisposition::Completed;
  if (std::holds_alternative<AcceptedPending>(outcome.state))
    return CompletionDisposition::AcceptedPending;
  return CompletionDisposition::Failed;
}

} // namespace obcx::onebot11::bot::standard::detail
