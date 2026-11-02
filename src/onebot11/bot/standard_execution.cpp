#include "standard_execution_codec.hpp"

#include <cmath>
#include <limits>

namespace obcx::onebot11::bot::standard {
namespace {
void validate_mode(ExecutionMode mode) {
  switch (mode) {
  case ExecutionMode::Direct:
  case ExecutionMode::Async:
  case ExecutionMode::RateLimited:
    return;
  }
  throw std::invalid_argument("invalid standard execution mode");
}
} // namespace

void ExecutionEvidence::validate() const {
  switch (submission) {
  case Submission::NotSubmitted:
  case Submission::PossiblySubmitted:
  case Submission::Accepted:
    break;
  default:
    throw std::invalid_argument("invalid standard submission evidence");
  }
  switch (completion) {
  case NativeCompletion::NotObserved:
    if (submission == Submission::Accepted || provider_retcode)
      throw std::invalid_argument(
          "unobserved execution contains native evidence");
    return;
  case NativeCompletion::AcceptedPending:
    if (provider_retcode != 1)
      throw std::invalid_argument("native acceptance requires retcode one");
    break;
  case NativeCompletion::ProviderReportedCompleted:
    if (provider_retcode != 0)
      throw std::invalid_argument("native completion requires zero retcode");
    break;
  case NativeCompletion::ProviderReportedFailed:
    if (!provider_retcode || *provider_retcode == 0 || *provider_retcode == 1)
      throw std::invalid_argument(
          "native rejection excludes retcodes zero and one");
    if (submission != Submission::PossiblySubmitted)
      throw std::invalid_argument(
          "native rejection cannot establish no submission");
    return;
  default:
    throw std::invalid_argument("invalid native completion evidence");
  }
  if (submission != Submission::Accepted || !provider_retcode)
    throw std::invalid_argument(
        "native acceptance requires submission evidence");
}

void AcceptedPending::validate() const {
  validate_mode(requested_mode);
  if (provider_retcode != 1)
    throw std::invalid_argument("pending result requires retcode one");
}

void ExecutionFailure::validate(const ExecutionEvidence &evidence) const {
  evidence.validate();
  error.validate();
  if (error.retryable)
    throw std::invalid_argument(
        "standard failure cannot grant automatic retry");
  const auto safety = evidence.submission == Submission::NotSubmitted
                          ? obcx::bot::SubmissionSafety::DefinitelyNotSubmitted
                          : obcx::bot::SubmissionSafety::PossiblySubmitted;
  if (error.submission_safety != safety)
    throw std::invalid_argument("failure contradicts submission evidence");
  switch (stage) {
  case FailureStage::BeforeSubmission:
    if (evidence.submission != Submission::NotSubmitted)
      throw std::invalid_argument(
          "pre-submission failure contains submission evidence");
    break;
  case FailureStage::Transport:
    break;
  case FailureStage::NativeReply:
  case FailureStage::Projection:
    if (evidence.submission == Submission::NotSubmitted)
      throw std::invalid_argument(
          "response failure cannot establish no submission");
    break;
  default:
    throw std::invalid_argument("invalid execution failure stage");
  }
}

namespace detail {
auto native_action_name(NativeAction action) -> std::string_view {
  switch (action) {
  case NativeAction::SendPrivateMsg:
    return "send_private_msg";
  case NativeAction::SendGroupMsg:
    return "send_group_msg";
  case NativeAction::SendMsg:
    return "send_msg";
  case NativeAction::DeleteMsg:
    return "delete_msg";
  case NativeAction::GetMsg:
    return "get_msg";
  case NativeAction::GetForwardMsg:
    return "get_forward_msg";
  case NativeAction::SendLike:
    return "send_like";
  case NativeAction::SetGroupKick:
    return "set_group_kick";
  case NativeAction::SetGroupBan:
    return "set_group_ban";
  case NativeAction::SetGroupAnonymousBan:
    return "set_group_anonymous_ban";
  case NativeAction::SetGroupWholeBan:
    return "set_group_whole_ban";
  case NativeAction::SetGroupAdmin:
    return "set_group_admin";
  case NativeAction::SetGroupAnonymous:
    return "set_group_anonymous";
  case NativeAction::SetGroupCard:
    return "set_group_card";
  case NativeAction::SetGroupName:
    return "set_group_name";
  case NativeAction::SetGroupLeave:
    return "set_group_leave";
  case NativeAction::SetGroupSpecialTitle:
    return "set_group_special_title";
  case NativeAction::SetFriendAddRequest:
    return "set_friend_add_request";
  case NativeAction::SetGroupAddRequest:
    return "set_group_add_request";
  case NativeAction::GetLoginInfo:
    return "get_login_info";
  case NativeAction::GetStrangerInfo:
    return "get_stranger_info";
  case NativeAction::GetFriendList:
    return "get_friend_list";
  case NativeAction::GetGroupInfo:
    return "get_group_info";
  case NativeAction::GetGroupList:
    return "get_group_list";
  case NativeAction::GetGroupMemberInfo:
    return "get_group_member_info";
  case NativeAction::GetGroupMemberList:
    return "get_group_member_list";
  case NativeAction::GetGroupHonorInfo:
    return "get_group_honor_info";
  case NativeAction::GetCookies:
    return "get_cookies";
  case NativeAction::GetCsrfToken:
    return "get_csrf_token";
  case NativeAction::GetCredentials:
    return "get_credentials";
  case NativeAction::GetRecord:
    return "get_record";
  case NativeAction::GetImage:
    return "get_image";
  case NativeAction::CanSendImage:
    return "can_send_image";
  case NativeAction::CanSendRecord:
    return "can_send_record";
  case NativeAction::GetStatus:
    return "get_status";
  case NativeAction::GetVersionInfo:
    return "get_version_info";
  case NativeAction::SetRestart:
    return "set_restart";
  case NativeAction::CleanCache:
    return "clean_cache";
  case NativeAction::HandleQuickOperation:
    return ".handle_quick_operation";
  }
  throw std::invalid_argument("invalid native standard action");
}

auto native_action_name(NativeAction action, ExecutionMode mode)
    -> std::string {
  auto name = std::string{native_action_name(action)};
  validate_mode(mode);
  switch (mode) {
  case ExecutionMode::Direct:
    break;
  case ExecutionMode::Async:
    name += "_async";
    break;
  case ExecutionMode::RateLimited:
    name += "_rate_limited";
    break;
  }
  return name;
}

auto standard_action_id(NativeAction action) -> obcx::bot::ActionId {
  return obcx::bot::ActionId{std::string{"onebot11.standard."} +
                             std::string{native_action_name(action)}};
}

namespace {
class BorrowedReplyBudget {
public:
  explicit BorrowedReplyBudget(const ValueLimits &limits) : limits_(limits) {}

  void inspect(const obcx::bot::Json &value, std::size_t depth) {
    if (nodes_ == limits_.maximum_nodes || depth > limits_.maximum_depth)
      throw std::invalid_argument("standard reply exceeds value limits");
    ++nodes_;
    if (value.is_string()) {
      text(value.get_ref<const std::string &>());
    } else if (value.is_binary()) {
      bytes(value.get_binary().size());
    } else if (value.is_structured()) {
      for (auto it = value.begin(); it != value.end(); ++it) {
        if (value.is_object())
          text(it.key());
        inspect(it.value(), depth + 1);
      }
    } else if (value.is_discarded() || (value.is_number_float() &&
                                        !std::isfinite(value.get<double>()))) {
      throw std::invalid_argument(
          "standard reply contains invalid native value");
    }
  }

private:
  void bytes(std::size_t count) {
    if (count > limits_.maximum_bytes - bytes_)
      throw std::invalid_argument("standard reply exceeds byte limit");
    bytes_ += count;
  }
  void text(std::string_view value) {
    if (value.size() > limits_.maximum_text_bytes)
      throw std::invalid_argument("standard reply exceeds text limit");
    bytes(value.size());
    obcx::bot::detail::validate_utf8(value);
  }

  const ValueLimits &limits_;
  std::size_t nodes_{};
  std::size_t bytes_{};
};
} // namespace

auto classify_native_reply(const obcx::bot::Json &reply, NativeAction action,
                           ExecutionMode mode, const ValueLimits &limits)
    -> NativeReplyDisposition {
  (void)native_action_name(action);
  validate_mode(mode);
  limits.validate();
  const auto malformed =
      NativeReplyDisposition{NativeReplyKind::Malformed, std::nullopt};
  try {
    // Validated caller limits are no larger than the existing hard bounds.
    BorrowedReplyBudget{limits}.inspect(reply, 0);
    if (!reply.is_object())
      return malformed;
    const auto status = reply.find("status");
    const auto retcode = reply.find("retcode");
    if (status == reply.end() || !status->is_string() ||
        retcode == reply.end() || !retcode->is_number_integer())
      return malformed;
    if (retcode->is_number_unsigned() &&
        retcode->get<std::uint64_t>() >
            static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max()))
      return malformed;
    const auto code = retcode->get<std::int64_t>();
    const auto &status_text = status->get_ref<const std::string &>();
    if (status_text == "async" && code == 1)
      return {NativeReplyKind::AcceptedPending, code};
    if (status_text == "ok" && code == 0 && mode == ExecutionMode::Direct &&
        action != NativeAction::SetRestart)
      return {NativeReplyKind::Completed, code};
    if (status_text == "failed" && code != 0 && code != 1)
      return {NativeReplyKind::Rejected, code};
    return malformed;
  } catch (...) {
    // Never expose exception snippets or native keys/wording. A response that
    // cannot be bounded/inspected says nothing about definite non-submission.
    return malformed;
  }
}

void ExecutionCheckpoint::possibly_submitted() noexcept {
  if (evidence_.submission == Submission::NotSubmitted)
    evidence_.submission = Submission::PossiblySubmitted;
}

void ExecutionCheckpoint::observe(
    const NativeReplyDisposition &disposition) noexcept {
  possibly_submitted();
  // A later failure/projection cannot overwrite an already observed
  // disposition.
  if (evidence_.completion != NativeCompletion::NotObserved)
    return;
  if (!disposition.provider_retcode)
    return;
  switch (disposition.kind) {
  case NativeReplyKind::Completed:
    if (*disposition.provider_retcode != 0)
      return;
    evidence_.submission = Submission::Accepted;
    evidence_.completion = NativeCompletion::ProviderReportedCompleted;
    break;
  case NativeReplyKind::AcceptedPending:
    if (*disposition.provider_retcode != 1)
      return;
    evidence_.submission = Submission::Accepted;
    evidence_.completion = NativeCompletion::AcceptedPending;
    break;
  case NativeReplyKind::Rejected:
    if (*disposition.provider_retcode == 0 ||
        *disposition.provider_retcode == 1)
      return;
    evidence_.completion = NativeCompletion::ProviderReportedFailed;
    break;
  case NativeReplyKind::Malformed:
    return;
  default:
    return;
  }
  evidence_.provider_retcode = disposition.provider_retcode;
}

auto ExecutionCheckpoint::evidence() const noexcept -> ExecutionEvidence {
  return evidence_;
}

auto ExecutionCheckpoint::failure(FailureStage stage) const
    -> ExecutionFailure {
  using Code = obcx::bot::BotOperationErrorCode;
  Code code;
  const char *message;
  switch (stage) {
  case FailureStage::BeforeSubmission:
    code = Code::InvalidRequest;
    message = "standard request failed before submission";
    break;
  case FailureStage::Transport:
    code = evidence_.submission == Submission::NotSubmitted
               ? Code::TransportFailure
               : Code::OutcomeUnknown;
    message = "standard transport did not produce a usable reply";
    break;
  case FailureStage::NativeReply:
    code = evidence_.completion == NativeCompletion::ProviderReportedFailed
               ? Code::ProviderRejected
               : Code::MalformedResponse;
    message = "standard native reply did not establish a valid result";
    break;
  case FailureStage::Projection:
    code = Code::MalformedResponse;
    message = "standard result projection failed";
    break;
  default:
    throw std::invalid_argument("invalid execution failure stage");
  }
  ExecutionFailure result{
      {.code = code,
       .message = message,
       .retryable = false,
       .submission_safety =
           evidence_.submission == Submission::NotSubmitted
               ? obcx::bot::SubmissionSafety::DefinitelyNotSubmitted
               : obcx::bot::SubmissionSafety::PossiblySubmitted},
      stage};
  result.validate(evidence_);
  return result;
}

} // namespace detail
} // namespace obcx::onebot11::bot::standard
