#pragma once

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/experimental/concurrent_channel.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace obcx::network::detail {

namespace asio = boost::asio;

class ActionDeadline {
public:
  virtual ~ActionDeadline() = default;
  virtual void cancel() = 0;
};

using ActionDeadlineFactory = std::function<std::shared_ptr<ActionDeadline>(
    asio::any_io_executor, std::chrono::milliseconds, std::function<void()>)>;

enum class ActionTerminalStatus : std::uint8_t {
  Response,
  Timeout,
  TransportFailure,
  Cancelled,
};

struct ActionTerminalOutcome {
  ActionTerminalStatus status{ActionTerminalStatus::Cancelled};
  std::string response;
  std::exception_ptr failure;
};

class PendingAction final {
public:
  auto wait() -> asio::awaitable<ActionTerminalOutcome>;

private:
  friend class ActionRequestTracker;
  using CompletionChannel = asio::experimental::concurrent_channel<void(
      boost::system::error_code, ActionTerminalOutcome)>;

  explicit PendingAction(asio::any_io_executor executor)
      : completion_(std::move(executor), 1) {}

  void finish(ActionTerminalOutcome outcome);

  CompletionChannel completion_;
  std::shared_ptr<ActionDeadline> deadline_;
};

class ActionRequestTracker final
    : public std::enable_shared_from_this<ActionRequestTracker> {
public:
  explicit ActionRequestTracker(asio::any_io_executor executor,
                                ActionDeadlineFactory deadline_factory = {});
  ~ActionRequestTracker();

  ActionRequestTracker(const ActionRequestTracker &) = delete;
  auto operator=(const ActionRequestTracker &)
      -> ActionRequestTracker & = delete;

  auto start(std::uint64_t echo, std::chrono::milliseconds timeout)
      -> std::shared_ptr<PendingAction>;
  auto respond(std::uint64_t echo, std::string response) -> bool;
  auto fail(std::uint64_t echo, std::exception_ptr failure) -> bool;
  auto cancel(std::uint64_t echo) -> bool;
  void fail_all(std::exception_ptr failure);
  void cancel_all();

  [[nodiscard]] auto pending_count() const -> std::size_t;
  [[nodiscard]] auto pending_echoes() const -> std::vector<std::uint64_t>;

private:
  auto finish(std::uint64_t echo, ActionTerminalOutcome outcome) -> bool;

  asio::any_io_executor executor_;
  ActionDeadlineFactory deadline_factory_;
  mutable std::mutex mutex_;
  std::unordered_map<std::uint64_t, std::shared_ptr<PendingAction>> pending_;
};

[[nodiscard]] auto default_action_deadline_factory() -> ActionDeadlineFactory;

} // namespace obcx::network::detail
