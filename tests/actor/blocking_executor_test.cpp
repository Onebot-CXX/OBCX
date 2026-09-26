#include "core/actor/blocking_executor.hpp"

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace obcx::core {
namespace {

namespace asio = boost::asio;
using namespace std::chrono_literals;

struct ReferenceCallable {
  auto operator()() -> int &;
};

struct AwaitableCallable {
  auto operator()() -> asio::awaitable<int>;
};

static_assert(!BlockingCallable<ReferenceCallable>);
static_assert(!BlockingCallable<AwaitableCallable>);

class BlockingGate {
public:
  void arrive_and_wait() {
    std::unique_lock lock(mutex_);
    worker_thread_ = std::this_thread::get_id();
    started_ = true;
    changed_.notify_all();
    changed_.wait(lock, [this] { return released_; });
  }

  [[nodiscard]] auto wait_for_start() -> std::thread::id {
    std::unique_lock lock(mutex_);
    if (!changed_.wait_for(lock, 2s, [this] { return started_; })) {
      throw std::runtime_error("blocking callable did not start");
    }
    return worker_thread_;
  }

  void release() {
    std::scoped_lock lock(mutex_);
    released_ = true;
    changed_.notify_all();
  }

private:
  std::mutex mutex_;
  std::condition_variable changed_;
  std::thread::id worker_thread_;
  bool started_ = false;
  bool released_ = false;
};

TEST(BlockingExecutorTest, ValueAndMoveOnlyResultReturnToCallerExecutor) {
  BlockingExecutor executor(1);
  asio::io_context io;
  std::promise<std::thread::id> io_thread_promise;
  auto io_thread_result = io_thread_promise.get_future();

  auto result = asio::co_spawn(
      io,
      [&executor]()
          -> asio::awaitable<std::pair<std::thread::id, std::unique_ptr<int>>> {
        const auto caller_thread = std::this_thread::get_id();
        auto value =
            co_await executor.run([] { return std::make_unique<int>(42); });
        EXPECT_EQ(std::this_thread::get_id(), caller_thread);
        co_return std::pair{caller_thread, std::move(value)};
      },
      asio::use_future);

  std::thread io_thread([&] {
    io_thread_promise.set_value(std::this_thread::get_id());
    io.run();
  });

  auto [caller_thread, value] = result.get();
  EXPECT_EQ(caller_thread, io_thread_result.get());
  ASSERT_NE(value, nullptr);
  EXPECT_EQ(*value, 42);
  io_thread.join();
  executor.shutdown();

  const auto metrics = executor.metrics();
  EXPECT_EQ(metrics.submitted, 1);
  EXPECT_EQ(metrics.completed, 1);
  EXPECT_EQ(metrics.failed, 0);
  EXPECT_EQ(metrics.pending, 0);
  EXPECT_EQ(metrics.running, 0);
}

TEST(BlockingExecutorTest, IoExecutorProgressesWhileCallableIsBlocked) {
  BlockingExecutor executor(1);
  asio::io_context io;
  auto gate = std::make_shared<BlockingGate>();

  auto blocked = asio::co_spawn(
      io,
      [&executor, gate]() -> asio::awaitable<void> {
        co_await executor.run([gate] { gate->arrive_and_wait(); });
      },
      asio::use_future);
  std::thread io_thread([&] { io.run(); });

  const auto blocking_thread = gate->wait_for_start();
  std::promise<std::thread::id> marker_promise;
  auto marker = marker_promise.get_future();
  asio::post(io, [&marker_promise] {
    marker_promise.set_value(std::this_thread::get_id());
  });
  ASSERT_EQ(marker.wait_for(200ms), std::future_status::ready);
  EXPECT_NE(marker.get(), blocking_thread);
  EXPECT_EQ(blocked.wait_for(20ms), std::future_status::timeout);

  gate->release();
  blocked.get();
  io_thread.join();
  executor.shutdown();
}

TEST(BlockingExecutorTest, VoidAndExceptionCompleteExactlyOnce) {
  BlockingExecutor executor(1);
  asio::io_context io;
  std::atomic_int void_calls = 0;

  auto result = asio::co_spawn(
      io,
      [&executor, &void_calls]() -> asio::awaitable<std::string> {
        co_await executor.run([&void_calls] {
          void_calls.fetch_add(1, std::memory_order_relaxed);
        });
        try {
          co_await executor.run(
              []() -> void { throw std::runtime_error("blocking failure"); });
        } catch (const std::runtime_error &error) {
          co_return error.what();
        }
        co_return "missing exception";
      },
      asio::use_future);

  io.run();
  EXPECT_EQ(result.get(), "blocking failure");
  EXPECT_EQ(void_calls.load(std::memory_order_relaxed), 1);
  executor.shutdown();

  const auto metrics = executor.metrics();
  EXPECT_EQ(metrics.submitted, 2);
  EXPECT_EQ(metrics.completed, 1);
  EXPECT_EQ(metrics.failed, 1);
}

TEST(BlockingExecutorTest, ImmediateCompletionIsNeverInline) {
  BlockingExecutor executor(1);
  asio::io_context io;
  bool initiating = true;
  bool called_inline = false;
  std::promise<int> completion_promise;
  auto completion = completion_promise.get_future();

  executor.async_run(
      [] { return 7; },
      asio::bind_executor(
          io.get_executor(),
          [&initiating, &called_inline, &completion_promise](
              std::exception_ptr exception, std::optional<int> value) {
            called_inline = initiating;
            if (exception) {
              completion_promise.set_exception(std::move(exception));
              return;
            }
            completion_promise.set_value(*value);
          }));
  initiating = false;

  EXPECT_EQ(completion.wait_for(20ms), std::future_status::timeout);
  io.run();
  EXPECT_EQ(completion.get(), 7);
  EXPECT_FALSE(called_inline);
  executor.shutdown();
}

TEST(BlockingExecutorTest, ClosedAdmissionFailsAsynchronously) {
  BlockingExecutor executor(1);
  executor.shutdown();
  asio::io_context io;
  bool callable_ran = false;

  auto result = asio::co_spawn(
      io,
      [&executor, &callable_ran]() -> asio::awaitable<void> {
        co_await executor.run([&callable_ran] { callable_ran = true; });
      },
      asio::use_future);

  io.run();
  EXPECT_THROW(result.get(), BlockingExecutorStopped);
  EXPECT_FALSE(callable_ran);
  EXPECT_EQ(executor.metrics().rejected, 1);
}

TEST(BlockingExecutorTest, ShutdownDrainsAnAdmittedCallable) {
  BlockingExecutor executor(1);
  asio::io_context io;
  auto gate = std::make_shared<BlockingGate>();

  auto blocked = asio::co_spawn(
      io,
      [&executor, gate]() -> asio::awaitable<int> {
        co_return co_await executor.run([gate] {
          gate->arrive_and_wait();
          return 42;
        });
      },
      asio::use_future);
  std::thread io_thread([&] { io.run(); });
  static_cast<void>(gate->wait_for_start());

  auto shutdown =
      std::async(std::launch::async, [&executor] { executor.shutdown(); });
  EXPECT_EQ(shutdown.wait_for(20ms), std::future_status::timeout);

  gate->release();
  EXPECT_EQ(shutdown.wait_for(2s), std::future_status::ready);
  shutdown.get();
  EXPECT_EQ(blocked.get(), 42);
  io_thread.join();

  const auto metrics = executor.metrics();
  EXPECT_FALSE(executor.accepting());
  EXPECT_EQ(metrics.submitted, 1);
  EXPECT_EQ(metrics.completed, 1);
  EXPECT_EQ(metrics.running, 0);
  EXPECT_EQ(metrics.pending, 0);
}

} // namespace
} // namespace obcx::core
