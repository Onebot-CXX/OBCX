#pragma once

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/associated_executor.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

namespace obcx::core {

class BlockingExecutorUnavailable final : public std::runtime_error {
public:
  BlockingExecutorUnavailable()
      : std::runtime_error("runtime blocking executor is unavailable") {}
};

class BlockingExecutorStopped final : public std::runtime_error {
public:
  BlockingExecutorStopped()
      : std::runtime_error("runtime blocking executor is stopped") {}
};

namespace detail {

template <typename T> struct IsAsioAwaitable : std::false_type {};

template <typename T, typename Executor>
struct IsAsioAwaitable<boost::asio::awaitable<T, Executor>> : std::true_type {};

template <typename Func>
using BlockingCallableResult = std::invoke_result_t<std::decay_t<Func> &>;

} // namespace detail

template <typename Func>
concept BlockingCallable =
    std::invocable<std::decay_t<Func> &> &&
    !std::is_reference_v<detail::BlockingCallableResult<Func>> &&
    !detail::IsAsioAwaitable<
        std::remove_cvref_t<detail::BlockingCallableResult<Func>>>::value;

struct BlockingExecutorMetrics {
  std::uint64_t submitted = 0;
  std::uint64_t running = 0;
  std::uint64_t pending = 0;
  std::uint64_t completed = 0;
  std::uint64_t failed = 0;
  std::uint64_t rejected = 0;
};

/**
 * Process-owned executor for synchronous blocking or CPU-heavy functions.
 *
 * The underlying thread pool is intentionally not exposed. Asynchronous I/O,
 * timers, and coroutine orchestration belong on an I/O executor; only the
 * complete dynamic extent of a synchronous callable runs here.
 */
class BlockingExecutor {
public:
  explicit BlockingExecutor(
      std::size_t thread_count = std::thread::hardware_concurrency())
      : state_(std::make_shared<State>(thread_count == 0 ? std::size_t{1}
                                                         : thread_count)) {}

  BlockingExecutor(const BlockingExecutor &) = delete;
  auto operator=(const BlockingExecutor &) -> BlockingExecutor & = delete;
  BlockingExecutor(BlockingExecutor &&) = delete;
  auto operator=(BlockingExecutor &&) -> BlockingExecutor & = delete;

  ~BlockingExecutor() { shutdown(); }

  [[nodiscard]] auto worker_count() const noexcept -> std::size_t {
    return state_->worker_count;
  }

  [[nodiscard]] auto accepting() const noexcept -> bool {
    return state_->accepting.load(std::memory_order_acquire);
  }

  [[nodiscard]] auto metrics() const noexcept -> BlockingExecutorMetrics {
    return {
        .submitted = state_->submitted.load(std::memory_order_relaxed),
        .running = state_->running.load(std::memory_order_relaxed),
        .pending = state_->pending.load(std::memory_order_relaxed),
        .completed = state_->completed.load(std::memory_order_relaxed),
        .failed = state_->failed.load(std::memory_order_relaxed),
        .rejected = state_->rejected.load(std::memory_order_relaxed),
    };
  }

  void close_admission() noexcept {
    std::scoped_lock lock(state_->admission_mutex);
    state_->accepting.store(false, std::memory_order_release);
  }

  /**
   * Close admission and drain every already-posted callable before joining.
   * This must be invoked by the process owner, not from a blocking worker.
   */
  void shutdown() noexcept {
    std::scoped_lock shutdown_lock(state_->shutdown_mutex);
    if (state_->joined) {
      return;
    }
    {
      std::scoped_lock admission_lock(state_->admission_mutex);
      state_->accepting.store(false, std::memory_order_release);
    }
    try {
      state_->pool.join();
    } catch (...) {
      state_->pool.stop();
      try {
        state_->pool.join();
      } catch (...) {
      }
    }
    state_->joined = true;
  }

  /**
   * Submit a synchronous callable and complete through the token's associated
   * executor. Non-void completion signature is
   * void(exception_ptr, optional<Result>); void completion is
   * void(exception_ptr). Core owns the worker and completion trampolines:
   * callable destruction precedes completion publication, so an actor cannot
   * finish draining while actor-defined worker frames are still unwinding.
   */
  template <BlockingCallable Func, typename CompletionToken>
  auto async_run(Func &&function, CompletionToken &&token) {
    using function_type = std::decay_t<Func>;
    using result_type = detail::BlockingCallableResult<Func>;

    if constexpr (std::is_void_v<result_type>) {
      return boost::asio::async_initiate<CompletionToken,
                                         void(std::exception_ptr)>(
          [state = state_, function = function_type(std::forward<Func>(
                               function))](auto &&handler) mutable {
            using handler_type = std::decay_t<decltype(handler)>;
            auto shared_handler = std::make_shared<handler_type>(
                std::forward<decltype(handler)>(handler));
            auto executor = boost::asio::any_io_executor{
                boost::asio::get_associated_executor(*shared_handler)};
            auto work_guard = std::make_shared<
                boost::asio::executor_work_guard<boost::asio::any_io_executor>>(
                boost::asio::make_work_guard(executor));
            auto complete = [shared_handler = std::move(shared_handler),
                             work_guard = std::move(work_guard)](
                                std::exception_ptr exception) mutable {
              (*shared_handler)(std::move(exception));
              work_guard->reset();
            };
            dispatch_work(
                state, std::move(executor),
                [function = std::move(function)]() mutable {
                  std::invoke(function);
                },
                std::move(complete));
          },
          token);
    } else {
      return boost::asio::async_initiate<CompletionToken,
                                         void(std::exception_ptr,
                                              std::optional<result_type>)>(
          [state = state_, function = function_type(std::forward<Func>(
                               function))](auto &&handler) mutable {
            using handler_type = std::decay_t<decltype(handler)>;
            auto shared_handler = std::make_shared<handler_type>(
                std::forward<decltype(handler)>(handler));
            auto executor = boost::asio::any_io_executor{
                boost::asio::get_associated_executor(*shared_handler)};
            auto work_guard = std::make_shared<
                boost::asio::executor_work_guard<boost::asio::any_io_executor>>(
                boost::asio::make_work_guard(executor));
            auto result = std::make_shared<std::optional<result_type>>();
            auto complete = [shared_handler = std::move(shared_handler),
                             work_guard = std::move(work_guard),
                             result](std::exception_ptr exception) mutable {
              (*shared_handler)(std::move(exception), std::move(*result));
              work_guard->reset();
            };
            dispatch_work(
                state, std::move(executor),
                [function = std::move(function),
                 result = std::move(result)]() mutable {
                  result->emplace(std::invoke(function));
                },
                std::move(complete));
          },
          token);
    }
  }

  template <BlockingCallable Func>
  auto run(Func &&function)
      -> boost::asio::awaitable<detail::BlockingCallableResult<Func>,
                                boost::asio::any_io_executor> {
    using result_type = detail::BlockingCallableResult<Func>;
    // This entry point must not itself be a coroutine: async_run captures the
    // decayed callable and shared pool state now, before either the callable's
    // temporary or this facade can be destroyed. Deferred awaiting keeps only
    // those owned values, and still joins the worker before returning a result.
    if constexpr (std::is_void_v<result_type>) {
      return async_run(std::forward<Func>(function),
                       boost::asio::use_awaitable);
    } else {
      return await_result<result_type>(
          async_run(std::forward<Func>(function), boost::asio::use_awaitable));
    }
  }

private:
  template <typename Result>
  static auto await_result(boost::asio::awaitable<std::optional<Result>,
                                                  boost::asio::any_io_executor>
                               operation)
      -> boost::asio::awaitable<Result, boost::asio::any_io_executor> {
    auto result = co_await std::move(operation);
    if (!result.has_value())
      throw std::logic_error("blocking callable completed without a result");
    co_return std::move(*result);
  }

  struct State {
    explicit State(const std::size_t count)
        : pool(count), worker_count(count) {}

    boost::asio::thread_pool pool;
    const std::size_t worker_count;
    std::mutex admission_mutex;
    std::mutex shutdown_mutex;
    std::atomic_bool accepting = true;
    bool joined = false;
    std::atomic_uint64_t submitted = 0;
    std::atomic_uint64_t running = 0;
    std::atomic_uint64_t pending = 0;
    std::atomic_uint64_t completed = 0;
    std::atomic_uint64_t failed = 0;
    std::atomic_uint64_t rejected = 0;
  };

  struct Work;
  static void dispatch_work(
      std::shared_ptr<State> state, boost::asio::any_io_executor executor,
      std::move_only_function<void()> work,
      std::move_only_function<void(std::exception_ptr)> completion);

  std::shared_ptr<State> state_;
};

} // namespace obcx::core
