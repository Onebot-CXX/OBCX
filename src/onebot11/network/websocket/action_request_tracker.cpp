#include "onebot11/network/websocket/detail/action_request_tracker.hpp"
#include "standard_ws_wire.hpp"

#include <boost/asio/error.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/system_error.hpp>
#include <stdexcept>
#include <utility>

namespace obcx::network::detail {
namespace {
class AsioActionDeadline final
    : public ActionDeadline,
      public std::enable_shared_from_this<AsioActionDeadline> {
public:
  AsioActionDeadline(asio::any_io_executor executor,
                     std::chrono::milliseconds timeout)
      : strand_(asio::make_strand(executor)), timer_(strand_, timeout) {}
  void start(std::function<void()> expired) {
    asio::post(strand_, [self = shared_from_this(),
                         expired = std::move(expired)] {
      if (self->cancelled_)
        return;
      self->timer_.async_wait([self, expired](boost::system::error_code error) {
        if (!error && !self->cancelled_)
          expired();
      });
    });
  }
  void cancel() override {
    asio::post(strand_, [self = shared_from_this()] {
      self->cancelled_ = true;
      self->timer_.cancel();
    });
  }

private:
  asio::strand<asio::any_io_executor> strand_;
  asio::steady_timer timer_;
  bool cancelled_{};
};
auto cancelled_outcome() -> ActionTerminalOutcome {
  return {.status = ActionTerminalStatus::Cancelled,
          .failure = std::make_exception_ptr(
              boost::system::system_error{asio::error::operation_aborted})};
}
} // namespace

auto PendingAction::wait() -> asio::awaitable<ActionTerminalOutcome> {
  co_return co_await completion_.async_receive(asio::use_awaitable);
}
void PendingAction::publish_deadline(std::shared_ptr<ActionDeadline> deadline) {
  {
    std::lock_guard lock(mutex_);
    if (!finished_) {
      deadline_ = std::move(deadline);
      return;
    }
  }
  // Injected cancel/factory callbacks may synchronously reenter the tracker.
  deadline->cancel();
}
void PendingAction::finish(ActionTerminalOutcome outcome) {
  std::shared_ptr<ActionDeadline> deadline;
  {
    std::lock_guard lock(mutex_);
    if (finished_)
      return;
    finished_ = true;
    deadline = std::move(deadline_);
  }
  (void)completion_.try_send(boost::system::error_code{}, std::move(outcome));
  if (deadline)
    deadline->cancel();
}
auto default_action_deadline_factory() -> ActionDeadlineFactory {
  return [](asio::any_io_executor executor, std::chrono::milliseconds timeout,
            std::function<void()> expired) -> std::shared_ptr<ActionDeadline> {
    auto deadline =
        std::make_shared<AsioActionDeadline>(std::move(executor), timeout);
    deadline->start(std::move(expired));
    return deadline;
  };
}
ActionRequestTracker::ActionRequestTracker(asio::any_io_executor executor,
                                           ActionDeadlineFactory factory)
    : executor_(std::move(executor)),
      timer_strand_(asio::make_strand(executor_)),
      deadline_factory_(factory ? std::move(factory)
                                : default_action_deadline_factory()) {}
ActionRequestTracker::~ActionRequestTracker() { cancel_all(); }

auto ActionRequestTracker::start(std::uint64_t echo,
                                 std::chrono::milliseconds timeout)
    -> std::shared_ptr<PendingAction> {
  if (timeout <= std::chrono::milliseconds::zero())
    throw std::invalid_argument("OneBot action timeout must be positive");
  auto request = std::shared_ptr<PendingAction>(new PendingAction{executor_});
  {
    std::lock_guard lock(mutex_);
    if (!pending_.emplace(echo, request).second)
      throw std::invalid_argument("duplicate pending OneBot action echo");
  }
  try {
    auto deadline = deadline_factory_(
        executor_, timeout,
        [weak = weak_from_this(), handle = std::weak_ptr{request}, echo] {
          if (auto tracker = weak.lock())
            if (auto current = handle.lock())
              (void)tracker->finish(echo, current,
                                    {.status = ActionTerminalStatus::Timeout});
        });
    if (!deadline)
      throw std::runtime_error("OneBot action deadline factory returned null");
    request->publish_deadline(std::move(deadline));
  } catch (...) {
    (void)fail(echo, request, std::current_exception());
    throw;
  }
  return request;
}
StandardPendingAction::StandardPendingAction(
    asio::any_io_executor executor, std::uint64_t key, std::uint64_t read_epoch,
    std::shared_ptr<WebsocketClient> original_client,
    std::shared_ptr<sd::StandardCallState> state)
    : echo(key), epoch(read_epoch), client(std::move(original_client)),
      call(std::move(state)), timer_(executor), deadline_joined_(executor, 1),
      completion_(executor, 1) {
  timer_.expires_at(call->deadline());
}

auto StandardPendingAction::wait_terminal() -> asio::awaitable<void> {
  while (!call->terminal()) {
    try {
      (void)co_await completion_.async_receive(asio::use_awaitable);
    } catch (...) {
      if (!call->terminal())
        throw;
    }
  }
}
auto StandardPendingAction::join_deadline() -> asio::awaitable<void> {
  (void)co_await deadline_joined_.async_receive(asio::use_awaitable);
}
void StandardPendingAction::acknowledge_deadline() {
  // Only the retirement task and the wait handler can reach this. The last
  // one sends the acknowledgment as its final protocol action.
  if (retirement_applied_ && !wait_pending_)
    (void)deadline_joined_.try_send(boost::system::error_code{}, true);
}
void ActionRequestTracker::start_standard(
    std::uint64_t echo, std::uint64_t epoch,
    std::shared_ptr<WebsocketClient> client,
    std::shared_ptr<sd::StandardCallState> call,
    std::shared_ptr<StandardPendingAction> &published) {
  if (!call)
    throw std::invalid_argument("standard WS call state is required");
  auto request =
      std::shared_ptr<StandardPendingAction>(new StandardPendingAction{
          timer_strand_, echo, epoch, std::move(client), std::move(call)});
  std::exception_ptr failure;
  {
    std::lock_guard publication_lock(request->deadline_publication_mutex_);
    {
      std::lock_guard lock(mutex_);
      if (!pending_.emplace(echo, request).second)
        throw std::invalid_argument("duplicate pending OneBot action echo");
    }
    published = request;
    // Publication precedes arm. Retirement may terminalize/erase/notify now,
    // but its post cannot pass this gate until arm is posted or has failed.
    try {
      asio::post(timer_strand_, [weak = weak_from_this(), request] {
        auto tracker = weak.lock();
        if (!tracker)
          return;
        if (request->call->terminal()) {
          tracker->retire_standard(request);
          return;
        }
        try {
          request->timer_.async_wait(
              [weak, request](boost::system::error_code error) {
                if (!error)
                  if (auto owner = weak.lock())
                    owner->fail_standard(
                        request,
                        {sd::StandardTransportFailureKind::DeadlineExceeded,
                         asio::error::timed_out});
                // Includes aborted completions and expiry queued before cancel.
                request->wait_pending_ = false;
                request->acknowledge_deadline();
              });
          request->wait_pending_ = true;
        } catch (...) {
          tracker->fail_standard(
              request, {sd::StandardTransportFailureKind::TransportError,
                        asio::error::fault});
        }
      });
    } catch (...) {
      failure = std::current_exception();
    }
  }
  if (failure) {
    fail_standard(request, {sd::StandardTransportFailureKind::TransportError,
                            asio::error::fault});
    std::rethrow_exception(failure);
  }
  retire_standard(request);
}
void ActionRequestTracker::retire_standard(
    const std::shared_ptr<StandardPendingAction> &request) {
  if (!request->call->terminal())
    return;
  {
    std::lock_guard lock(mutex_);
    const auto found = pending_.find(request->echo);
    if (found != pending_.end()) {
      const auto current =
          std::get_if<std::shared_ptr<StandardPendingAction>>(&found->second);
      if (current && *current == request)
        pending_.erase(found);
    }
  }
  // Notifications may coalesce. The full result is retained independently.
  (void)request->completion_.try_send(boost::system::error_code{}, true);
  std::lock_guard publication_lock(request->deadline_publication_mutex_);
  if (request->retirement_posted_)
    return;
  request->retirement_posted_ = true;
  try {
    asio::post(timer_strand_, [request] {
      // The publication gate orders this after initial arm work (or its
      // failure). No later arm or second retirement task can be published.
      if (request->wait_pending_)
        request->timer_.cancel();
      request->retirement_applied_ = true;
      request->acknowledge_deadline();
    });
  } catch (...) {
    request->retirement_posted_ = false;
    throw;
  }
}
void ActionRequestTracker::fail_standard(
    const std::shared_ptr<StandardPendingAction> &request,
    sd::StandardTransportFailure failure) {
  (void)request->call->try_fail(failure);
  retire_standard(request);
}
auto ActionRequestTracker::receive_standard(std::uint64_t epoch,
                                            std::string_view frame) -> bool {
  const auto candidate = locate_standard_ws_echo(frame);
  if (!candidate.standard_namespace_seen)
    return false;
  if (!candidate.echo)
    return true;
  std::shared_ptr<StandardPendingAction> request;
  {
    std::lock_guard lock(mutex_);
    const auto found = pending_.find(*candidate.echo);
    if (found == pending_.end())
      return true;
    const auto current =
        std::get_if<std::shared_ptr<StandardPendingAction>>(&found->second);
    if (!current || (*current)->epoch != epoch)
      return true;
    request = *current;
  }
  (void)request->call->try_complete_reply(frame, std::nullopt);
  retire_standard(
      request); // Rejected precommit receipts do not erase pending work.
  return true;
}
auto ActionRequestTracker::respond(std::uint64_t echo, std::string response)
    -> bool {
  return finish(echo, {.status = ActionTerminalStatus::Response,
                       .response = std::move(response)});
}
auto ActionRequestTracker::fail(std::uint64_t echo, std::exception_ptr failure)
    -> bool {
  if (!failure)
    failure = std::make_exception_ptr(
        std::runtime_error{"OneBot action transport failed"});
  return finish(echo, {.status = ActionTerminalStatus::TransportFailure,
                       .failure = std::move(failure)});
}
auto ActionRequestTracker::fail(std::uint64_t echo,
                                const std::shared_ptr<PendingAction> &request,
                                std::exception_ptr failure) -> bool {
  if (!failure)
    failure = std::make_exception_ptr(
        std::runtime_error{"OneBot action transport failed"});
  return finish(echo, request,
                {.status = ActionTerminalStatus::TransportFailure,
                 .failure = std::move(failure)});
}
auto ActionRequestTracker::cancel(std::uint64_t echo) -> bool {
  return finish(echo, cancelled_outcome());
}
auto ActionRequestTracker::snapshot() const
    -> std::vector<std::pair<std::uint64_t, Entry>> {
  std::lock_guard lock(mutex_);
  return {pending_.begin(), pending_.end()};
}
void ActionRequestTracker::fail_all(std::exception_ptr failure) {
  boost::system::error_code error;
  if (failure) {
    try {
      std::rethrow_exception(failure);
    } catch (const boost::system::system_error &exception) {
      error = exception.code();
    } catch (...) {
    }
  }
  for (const auto &[echo, entry] : snapshot()) {
    if (const auto legacy = std::get_if<std::shared_ptr<PendingAction>>(&entry))
      (void)fail(echo, *legacy, failure);
    else
      fail_standard(std::get<std::shared_ptr<StandardPendingAction>>(entry),
                    {sd::StandardTransportFailureKind::TransportError, error});
  }
}
void ActionRequestTracker::cancel_all() {
  for (const auto &[echo, entry] : snapshot()) {
    if (const auto legacy = std::get_if<std::shared_ptr<PendingAction>>(&entry))
      (void)finish(echo, *legacy, cancelled_outcome());
    else
      fail_standard(std::get<std::shared_ptr<StandardPendingAction>>(entry),
                    {sd::StandardTransportFailureKind::ConnectionRetired,
                     asio::error::operation_aborted});
  }
}
auto ActionRequestTracker::pending_count() const -> std::size_t {
  std::lock_guard lock(mutex_);
  return pending_.size();
}
auto ActionRequestTracker::pending_echoes() const
    -> std::vector<std::uint64_t> {
  std::vector<std::uint64_t> result;
  std::lock_guard lock(mutex_);
  result.reserve(pending_.size());
  for (const auto &[echo, entry] : pending_)
    result.push_back(echo);
  return result;
}
auto ActionRequestTracker::finish(std::uint64_t echo,
                                  ActionTerminalOutcome outcome) -> bool {
  std::shared_ptr<PendingAction> request;
  {
    std::lock_guard lock(mutex_);
    const auto found = pending_.find(echo);
    if (found == pending_.end())
      return false;
    const auto current =
        std::get_if<std::shared_ptr<PendingAction>>(&found->second);
    if (!current)
      return false;
    request = *current;
  }
  return finish(echo, request, std::move(outcome));
}
auto ActionRequestTracker::finish(std::uint64_t echo,
                                  const std::shared_ptr<PendingAction> &request,
                                  ActionTerminalOutcome outcome) -> bool {
  {
    std::lock_guard lock(mutex_);
    const auto found = pending_.find(echo);
    if (found == pending_.end())
      return false;
    const auto current =
        std::get_if<std::shared_ptr<PendingAction>>(&found->second);
    if (!current || *current != request)
      return false;
    pending_.erase(found);
  }
  request->finish(std::move(outcome));
  return true;
}
} // namespace obcx::network::detail
