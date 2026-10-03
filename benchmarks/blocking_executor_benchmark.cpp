#include "core/actor/blocking_executor.hpp"
#include "core/actor/native_actor_scheduler.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/use_future.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace obcx::core {
namespace {

namespace asio = boost::asio;

struct CompletionSamples {
  std::vector<double> microseconds;
  double elapsed_seconds = 0;
};

struct CompletionSummary {
  double mean_us = 0;
  double p50_us = 0;
  double p95_us = 0;
  double operations_per_second = 0;
};

auto summarize(CompletionSamples samples) -> CompletionSummary {
  std::ranges::sort(samples.microseconds);
  const auto percentile = [&samples](const double ratio) {
    const auto index = static_cast<std::size_t>(
        ratio * static_cast<double>(samples.microseconds.size() - 1));
    return samples.microseconds[index];
  };
  return {
      .mean_us =
          std::accumulate(samples.microseconds.begin(),
                          samples.microseconds.end(), 0.0) /
          static_cast<double>(samples.microseconds.size()),
      .p50_us = percentile(0.50),
      .p95_us = percentile(0.95),
      .operations_per_second =
          static_cast<double>(samples.microseconds.size()) /
          samples.elapsed_seconds,
  };
}

void perform_work(const std::chrono::microseconds duration) {
  if (duration.count() > 0) {
    std::this_thread::sleep_for(duration);
  }
}

class PollingBridgeBaseline {
public:
  explicit PollingBridgeBaseline(const std::size_t workers) : pool_(workers) {}

  auto run(const std::chrono::microseconds work)
      -> asio::awaitable<int, asio::any_io_executor> {
    auto promise = std::make_shared<std::promise<int>>();
    auto future = promise->get_future();
    asio::post(pool_, [promise, work] {
      perform_work(work);
      promise->set_value(42);
    });

    while (future.wait_for(std::chrono::milliseconds{1}) !=
           std::future_status::ready) {
      asio::steady_timer timer{co_await asio::this_coro::executor,
                               std::chrono::milliseconds{1}};
      co_await timer.async_wait(asio::use_awaitable);
    }
    co_return future.get();
  }

  void join() { pool_.join(); }

private:
  asio::thread_pool pool_;
};

auto measure_event_driven(const std::size_t iterations,
                          const std::chrono::microseconds work)
    -> CompletionSamples {
  BlockingExecutor executor(2);
  asio::io_context io;
  auto samples = asio::co_spawn(
      io,
      [&executor, iterations, work]() -> asio::awaitable<CompletionSamples> {
        CompletionSamples result;
        result.microseconds.reserve(iterations);
        const auto all_started = std::chrono::steady_clock::now();
        for (std::size_t index = 0; index < iterations; ++index) {
          const auto started = std::chrono::steady_clock::now();
          const auto value =
              co_await executor.run([work] {
                perform_work(work);
                return 42;
              });
          if (value != 42) {
            throw std::runtime_error("event-driven result mismatch");
          }
          result.microseconds.push_back(
              std::chrono::duration<double, std::micro>(
                  std::chrono::steady_clock::now() - started)
                  .count());
        }
        result.elapsed_seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                          all_started)
                .count();
        co_return result;
      },
      asio::use_future);
  io.run();
  auto result = samples.get();
  executor.shutdown();
  return result;
}

auto measure_polling_baseline(const std::size_t iterations,
                              const std::chrono::microseconds work)
    -> CompletionSamples {
  PollingBridgeBaseline executor(2);
  asio::io_context io;
  auto samples = asio::co_spawn(
      io,
      [&executor, iterations, work]() -> asio::awaitable<CompletionSamples> {
        CompletionSamples result;
        result.microseconds.reserve(iterations);
        const auto all_started = std::chrono::steady_clock::now();
        for (std::size_t index = 0; index < iterations; ++index) {
          const auto started = std::chrono::steady_clock::now();
          if (co_await executor.run(work) != 42) {
            throw std::runtime_error("polling baseline result mismatch");
          }
          result.microseconds.push_back(
              std::chrono::duration<double, std::micro>(
                  std::chrono::steady_clock::now() - started)
                  .count());
        }
        result.elapsed_seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                          all_started)
                .count();
        co_return result;
      },
      asio::use_future);
  io.run();
  auto result = samples.get();
  executor.join();
  return result;
}

class BlockingGate {
public:
  void wait() {
    std::unique_lock lock(mutex_);
    started_ = true;
    changed_.notify_all();
    changed_.wait(lock, [this] { return released_; });
  }

  void wait_for_start() {
    std::unique_lock lock(mutex_);
    if (!changed_.wait_for(lock, std::chrono::seconds{5},
                           [this] { return started_; })) {
      throw std::runtime_error("blocking partition did not start");
    }
  }

  void release() {
    std::scoped_lock lock(mutex_);
    released_ = true;
    changed_.notify_all();
  }

private:
  std::mutex mutex_;
  std::condition_variable changed_;
  bool started_ = false;
  bool released_ = false;
};

class PartitionBenchmarkActor final : public IActorV2 {
public:
  PartitionBenchmarkActor(std::shared_ptr<BlockingGate> gate,
                          std::shared_ptr<std::atomic_bool> same_started)
      : gate_(std::move(gate)), same_started_(std::move(same_started)) {}

  [[nodiscard]] auto get_name() const -> std::string override {
    return "blocking-partition-benchmark";
  }
  [[nodiscard]] auto get_version() const -> std::string override {
    return "benchmark";
  }

  auto handle_message(const MessageEnvelope &message, ActorContext &context)
      -> ActorTask<ActorResult> override {
    if (message.id == "blocked") {
      co_await context.run_blocking([gate = gate_] { gate->wait(); });
    } else if (message.id == "same") {
      same_started_->store(true, std::memory_order_release);
    }
    co_return ActorResult::success();
  }

private:
  std::shared_ptr<BlockingGate> gate_;
  std::shared_ptr<std::atomic_bool> same_started_;
};

struct PartitionSummary {
  double elapsed_ms = 0;
  double operations_per_second = 0;
  bool same_partition_started_before_release = false;
};

auto measure_independent_partitions(const std::size_t count)
    -> PartitionSummary {
  asio::io_context io;
  auto work = asio::make_work_guard(io);
  std::thread io_thread([&io] { io.run(); });
  auto blocking_executor = std::make_shared<BlockingExecutor>(1);
  auto io_executor =
      std::make_shared<asio::any_io_executor>(io.get_executor());
  auto gate = std::make_shared<BlockingGate>();
  auto same_started = std::make_shared<std::atomic_bool>(false);

  NativeActorScheduler scheduler(
      NativeActorSchedulerOptions{.worker_count = 1});
  scheduler.register_service<BlockingExecutor>(blocking_executor);
  scheduler.register_service<asio::any_io_executor>(io_executor);
  scheduler.register_actor(
      std::make_shared<PartitionBenchmarkActor>(gate, same_started));

  std::promise<ActorResult> blocked_promise;
  auto blocked = blocked_promise.get_future();
  MessageEnvelope blocked_message;
  blocked_message.id = "blocked";
  if (!scheduler.enqueue(
          ActorInvocation{.actor_id = "blocking-partition-benchmark",
                          .partition_key = "held",
                          .message = std::move(blocked_message)},
          [&blocked_promise](ActorResult result) {
            blocked_promise.set_value(std::move(result));
          })) {
    throw std::runtime_error("blocked benchmark message was rejected");
  }
  gate->wait_for_start();

  std::promise<ActorResult> same_promise;
  auto same = same_promise.get_future();
  MessageEnvelope same_message;
  same_message.id = "same";
  if (!scheduler.enqueue(
          ActorInvocation{.actor_id = "blocking-partition-benchmark",
                          .partition_key = "held",
                          .message = std::move(same_message)},
          [&same_promise](ActorResult result) {
            same_promise.set_value(std::move(result));
          })) {
    throw std::runtime_error("same-partition benchmark message was rejected");
  }

  std::atomic_size_t completed = 0;
  std::promise<void> all_completed_promise;
  auto all_completed = all_completed_promise.get_future();
  const auto started = std::chrono::steady_clock::now();
  for (std::size_t index = 0; index < count; ++index) {
    MessageEnvelope message;
    message.id = "independent-" + std::to_string(index);
    if (!scheduler.enqueue(
            ActorInvocation{
                .actor_id = "blocking-partition-benchmark",
                .partition_key = "partition-" + std::to_string(index),
                .message = std::move(message),
            },
            [&completed, &all_completed_promise, count](ActorResult result) {
              if (!result.ok()) {
                throw std::runtime_error(
                    "independent benchmark invocation failed");
              }
              if (completed.fetch_add(1, std::memory_order_acq_rel) + 1 ==
                  count) {
                all_completed_promise.set_value();
              }
            })) {
      throw std::runtime_error(
          "independent benchmark message was rejected");
    }
  }
  if (all_completed.wait_for(std::chrono::seconds{30}) !=
      std::future_status::ready) {
    throw std::runtime_error("independent partitions did not complete");
  }
  const auto elapsed =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started);
  const auto same_before_release =
      same_started->load(std::memory_order_acquire);

  gate->release();
  if (blocked.wait_for(std::chrono::seconds{5}) !=
          std::future_status::ready ||
      same.wait_for(std::chrono::seconds{5}) != std::future_status::ready ||
      !blocked.get().ok() || !same.get().ok()) {
    throw std::runtime_error("held partition did not retire cleanly");
  }

  scheduler.shutdown();
  blocking_executor->shutdown();
  work.reset();
  io.stop();
  io_thread.join();

  return {
      .elapsed_ms = elapsed.count() * 1000.0,
      .operations_per_second = static_cast<double>(count) / elapsed.count(),
      .same_partition_started_before_release = same_before_release,
  };
}

} // namespace
} // namespace obcx::core

auto main(int argc, char **argv) -> int {
  std::size_t iterations = 500;
  std::size_t work_us = 1000;
  std::size_t partitions = 20000;
  for (int index = 1; index + 1 < argc; index += 2) {
    const std::string option = argv[index];
    if (option == "--iterations") {
      iterations = std::stoull(argv[index + 1]);
    } else if (option == "--work-us") {
      work_us = std::stoull(argv[index + 1]);
    } else if (option == "--partitions") {
      partitions = std::stoull(argv[index + 1]);
    } else {
      std::cerr << "unknown option: " << option << '\n';
      return 2;
    }
  }
  if (iterations == 0 || partitions == 0) {
    std::cerr << "iterations and partitions must be positive\n";
    return 2;
  }

  try {
    const auto work = std::chrono::microseconds{work_us};
    const auto event_driven = obcx::core::summarize(
        obcx::core::measure_event_driven(iterations, work));
    const auto polling = obcx::core::summarize(
        obcx::core::measure_polling_baseline(iterations, work));
    const auto partitions_result =
        obcx::core::measure_independent_partitions(partitions);

    std::cout << "event_driven iterations=" << iterations
              << " work_us=" << work_us
              << " mean_us=" << event_driven.mean_us
              << " p50_us=" << event_driven.p50_us
              << " p95_us=" << event_driven.p95_us
              << " ops_per_second=" << event_driven.operations_per_second
              << '\n';
    std::cout << "polling_baseline iterations=" << iterations
              << " work_us=" << work_us << " mean_us=" << polling.mean_us
              << " p50_us=" << polling.p50_us
              << " p95_us=" << polling.p95_us
              << " ops_per_second=" << polling.operations_per_second << '\n';
    std::cout << "independent_partitions count=" << partitions
              << " elapsed_ms=" << partitions_result.elapsed_ms
              << " ops_per_second="
              << partitions_result.operations_per_second
              << " same_partition_started_before_release="
              << std::boolalpha
              << partitions_result.same_partition_started_before_release
              << '\n';
  } catch (const std::exception &error) {
    std::cerr << "blocking_executor_benchmark failed: " << error.what()
              << '\n';
    return 1;
  }
  return 0;
}
