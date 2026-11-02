#pragma once

#include <atomic>
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/system/error_code.hpp>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

namespace obcx::core {

// Optional generation service for actor-owned background work. This service
// has no timers or scheduling policy. Actors register during preparation and
// keep scheduling, cancellation, and persistence in their own runtime.
class ActorGenerationLifecycle final {
public:
  class Token final {
  public:
    [[nodiscard]] auto valid() const noexcept -> bool {
      return valid_.load(std::memory_order_acquire);
    }

  private:
    friend class ActorGenerationLifecycle;
    std::atomic_bool valid_{true};
  };

  using TokenPtr = std::shared_ptr<const Token>;
  using WorkLease = std::shared_ptr<void>;
  using AcquireWork = std::function<WorkLease()>;
  // Notifications must not throw. Start arranges actor-owned asynchronous
  // work; stop cancels it. Neither callback may wait for that work to complete
  // or recursively transition this service.
  using Start = std::move_only_function<void(TokenPtr) noexcept>;
  using Stop = std::move_only_function<void() noexcept>;

  explicit ActorGenerationLifecycle(AcquireWork acquire_work)
      : acquire_work_(std::move(acquire_work)) {
    if (!acquire_work_) {
      throw std::invalid_argument(
          "generation lifecycle requires work admission");
    }
  }

  void subscribe(Start start, Stop stop) {
    if (!start || !stop) {
      throw std::invalid_argument(
          "generation lifecycle requires both callbacks");
    }
    std::scoped_lock transition(transition_mutex_);
    if (sealed_) {
      throw std::logic_error("generation lifecycle preparation has ended");
    }
    handlers_.push_back({std::move(start), std::move(stop)});
  }

  // Called only after active-generation publication. A fresh token is also
  // used when resuming the old generation after an aborted reload.
  void activate() {
    std::scoped_lock transition(transition_mutex_);
    TokenPtr token;
    {
      std::scoped_lock lock(state_mutex_);
      if (retired_ || token_) {
        return;
      }
      token_ = std::make_shared<Token>();
      token = token_;
      sealed_ = true;
    }
    for (auto &handler : handlers_) {
      handler.start(token);
    }
  }

  void invalidate() noexcept {
    std::scoped_lock transition(transition_mutex_);
    invalidate_locked();
  }

  // The returned lease participates in the generation's existing route drain.
  // Keep it until ALL callbacks for the admitted operation have retired, not
  // just until cancellation is requested. A timer retaining actor code needs
  // this lease even while idle; cancellation alone does not retire its handler.
  [[nodiscard]] auto acquire_work(const TokenPtr &token) -> WorkLease {
    std::scoped_lock lock(state_mutex_);
    if (!token || token != token_ || !token->valid()) {
      return {};
    }
    return acquire_work_();
  }

  // These non-inline core implementations own the outer Asio handlers. A
  // lease captured only in an actor-compiled handler can expire while its
  // destructor/continuation still executes DSO code. These boundaries destroy
  // actor closures and coroutine frames before releasing the route lease.
  // spawn_owned uses a serial executor and posts terminal completion after
  // the final actor awaitable handler has unwound. Cancellation-slot emission
  // must use the supplied serial executor (as must slot setup/replacement).
  // Use this factory rather than constructing actor-owned executor erasure
  // tables: executor/control-block destruction must remain core-owned too.
  static auto make_serial_executor(boost::asio::any_io_executor executor)
      -> boost::asio::any_io_executor;
  static void spawn_owned(boost::asio::any_io_executor serial_executor,
                          WorkLease lease, boost::asio::awaitable<void> work,
                          std::function<void(std::exception_ptr)> completion,
                          boost::asio::cancellation_slot cancellation);
  static void wait_owned(
      boost::asio::steady_timer &timer, WorkLease lease,
      std::function<void(boost::system::error_code)> completion);
  static void post_owned(boost::asio::any_io_executor executor, WorkLease lease,
                         std::function<void()> completion);

  // Destroy DSO-owned callback closures before unloading actor libraries.
  // Idempotent and terminal: this generation can never reactivate afterward.
  void retire() noexcept {
    std::scoped_lock transition(transition_mutex_);
    invalidate_locked();
    {
      std::scoped_lock lock(state_mutex_);
      retired_ = true;
      sealed_ = true;
    }
    handlers_.clear();
  }

private:
  struct Handler {
    Start start;
    Stop stop;
  };

  void invalidate_locked() noexcept {
    {
      std::scoped_lock lock(state_mutex_);
      if (!token_) {
        return;
      }
      token_->valid_.store(false, std::memory_order_release);
      token_.reset();
    }
    for (auto &handler : handlers_) {
      handler.stop();
    }
  }

  std::mutex transition_mutex_;
  std::mutex state_mutex_;
  AcquireWork acquire_work_;
  std::vector<Handler> handlers_;
  std::shared_ptr<Token> token_;
  bool sealed_ = false;
  bool retired_ = false;
};

} // namespace obcx::core
