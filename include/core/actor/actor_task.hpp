#ifndef OBCX_INCLUDE_CORE_ACTOR_TASK_HPP_
#define OBCX_INCLUDE_CORE_ACTOR_TASK_HPP_

#include <atomic>
#include <concepts>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace obcx::core {

inline constexpr size_t ACTOR_TASK_NO_PREFERRED_WORKER =
    std::numeric_limits<size_t>::max();

class ActorTaskCancelled final : public std::exception {
public:
  [[nodiscard]] auto what() const noexcept -> const char * override {
    return "actor task cancelled";
  }
};

class ActorCancellationRegistration {
public:
  ActorCancellationRegistration(const ActorCancellationRegistration &) = delete;
  auto operator=(const ActorCancellationRegistration &)
      -> ActorCancellationRegistration & = delete;

  ~ActorCancellationRegistration() { deactivate(); }

  void deactivate() noexcept {
    try {
      std::scoped_lock lock(mutex_);
      callback_ = {};
    } catch (...) {
    }
  }

private:
  friend class ActorCancellationState;

  explicit ActorCancellationRegistration(std::function<void()> callback)
      : callback_(std::move(callback)) {}

  void notify() noexcept {
    try {
      std::scoped_lock lock(mutex_);
      if (callback_) {
        callback_();
      }
    } catch (...) {
    }
  }

  std::mutex mutex_;
  std::function<void()> callback_;
};

class ActorCancellationState {
public:
  void request_cancellation() noexcept {
    if (requested_.exchange(true, std::memory_order_acq_rel)) {
      return;
    }
    try {
      std::vector<std::weak_ptr<ActorCancellationRegistration>> callbacks;
      {
        std::scoped_lock lock(mutex_);
        callbacks.swap(callbacks_);
      }
      for (auto &callback : callbacks) {
        if (auto registered = callback.lock()) {
          registered->notify();
        }
      }
    } catch (...) {
    }
  }

  [[nodiscard]] auto cancellation_requested() const noexcept -> bool {
    return requested_.load(std::memory_order_acquire);
  }

  auto register_callback(std::function<void()> callback)
      -> std::shared_ptr<ActorCancellationRegistration> {
    auto registration = std::shared_ptr<ActorCancellationRegistration>(
        new ActorCancellationRegistration(std::move(callback)));
    bool notify_now = false;
    {
      std::scoped_lock lock(mutex_);
      if (requested_.load(std::memory_order_acquire)) {
        notify_now = true;
      } else {
        callbacks_.push_back(registration);
      }
    }
    if (notify_now) {
      registration->notify();
    }
    return registration;
  }

private:
  std::atomic_bool requested_ = false;
  std::mutex mutex_;
  std::vector<std::weak_ptr<ActorCancellationRegistration>> callbacks_;
};

enum class ActorTaskSuspension : uint8_t {
  Initial,
  Running,
  Yielded,
  AwaitingIo,
  Completed,
};

struct ActorTaskRuntimeContext {
  void *scheduler = nullptr;
  uint64_t task_id = 0;
  uint64_t mailbox_generation = 0;
  size_t preferred_worker = ACTOR_TASK_NO_PREFERRED_WORKER;
  std::shared_ptr<ActorCancellationState> cancellation;
  std::shared_ptr<std::atomic_uint64_t> io_epoch_source;
  std::function<void()> completion_target;
  std::function<void(uint64_t suspension_epoch)> make_runnable;
};

class ActorTaskPromiseBase {
public:
  ActorTaskPromiseBase() = default;
  ActorTaskPromiseBase(const ActorTaskPromiseBase &) = delete;
  auto operator=(const ActorTaskPromiseBase &)
      -> ActorTaskPromiseBase & = delete;

  [[nodiscard]] auto initial_suspend() const noexcept -> std::suspend_always {
    return {};
  }

  struct FinalAwaiter {
    [[nodiscard]] auto await_ready() const noexcept -> bool { return false; }

    template <typename Promise>
    void await_suspend(std::coroutine_handle<Promise> handle) const noexcept {
      auto &promise = static_cast<ActorTaskPromiseBase &>(handle.promise());
      promise.suspension_.store(ActorTaskSuspension::Completed,
                                std::memory_order_release);
      if (promise.runtime_.completion_target) {
        try {
          promise.runtime_.completion_target();
        } catch (...) {
          // Completion notification is a runtime boundary and must not escape
          // final_suspend. The scheduler still observes handle.done().
        }
      }
    }

    void await_resume() const noexcept {}
  };

  [[nodiscard]] auto final_suspend() const noexcept -> FinalAwaiter {
    return {};
  }

  void unhandled_exception() noexcept { exception_ = std::current_exception(); }

  void attach_runtime(ActorTaskRuntimeContext runtime) {
    runtime_ = std::move(runtime);
    if (!runtime_.cancellation) {
      runtime_.cancellation = std::make_shared<ActorCancellationState>();
    }
    if (!runtime_.io_epoch_source) {
      runtime_.io_epoch_source = std::make_shared<std::atomic_uint64_t>(0);
    }
  }

  [[nodiscard]] auto runtime() noexcept -> ActorTaskRuntimeContext & {
    return runtime_;
  }

  [[nodiscard]] auto runtime() const noexcept
      -> const ActorTaskRuntimeContext & {
    return runtime_;
  }

  [[nodiscard]] auto suspension() const noexcept -> ActorTaskSuspension {
    return suspension_.load(std::memory_order_acquire);
  }

  void set_suspension(const ActorTaskSuspension suspension) noexcept {
    suspension_.store(suspension, std::memory_order_release);
  }

  auto begin_io_suspension() noexcept -> uint64_t {
    const auto epoch =
        runtime_.io_epoch_source
            ? runtime_.io_epoch_source->fetch_add(1,
                                                  std::memory_order_acq_rel) +
                  1
            : io_suspension_epoch_.load(std::memory_order_relaxed) + 1;
    io_suspension_epoch_.store(epoch, std::memory_order_release);
    set_suspension(ActorTaskSuspension::AwaitingIo);
    return epoch;
  }

  void adopt_io_suspension(const uint64_t epoch) noexcept {
    io_suspension_epoch_.store(epoch, std::memory_order_release);
    set_suspension(ActorTaskSuspension::AwaitingIo);
  }

  [[nodiscard]] auto io_suspension_epoch() const noexcept -> uint64_t {
    return io_suspension_epoch_.load(std::memory_order_acquire);
  }

  void request_cancellation() noexcept {
    ensure_cancellation_state()->request_cancellation();
  }

  [[nodiscard]] auto cancellation_requested() const noexcept -> bool {
    return runtime_.cancellation &&
           runtime_.cancellation->cancellation_requested();
  }

  void rethrow_if_failed() const {
    if (exception_) {
      std::rethrow_exception(exception_);
    }
  }

protected:
  auto ensure_cancellation_state() noexcept
      -> std::shared_ptr<ActorCancellationState> {
    if (!runtime_.cancellation) {
      runtime_.cancellation = std::make_shared<ActorCancellationState>();
    }
    return runtime_.cancellation;
  }

private:
  ActorTaskRuntimeContext runtime_;
  std::exception_ptr exception_;
  std::atomic<ActorTaskSuspension> suspension_ = ActorTaskSuspension::Initial;
  std::atomic_uint64_t io_suspension_epoch_ = 0;
};

class ActorYieldAwaiter {
public:
  explicit ActorYieldAwaiter(
      std::shared_ptr<ActorCancellationState> cancellation = nullptr)
      : cancellation_(std::move(cancellation)) {}

  [[nodiscard]] auto await_ready() const noexcept -> bool {
    return cancellation_ && cancellation_->cancellation_requested();
  }

  template <typename Promise>
  void await_suspend(std::coroutine_handle<Promise> handle) const noexcept {
    static_assert(std::is_base_of_v<ActorTaskPromiseBase, Promise>);
    auto &promise = static_cast<ActorTaskPromiseBase &>(handle.promise());
    promise.set_suspension(ActorTaskSuspension::Yielded);
  }

  void await_resume() const {
    if (cancellation_ && cancellation_->cancellation_requested()) {
      throw ActorTaskCancelled{};
    }
  }

private:
  std::shared_ptr<ActorCancellationState> cancellation_;
};

class ActorTaskRuntimeAwaiter {
public:
  [[nodiscard]] auto await_ready() const noexcept -> bool { return false; }

  template <typename Promise>
  auto await_suspend(std::coroutine_handle<Promise> handle) noexcept -> bool {
    static_assert(std::is_base_of_v<ActorTaskPromiseBase, Promise>);
    runtime_ = &static_cast<ActorTaskPromiseBase &>(handle.promise()).runtime();
    return false;
  }

  [[nodiscard]] auto await_resume() const -> ActorTaskRuntimeContext {
    if (runtime_ == nullptr) {
      throw std::logic_error("actor task runtime is unavailable");
    }
    return *runtime_;
  }

private:
  ActorTaskRuntimeContext *runtime_ = nullptr;
};

[[nodiscard]] inline auto current_actor_task_runtime()
    -> ActorTaskRuntimeAwaiter {
  return {};
}

class ActorForwardSuspensionAwaiter {
public:
  ActorForwardSuspensionAwaiter(const ActorTaskSuspension suspension,
                                const uint64_t io_epoch) noexcept
      : suspension_(suspension), io_epoch_(io_epoch) {}

  [[nodiscard]] auto await_ready() const noexcept -> bool { return false; }

  template <typename Promise>
  void await_suspend(std::coroutine_handle<Promise> handle) const noexcept {
    static_assert(std::is_base_of_v<ActorTaskPromiseBase, Promise>);
    auto &promise = static_cast<ActorTaskPromiseBase &>(handle.promise());
    if (suspension_ == ActorTaskSuspension::AwaitingIo) {
      promise.adopt_io_suspension(io_epoch_);
    } else {
      promise.set_suspension(ActorTaskSuspension::Yielded);
    }
  }

  void await_resume() const noexcept {}

private:
  ActorTaskSuspension suspension_;
  uint64_t io_epoch_;
};

[[nodiscard]] inline auto forward_actor_task_suspension(
    const ActorTaskSuspension suspension, const uint64_t io_epoch = 0)
    -> ActorForwardSuspensionAwaiter {
  return {suspension, io_epoch};
}

template <typename T> class ActorTask;

template <typename T>
class ActorTaskPromise final : public ActorTaskPromiseBase {
public:
  [[nodiscard]] auto get_return_object() noexcept -> ActorTask<T>;

  template <typename Value>
  requires std::convertible_to<Value, T>
  void return_value(Value &&value) {
    value_.emplace(std::forward<Value>(value));
  }

  auto take_result() -> T {
    rethrow_if_failed();
    if (!value_) {
      throw std::logic_error("actor task completed without a result");
    }
    return std::move(*value_);
  }

private:
  std::optional<T> value_;
};

template <> class ActorTaskPromise<void> final : public ActorTaskPromiseBase {
public:
  [[nodiscard]] auto get_return_object() noexcept -> ActorTask<void>;
  void return_void() const noexcept {}

  void take_result() const { rethrow_if_failed(); }
};

template <typename T> class [[nodiscard]] ActorTask {
public:
  using value_type = T;
  using promise_type = ActorTaskPromise<T>;
  using handle_type = std::coroutine_handle<promise_type>;

  ActorTask() noexcept = default;
  explicit ActorTask(handle_type handle) noexcept : handle_(handle) {}

  ActorTask(const ActorTask &) = delete;
  auto operator=(const ActorTask &) -> ActorTask & = delete;

  ActorTask(ActorTask &&other) noexcept
      : handle_(std::exchange(other.handle_, {})) {}

  auto operator=(ActorTask &&other) noexcept -> ActorTask & {
    if (this != &other) {
      reset();
      handle_ = std::exchange(other.handle_, {});
    }
    return *this;
  }

  ~ActorTask() { reset(); }

  [[nodiscard]] auto valid() const noexcept -> bool {
    return static_cast<bool>(handle_);
  }

  [[nodiscard]] auto done() const noexcept -> bool {
    return !handle_ || handle_.done();
  }

  [[nodiscard]] auto suspension() const noexcept -> ActorTaskSuspension {
    return handle_ ? handle_.promise().suspension()
                   : ActorTaskSuspension::Completed;
  }

  void attach_runtime(ActorTaskRuntimeContext runtime) {
    require_handle().promise().attach_runtime(std::move(runtime));
  }

  [[nodiscard]] auto runtime() -> ActorTaskRuntimeContext & {
    return require_handle().promise().runtime();
  }

  [[nodiscard]] auto runtime() const -> const ActorTaskRuntimeContext & {
    return require_handle().promise().runtime();
  }

  void resume() {
    auto handle = require_handle();
    if (handle.done()) {
      throw std::logic_error("cannot resume a completed actor task");
    }
    handle.promise().set_suspension(ActorTaskSuspension::Running);
    handle.resume();
  }

  void request_cancellation() noexcept {
    if (handle_) {
      handle_.promise().request_cancellation();
    }
  }

  [[nodiscard]] auto cancellation_requested() const noexcept -> bool {
    return handle_ && handle_.promise().cancellation_requested();
  }

  [[nodiscard]] auto io_suspension_epoch() const noexcept -> uint64_t {
    return handle_ ? handle_.promise().io_suspension_epoch() : 0;
  }

  auto take_result() -> T
  requires(!std::is_void_v<T>)
  {
    auto handle = require_completed_handle();
    return handle.promise().take_result();
  }

  void take_result()
  requires std::is_void_v<T>
  {
    auto handle = require_completed_handle();
    handle.promise().take_result();
  }

  [[nodiscard]] auto native_handle() const noexcept -> handle_type {
    return handle_;
  }

  [[nodiscard]] auto release() noexcept -> handle_type {
    return std::exchange(handle_, {});
  }

  void reset() noexcept {
    if (handle_) {
      handle_.destroy();
      handle_ = {};
    }
  }

private:
  [[nodiscard]] auto require_handle() const -> handle_type {
    if (!handle_) {
      throw std::logic_error("actor task has no coroutine frame");
    }
    return handle_;
  }

  [[nodiscard]] auto require_completed_handle() const -> handle_type {
    auto handle = require_handle();
    if (!handle.done()) {
      throw std::logic_error("actor task result requested before completion");
    }
    return handle;
  }

  handle_type handle_{};
};

template <typename T>
auto ActorTaskPromise<T>::get_return_object() noexcept -> ActorTask<T> {
  return ActorTask<T>{
      std::coroutine_handle<ActorTaskPromise<T>>::from_promise(*this)};
}

inline auto ActorTaskPromise<void>::get_return_object() noexcept
    -> ActorTask<void> {
  return ActorTask<void>{
      std::coroutine_handle<ActorTaskPromise<void>>::from_promise(*this)};
}

} // namespace obcx::core

#endif // OBCX_INCLUDE_CORE_ACTOR_TASK_HPP_
