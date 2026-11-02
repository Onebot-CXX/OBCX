#include "onebot11/bot/detail/standard_call_state.hpp"

#include "standard_execution_codec.hpp"

#include <mutex>
#include <stdexcept>
#include <utility>

namespace obcx::onebot11::bot::standard::detail {

struct StandardCallState::Impl {
  Impl(CallLimits supplied_limits, StandardDeadline supplied_deadline)
      : limits(supplied_limits), deadline(supplied_deadline) {}

  template <typename Body>
  auto try_complete_reply(Body &&body, std::optional<long> http_status)
      -> bool {
    const std::lock_guard lock{mutex};
    if (is_terminal || !committed)
      return false;
    // Bound wire bytes before taking ownership, including the rvalue path.
    // This is not a parser, heap or aggregate logical-accounting budget.
    if (body.size() > limits.maximum_response_bytes) {
      result.emplace(StandardTransportFailure{
          StandardTransportFailureKind::ResponseTooLarge,
          boost::system::error_code{}});
      is_terminal = true;
      return true;
    }
    result.emplace(
        StandardWireReply{std::string{std::forward<Body>(body)}, http_status});
    reply_won = true;
    is_terminal = true;
    return true;
  }

  auto fail_locked(StandardTransportFailure failure) -> bool {
    switch (failure.kind) {
    case StandardTransportFailureKind::Cancelled:
    case StandardTransportFailureKind::DeadlineExceeded:
    case StandardTransportFailureKind::ConnectionRetired:
    case StandardTransportFailureKind::TransportError:
    case StandardTransportFailureKind::ResponseTooLarge:
    case StandardTransportFailureKind::InvalidResponse:
      break;
    default:
      throw std::invalid_argument("invalid standard transport failure kind");
    }
    result.emplace(std::move(failure));
    is_terminal = true;
    return true;
  }

  const CallLimits limits;
  const StandardDeadline deadline;
  std::mutex mutex;
  ExecutionCheckpoint checkpoint;
  std::optional<StandardWireResult> result;
  // Terminal and reply ownership facts survive consumption of result.
  bool is_terminal{false};
  bool committed{false};
  bool reply_won{false};
};

StandardCallState::StandardCallState(const CallLimits &limits,
                                     StandardDeadline deadline) {
  limits.validate();
  impl_ = std::make_unique<Impl>(limits, deadline);
}

StandardCallState::~StandardCallState() = default;

auto StandardCallState::limits() const noexcept -> CallLimits {
  return impl_->limits;
}

auto StandardCallState::deadline() const noexcept -> StandardDeadline {
  return impl_->deadline;
}

auto StandardCallState::try_commit_write() -> bool {
  const std::lock_guard lock{impl_->mutex};
  if (impl_->is_terminal || impl_->committed ||
      std::chrono::steady_clock::now() >= impl_->deadline)
    return false;
  impl_->checkpoint.possibly_submitted();
  impl_->committed = true;
  return true;
}

auto StandardCallState::try_complete_reply(std::string_view body,
                                           std::optional<long> http_status)
    -> bool {
  return impl_->try_complete_reply(body, http_status);
}

auto StandardCallState::try_complete_reply(std::string &&body,
                                           std::optional<long> http_status)
    -> bool {
  return impl_->try_complete_reply(std::move(body), http_status);
}

auto StandardCallState::try_fail(StandardTransportFailure failure) -> bool {
  const std::lock_guard lock{impl_->mutex};
  if (impl_->is_terminal)
    return false;
  return impl_->fail_locked(std::move(failure));
}

auto StandardCallState::try_fail_before_commit(StandardTransportFailure failure)
    -> bool {
  const std::lock_guard lock{impl_->mutex};
  if (impl_->is_terminal || impl_->committed)
    return false;
  return impl_->fail_locked(std::move(failure));
}

auto StandardCallState::terminal() const -> bool {
  const std::lock_guard lock{impl_->mutex};
  return impl_->is_terminal;
}

auto StandardCallState::take_result() -> std::optional<StandardWireResult> {
  const std::lock_guard lock{impl_->mutex};
  if (!impl_->is_terminal)
    return std::nullopt;
  if (!impl_->result)
    throw std::logic_error("standard call result already consumed");
  auto result = std::move(impl_->result);
  impl_->result.reset();
  return result;
}

void StandardCallState::observe(const NativeReplyDisposition &disposition) {
  const std::lock_guard lock{impl_->mutex};
  if (!impl_->reply_won)
    throw std::logic_error("standard call observation requires a reply winner");
  impl_->checkpoint.observe(disposition);
}

auto StandardCallState::evidence() const -> ExecutionEvidence {
  const std::lock_guard lock{impl_->mutex};
  return impl_->checkpoint.evidence();
}

} // namespace obcx::onebot11::bot::standard::detail
