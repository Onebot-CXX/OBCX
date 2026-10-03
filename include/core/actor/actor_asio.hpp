#ifndef OBCX_INCLUDE_CORE_ACTOR_ASIO_HPP_
#define OBCX_INCLUDE_CORE_ACTOR_ASIO_HPP_

#include "core/actor/actor_task.hpp"

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/post.hpp>
#include <boost/system/system_error.hpp>

#include <atomic>
#include <concepts>
#include <coroutine>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>

namespace obcx::core {

class ActorAsioCompletionToken {
public:
  explicit ActorAsioCompletionToken(boost::asio::any_io_executor executor)
      : executor_(std::move(executor)) {}

  [[nodiscard]] auto executor() const -> boost::asio::any_io_executor {
    return executor_;
  }

private:
  boost::asio::any_io_executor executor_;
};

namespace detail {

template <typename Awaitable> struct ActorAsioAwaitableTraits;

template <typename T, typename Executor>
struct ActorAsioAwaitableTraits<boost::asio::awaitable<T, Executor>> {
  using value_type = T;
};

enum class ActorAsioOperationPhase : uint8_t {
  Pending,
  Completed,
  Abandoned,
};

template <typename T> class ActorAsioOperationState {
public:
  explicit ActorAsioOperationState(std::shared_ptr<void> actor_lifetime = {})
      : actor_lifetime_(std::move(actor_lifetime)) {}

  void attach(std::function<void(uint64_t)> make_runnable,
              const uint64_t suspension_epoch) {
    make_runnable_ = std::move(make_runnable);
    suspension_epoch_ = suspension_epoch;
  }

  void complete(T value, std::exception_ptr exception = {}) {
    value_.emplace(std::move(value));
    exception_ = std::move(exception);
    publish_completion();
  }

  void complete_exception(std::exception_ptr exception) {
    exception_ = std::move(exception);
    publish_completion();
  }

  void abandon() noexcept {
    auto expected = ActorAsioOperationPhase::Pending;
    phase_.compare_exchange_strong(expected, ActorAsioOperationPhase::Abandoned,
                                   std::memory_order_acq_rel,
                                   std::memory_order_acquire);
  }

  [[nodiscard]] auto cancellation_slot() noexcept {
    return cancellation_signal_.slot();
  }

  void attach_cancellation(
      std::shared_ptr<ActorCancellationRegistration> registration) {
    cancellation_registration_ = std::move(registration);
  }

  void request_cancellation() noexcept {
    try {
      cancellation_signal_.emit(boost::asio::cancellation_type::terminal);
    } catch (...) {
    }
  }

  auto take_result() -> T {
    if (phase_.load(std::memory_order_acquire) !=
        ActorAsioOperationPhase::Completed) {
      throw std::logic_error("Asio actor operation resumed before completion");
    }
    if (exception_) {
      std::rethrow_exception(exception_);
    }
    if (!value_) {
      throw std::logic_error("Asio actor operation completed without a value");
    }
    return std::move(*value_);
  }

private:
  void publish_completion() {
    auto expected = ActorAsioOperationPhase::Pending;
    if (!phase_.compare_exchange_strong(
            expected, ActorAsioOperationPhase::Completed,
            std::memory_order_release, std::memory_order_acquire)) {
      return;
    }
    if (make_runnable_) {
      make_runnable_(suspension_epoch_);
    }
  }

  std::atomic<ActorAsioOperationPhase> phase_ =
      ActorAsioOperationPhase::Pending;
  std::optional<T> value_;
  std::exception_ptr exception_;
  std::function<void(uint64_t)> make_runnable_;
  uint64_t suspension_epoch_ = 0;
  boost::asio::cancellation_signal cancellation_signal_;
  std::shared_ptr<ActorCancellationRegistration> cancellation_registration_;
  std::shared_ptr<void> actor_lifetime_;
};

template <> class ActorAsioOperationState<void> {
public:
  explicit ActorAsioOperationState(std::shared_ptr<void> actor_lifetime = {})
      : actor_lifetime_(std::move(actor_lifetime)) {}

  void attach(std::function<void(uint64_t)> make_runnable,
              const uint64_t suspension_epoch) {
    make_runnable_ = std::move(make_runnable);
    suspension_epoch_ = suspension_epoch;
  }

  void complete(std::exception_ptr exception = {}) {
    exception_ = std::move(exception);
    auto expected = ActorAsioOperationPhase::Pending;
    if (!phase_.compare_exchange_strong(
            expected, ActorAsioOperationPhase::Completed,
            std::memory_order_release, std::memory_order_acquire)) {
      return;
    }
    if (make_runnable_) {
      make_runnable_(suspension_epoch_);
    }
  }

  void abandon() noexcept {
    auto expected = ActorAsioOperationPhase::Pending;
    phase_.compare_exchange_strong(expected, ActorAsioOperationPhase::Abandoned,
                                   std::memory_order_acq_rel,
                                   std::memory_order_acquire);
  }

  [[nodiscard]] auto cancellation_slot() noexcept {
    return cancellation_signal_.slot();
  }

  void attach_cancellation(
      std::shared_ptr<ActorCancellationRegistration> registration) {
    cancellation_registration_ = std::move(registration);
  }

  void request_cancellation() noexcept {
    try {
      cancellation_signal_.emit(boost::asio::cancellation_type::terminal);
    } catch (...) {
    }
  }

  void take_result() const {
    if (phase_.load(std::memory_order_acquire) !=
        ActorAsioOperationPhase::Completed) {
      throw std::logic_error("Asio actor operation resumed before completion");
    }
    if (exception_) {
      std::rethrow_exception(exception_);
    }
  }

private:
  std::atomic<ActorAsioOperationPhase> phase_ =
      ActorAsioOperationPhase::Pending;
  std::exception_ptr exception_;
  std::function<void(uint64_t)> make_runnable_;
  uint64_t suspension_epoch_ = 0;
  boost::asio::cancellation_signal cancellation_signal_;
  std::shared_ptr<ActorCancellationRegistration> cancellation_registration_;
  std::shared_ptr<void> actor_lifetime_;
};

template <typename Awaitable, typename Factory> class ActorAsioAwaiter {
public:
  using awaitable_type = std::remove_cvref_t<Awaitable>;
  using value_type =
      typename ActorAsioAwaitableTraits<awaitable_type>::value_type;

  ActorAsioAwaiter(boost::asio::any_io_executor executor, Factory factory,
                   std::shared_ptr<void> actor_lifetime = {})
      : executor_(std::move(executor)), factory_(std::move(factory)),
        operation_(std::make_shared<ActorAsioOperationState<value_type>>(
            std::move(actor_lifetime))) {}

  ActorAsioAwaiter(const ActorAsioAwaiter &) = delete;
  auto operator=(const ActorAsioAwaiter &) -> ActorAsioAwaiter & = delete;
  ActorAsioAwaiter(ActorAsioAwaiter &&other) noexcept
      : executor_(std::move(other.executor_)),
        factory_(std::move(other.factory_)),
        operation_(std::exchange(other.operation_, {})) {}
  auto operator=(ActorAsioAwaiter &&) -> ActorAsioAwaiter & = delete;

  ~ActorAsioAwaiter() {
    if (operation_) {
      operation_->abandon();
    }
  }

  [[nodiscard]] auto await_ready() const noexcept -> bool { return false; }

  template <typename Promise>
  void await_suspend(std::coroutine_handle<Promise> handle) {
    static_assert(std::is_base_of_v<ActorTaskPromiseBase, Promise>);
    auto &promise = static_cast<ActorTaskPromiseBase &>(handle.promise());
    if (!promise.runtime().make_runnable) {
      throw std::logic_error(
          "Asio actor await requires an attached scheduler runtime");
    }
    const auto epoch = promise.begin_io_suspension();
    operation_->attach(promise.runtime().make_runnable, epoch);

    try {
      // Coroutine lambdas keep a pointer to their closure rather than copying
      // captures into the coroutine frame. Keep that closure alive until the
      // nested Asio coroutine reports completion, even if cancellation destroys
      // the owning ActorTask frame first.
      auto factory = std::make_shared<Factory>(std::move(factory_));
      auto awaitable = std::invoke(*factory);
      if constexpr (std::is_void_v<value_type>) {
        boost::asio::co_spawn(executor_, std::move(awaitable),
                              boost::asio::bind_cancellation_slot(
                                  operation_->cancellation_slot(),
                                  [operation = operation_, factory](
                                      std::exception_ptr exception) mutable {
                                    operation->complete(std::move(exception));
                                  }));
      } else {
        boost::asio::co_spawn(
            executor_, std::move(awaitable),
            boost::asio::bind_cancellation_slot(
                operation_->cancellation_slot(),
                [operation = operation_, factory](std::exception_ptr exception,
                                                  value_type value) mutable {
                  if (exception) {
                    operation->complete_exception(std::move(exception));
                  } else {
                    operation->complete(std::move(value));
                  }
                }));
      }

      if (promise.runtime().cancellation) {
        auto weak_operation =
            std::weak_ptr<ActorAsioOperationState<value_type>>{operation_};
        auto registration = promise.runtime().cancellation->register_callback(
            [weak_operation, executor = executor_]() mutable {
              boost::asio::post(executor, [weak_operation]() mutable {
                if (auto operation = weak_operation.lock()) {
                  operation->request_cancellation();
                }
              });
            });
        operation_->attach_cancellation(std::move(registration));
      }
    } catch (...) {
      if constexpr (std::is_void_v<value_type>) {
        operation_->complete(std::current_exception());
      } else {
        operation_->complete_exception(std::current_exception());
      }
    }
  }

  auto await_resume() -> value_type {
    if constexpr (std::is_void_v<value_type>) {
      operation_->take_result();
      return;
    } else {
      return operation_->take_result();
    }
  }

private:
  boost::asio::any_io_executor executor_;
  Factory factory_;
  std::shared_ptr<ActorAsioOperationState<value_type>> operation_;
};

template <typename Value, typename Initiation, typename... InitArgs>
class ActorAsioDirectAwaiter {
public:
  using value_type = Value;

  ActorAsioDirectAwaiter(boost::asio::any_io_executor executor,
                         Initiation initiation, InitArgs... args)
      : executor_(std::move(executor)), initiation_(std::move(initiation)),
        args_(std::move(args)...),
        operation_(std::make_shared<ActorAsioOperationState<value_type>>()) {}

  ActorAsioDirectAwaiter(const ActorAsioDirectAwaiter &) = delete;
  auto operator=(const ActorAsioDirectAwaiter &)
      -> ActorAsioDirectAwaiter & = delete;
  ActorAsioDirectAwaiter(ActorAsioDirectAwaiter &&other) noexcept
      : executor_(std::move(other.executor_)),
        initiation_(std::move(other.initiation_)),
        args_(std::move(other.args_)),
        operation_(std::exchange(other.operation_, {})) {}
  auto operator=(ActorAsioDirectAwaiter &&)
      -> ActorAsioDirectAwaiter & = delete;

  ~ActorAsioDirectAwaiter() {
    if (operation_) {
      operation_->abandon();
    }
  }

  [[nodiscard]] auto await_ready() const noexcept -> bool { return false; }

  template <typename Promise>
  void await_suspend(std::coroutine_handle<Promise> handle) {
    static_assert(std::is_base_of_v<ActorTaskPromiseBase, Promise>);
    auto &promise = static_cast<ActorTaskPromiseBase &>(handle.promise());
    if (!promise.runtime().make_runnable) {
      throw std::logic_error(
          "Asio actor await requires an attached scheduler runtime");
    }
    const auto epoch = promise.begin_io_suspension();
    operation_->attach(promise.runtime().make_runnable, epoch);

    try {
      if constexpr (std::is_void_v<value_type>) {
        auto handler = boost::asio::bind_cancellation_slot(
            operation_->cancellation_slot(),
            [operation = operation_](boost::system::error_code error) mutable {
              if (error) {
                operation->complete(std::make_exception_ptr(
                    boost::system::system_error(error)));
              } else {
                operation->complete();
              }
            });
        start(std::move(handler));
      } else {
        auto handler = boost::asio::bind_cancellation_slot(
            operation_->cancellation_slot(),
            [operation = operation_](boost::system::error_code error,
                                     value_type value) mutable {
              if (error) {
                operation->complete_exception(std::make_exception_ptr(
                    boost::system::system_error(error)));
              } else {
                operation->complete(std::move(value));
              }
            });
        start(std::move(handler));
      }

      if (promise.runtime().cancellation) {
        auto weak_operation =
            std::weak_ptr<ActorAsioOperationState<value_type>>{operation_};
        auto registration = promise.runtime().cancellation->register_callback(
            [weak_operation, executor = executor_]() mutable {
              boost::asio::post(executor, [weak_operation]() mutable {
                if (auto operation = weak_operation.lock()) {
                  operation->request_cancellation();
                }
              });
            });
        operation_->attach_cancellation(std::move(registration));
      }
    } catch (...) {
      if constexpr (std::is_void_v<value_type>) {
        operation_->complete(std::current_exception());
      } else {
        operation_->complete_exception(std::current_exception());
      }
    }
  }

  auto await_resume() -> value_type {
    if constexpr (std::is_void_v<value_type>) {
      operation_->take_result();
      return;
    } else {
      return operation_->take_result();
    }
  }

private:
  template <typename Handler> void start(Handler handler) {
    std::apply(
        [this, handler = std::move(handler)](auto &&...args) mutable {
          std::invoke(std::move(initiation_), std::move(handler),
                      std::forward<decltype(args)>(args)...);
        },
        std::move(args_));
  }

  boost::asio::any_io_executor executor_;
  Initiation initiation_;
  std::tuple<InitArgs...> args_;
  std::shared_ptr<ActorAsioOperationState<value_type>> operation_;
};

} // namespace detail
} // namespace obcx::core

namespace boost::asio {

template <typename Result>
class async_result<::obcx::core::ActorAsioCompletionToken,
                   Result(boost::system::error_code)> {
public:
  template <typename Initiation, typename... InitArgs>
  static auto initiate(Initiation initiation,
                       ::obcx::core::ActorAsioCompletionToken token,
                       InitArgs... args) {
    using awaiter_type = ::obcx::core::detail::ActorAsioDirectAwaiter<
        void, std::decay_t<Initiation>, std::decay_t<InitArgs>...>;
    return awaiter_type{token.executor(), std::move(initiation),
                        std::move(args)...};
  }
};

template <typename Result, typename Value>
class async_result<::obcx::core::ActorAsioCompletionToken,
                   Result(boost::system::error_code, Value)> {
public:
  template <typename Initiation, typename... InitArgs>
  static auto initiate(Initiation initiation,
                       ::obcx::core::ActorAsioCompletionToken token,
                       InitArgs... args) {
    using awaiter_type =
        ::obcx::core::detail::ActorAsioDirectAwaiter<std::decay_t<Value>,
                                                     std::decay_t<Initiation>,
                                                     std::decay_t<InitArgs>...>;
    return awaiter_type{token.executor(), std::move(initiation),
                        std::move(args)...};
  }
};

} // namespace boost::asio

#endif // OBCX_INCLUDE_CORE_ACTOR_ASIO_HPP_
