#pragma once

#include "onebot11/bot/detail/standard_call_state.hpp"
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/experimental/concurrent_channel.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace obcx::network {
class WebsocketClient;
}
namespace obcx::network::detail {
namespace asio = boost::asio;
namespace sd = obcx::onebot11::bot::standard::detail;

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
  void publish_deadline(std::shared_ptr<ActionDeadline> deadline);
  CompletionChannel completion_;
  std::mutex mutex_;
  std::shared_ptr<ActionDeadline> deadline_;
  bool finished_{};
};

// Notification carries no reply bytes; StandardCallState is the only winner.
class StandardPendingAction final {
public:
  auto wait_terminal() -> asio::awaitable<void>;
  // Single coordinator join, separate from terminal-result notification.
  auto join_deadline() -> asio::awaitable<void>;
  const std::uint64_t echo;
  const std::uint64_t epoch;
  const std::shared_ptr<WebsocketClient> client;
  const std::shared_ptr<sd::StandardCallState> call;

private:
  friend class ActionRequestTracker;
  StandardPendingAction(asio::any_io_executor executor, std::uint64_t key,
                        std::uint64_t read_epoch,
                        std::shared_ptr<WebsocketClient> original_client,
                        std::shared_ptr<sd::StandardCallState> state);
  void acknowledge_deadline();
  // Orders the initial arm post before the sole retirement post, even when
  // retirement races map publication. Never held during timer operations.
  std::mutex deadline_publication_mutex_;
  bool retirement_posted_{}; // deadline_publication_mutex_ only
  asio::steady_timer timer_;
  bool wait_pending_{};       // timer executor only
  bool retirement_applied_{}; // timer executor only
  asio::experimental::concurrent_channel<void(boost::system::error_code, bool)>
      deadline_joined_;
  asio::experimental::concurrent_channel<void(boost::system::error_code, bool)>
      completion_;
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
  // Set published before any post-publication operation can throw, so the
  // coordinator can always retire/join even if initiation fails.
  void start_standard(std::uint64_t echo, std::uint64_t epoch,
                      std::shared_ptr<WebsocketClient> client,
                      std::shared_ptr<sd::StandardCallState> call,
                      std::shared_ptr<StandardPendingAction> &published);
  auto receive_standard(std::uint64_t epoch, std::string_view frame) -> bool;
  void retire_standard(const std::shared_ptr<StandardPendingAction> &request);
  void fail_standard(const std::shared_ptr<StandardPendingAction> &request,
                     sd::StandardTransportFailure failure);
  auto fail(std::uint64_t echo, const std::shared_ptr<PendingAction> &request,
            std::exception_ptr failure) -> bool;
  auto respond(std::uint64_t echo, std::string response) -> bool;
  auto fail(std::uint64_t echo, std::exception_ptr failure) -> bool;
  auto cancel(std::uint64_t echo) -> bool;
  void fail_all(std::exception_ptr failure);
  void cancel_all();
  [[nodiscard]] auto pending_count() const -> std::size_t;
  [[nodiscard]] auto pending_echoes() const -> std::vector<std::uint64_t>;

private:
  using Entry = std::variant<std::shared_ptr<PendingAction>,
                             std::shared_ptr<StandardPendingAction>>;
  auto finish(std::uint64_t echo, ActionTerminalOutcome outcome) -> bool;
  auto finish(std::uint64_t echo, const std::shared_ptr<PendingAction> &request,
              ActionTerminalOutcome outcome) -> bool;
  auto snapshot() const -> std::vector<std::pair<std::uint64_t, Entry>>;
  asio::any_io_executor executor_;
  asio::strand<asio::any_io_executor> timer_strand_;
  ActionDeadlineFactory deadline_factory_;
  mutable std::mutex mutex_;
  std::unordered_map<std::uint64_t, Entry> pending_;
};
[[nodiscard]] auto default_action_deadline_factory() -> ActionDeadlineFactory;
} // namespace obcx::network::detail
