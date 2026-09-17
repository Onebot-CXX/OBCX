#pragma once

#include <atomic>
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
  // just until cancellation is requested. Idle timer waits need no work lease.
  [[nodiscard]] auto acquire_work(const TokenPtr &token) -> WorkLease {
    std::scoped_lock lock(state_mutex_);
    if (!token || token != token_ || !token->valid()) {
      return {};
    }
    return acquire_work_();
  }

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
