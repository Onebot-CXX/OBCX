#include "core/actor/native_actor_scheduler.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <numeric>
#include <random>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace obcx::core {
namespace {

class ImmediateExternalAwaiter {
public:
  [[nodiscard]] auto await_ready() const noexcept -> bool { return false; }

  template <typename Promise>
  void await_suspend(std::coroutine_handle<Promise> handle) const noexcept {
    auto &promise = static_cast<ActorTaskPromiseBase &>(handle.promise());
    const auto epoch = promise.begin_io_suspension();
    promise.runtime().make_runnable(epoch);
  }

  void await_resume() const noexcept {}
};

class StressActor final : public IActorV2 {
public:
  [[nodiscard]] auto get_name() const -> std::string override {
    return "stress";
  }
  [[nodiscard]] auto get_version() const -> std::string override {
    return "v2-stress";
  }

  auto handle_message(const MessageEnvelope &, ActorContext &)
      -> ActorTask<ActorResult> override {
    co_await ImmediateExternalAwaiter{};
    co_return ActorResult::success();
  }
};

class LateCompletionGate {
public:
  class Awaiter {
  public:
    explicit Awaiter(LateCompletionGate &gate) : gate_(gate) {}

    [[nodiscard]] auto await_ready() const noexcept -> bool { return false; }

    template <typename Promise>
    void await_suspend(std::coroutine_handle<Promise> handle) {
      auto &promise = static_cast<ActorTaskPromiseBase &>(handle.promise());
      const auto epoch = promise.begin_io_suspension();
      auto make_runnable = promise.runtime().make_runnable;
      std::scoped_lock lock(gate_.mutex_);
      gate_.notifications_.push_back([make_runnable = std::move(make_runnable),
                                      epoch] { make_runnable(epoch); });
    }

    void await_resume() const noexcept {}

  private:
    LateCompletionGate &gate_;
  };

  [[nodiscard]] auto wait() -> Awaiter { return Awaiter{*this}; }

  void notify_all() const {
    std::vector<std::function<void()>> notifications;
    {
      std::scoped_lock lock(mutex_);
      notifications = notifications_;
    }
    for (const auto &notify : notifications) {
      notify();
    }
  }

private:
  mutable std::mutex mutex_;
  std::vector<std::function<void()>> notifications_;
};

class CancellationActor final : public IActorV2 {
public:
  explicit CancellationActor(std::shared_ptr<LateCompletionGate> gate)
      : gate_(std::move(gate)) {}

  [[nodiscard]] auto get_name() const -> std::string override {
    return "cancel";
  }
  [[nodiscard]] auto get_version() const -> std::string override {
    return "v2-stress";
  }

  auto handle_message(const MessageEnvelope &, ActorContext &)
      -> ActorTask<ActorResult> override {
    co_await gate_->wait();
    co_return ActorResult::success();
  }

private:
  std::shared_ptr<LateCompletionGate> gate_;
};

struct BatchCompletion {
  explicit BatchCompletion(const size_t expected_count)
      : expected(expected_count) {}

  const size_t expected;
  std::atomic_size_t completed = 0;
  std::promise<void> done;
};

auto wait_until(const std::function<bool()> &predicate,
                const std::chrono::steady_clock::duration timeout) -> bool {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (!predicate()) {
    if (std::chrono::steady_clock::now() >= deadline) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
  return true;
}

auto run_transition_stress(const size_t invocation_count,
                           const size_t batch_size, const uint32_t seed)
    -> NativeActorSchedulerMetrics {
  NativeActorScheduler scheduler(NativeActorSchedulerOptions{
      .worker_count = 4,
      .slow_resume_warning_ms = 0,
  });
  scheduler.register_actor(std::make_shared<StressActor>());

  std::vector<std::atomic_uint8_t> seen(invocation_count);
  for (auto &value : seen) {
    value.store(0, std::memory_order_relaxed);
  }
  std::atomic_size_t failures = 0;
  std::atomic_size_t duplicates = 0;
  std::minstd_rand random(seed);

  for (size_t begin = 0; begin < invocation_count; begin += batch_size) {
    const auto count = std::min(batch_size, invocation_count - begin);
    std::vector<size_t> order(count);
    std::iota(order.begin(), order.end(), size_t{0});
    std::ranges::shuffle(order, random);
    auto batch = std::make_shared<BatchCompletion>(count);
    auto ready = batch->done.get_future();

    for (const auto offset : order) {
      const auto index = begin + offset;
      MessageEnvelope message;
      message.id = "stress-" + std::to_string(index);
      message.type = "SyntheticStress";
      const auto accepted = scheduler.enqueue(
          ActorInvocation{
              .actor_id = "stress",
              .partition_key =
                  "partition-" + std::to_string((index * 17U) % 4096U),
              .message = std::move(message),
          },
          [index, &seen, &failures, &duplicates,
           batch](ActorResult result) mutable {
            if (!result.ok()) {
              failures.fetch_add(1, std::memory_order_relaxed);
            }
            if (seen[index].fetch_add(1, std::memory_order_relaxed) != 0) {
              duplicates.fetch_add(1, std::memory_order_relaxed);
            }
            if (batch->completed.fetch_add(1, std::memory_order_acq_rel) + 1 ==
                batch->expected) {
              batch->done.set_value();
            }
          });
      if (!accepted) {
        throw std::runtime_error("stress scheduler rejected an invocation");
      }
    }

    if (ready.wait_for(std::chrono::seconds{30}) !=
        std::future_status::ready) {
      throw std::runtime_error("stress batch timed out");
    }
  }

  scheduler.shutdown();
  const auto metrics = scheduler.metrics();
  const auto lost = std::ranges::count_if(seen, [](const auto &value) {
    return value.load(std::memory_order_relaxed) != 1;
  });
  if (lost != 0 || duplicates.load(std::memory_order_relaxed) != 0 ||
      failures.load(std::memory_order_relaxed) != 0 ||
      metrics.accepted != invocation_count ||
      metrics.completed != invocation_count || metrics.pending != 0 ||
      metrics.running_resumes < invocation_count * 2 ||
      metrics.suspensions < invocation_count) {
    throw std::runtime_error(
        "stress transition accounting detected loss or duplication");
  }
  return metrics;
}

auto run_cancellation_stress(const size_t invocation_count)
    -> NativeActorSchedulerMetrics {
  auto gate = std::make_shared<LateCompletionGate>();
  std::atomic_size_t completions = 0;
  std::atomic_size_t unexpected_successes = 0;
  NativeActorSchedulerMetrics metrics;
  {
    NativeActorScheduler scheduler(NativeActorSchedulerOptions{
        .worker_count = 4,
        .slow_resume_warning_ms = 0,
    });
    scheduler.register_actor(std::make_shared<CancellationActor>(gate));
    for (size_t index = 0; index < invocation_count; ++index) {
      MessageEnvelope message;
      message.id = "cancel-" + std::to_string(index);
      message.type = "SyntheticCancellation";
      if (!scheduler.enqueue(
              ActorInvocation{
                  .actor_id = "cancel",
                  .partition_key = "partition-" + std::to_string(index),
                  .message = std::move(message),
              },
              [&completions, &unexpected_successes](ActorResult result) {
                if (result.ok() || !result.failure ||
                    result.failure->code != "scheduler_cancelled") {
                  unexpected_successes.fetch_add(1, std::memory_order_relaxed);
                }
                completions.fetch_add(1, std::memory_order_relaxed);
              })) {
        throw std::runtime_error(
            "cancellation scheduler rejected an invocation");
      }
    }

    if (!wait_until(
            [&scheduler, invocation_count] {
              return scheduler.metrics().suspended == invocation_count;
            },
            std::chrono::seconds{30})) {
      throw std::runtime_error("cancellation tasks did not all suspend");
    }
    scheduler.shutdown(ActorExecutorShutdownMode::Cancel);
    gate->notify_all();
    metrics = scheduler.metrics();
    if (completions.load(std::memory_order_relaxed) != invocation_count ||
        unexpected_successes.load(std::memory_order_relaxed) != 0 ||
        metrics.cancelled != invocation_count || metrics.pending != 0 ||
        metrics.late_completions < invocation_count) {
      throw std::runtime_error(
          "cancellation stress detected a missing or duplicate completion");
    }
  }

  const auto before_post_destruction =
      completions.load(std::memory_order_relaxed);
  gate->notify_all();
  if (completions.load(std::memory_order_relaxed) != before_post_destruction) {
    throw std::runtime_error("notification resumed work after destruction");
  }
  return metrics;
}

} // namespace
} // namespace obcx::core

auto main(int argc, char **argv) -> int {
  size_t invocation_count = 200'000;
  size_t batch_size = 10'000;
  uint32_t seed = 0x4f424358U;
  for (int index = 1; index + 1 < argc; index += 2) {
    const std::string option = argv[index];
    if (option == "--invocations") {
      invocation_count = std::stoull(argv[index + 1]);
    } else if (option == "--batch") {
      batch_size = std::stoull(argv[index + 1]);
    } else if (option == "--seed") {
      seed = static_cast<uint32_t>(std::stoul(argv[index + 1], nullptr, 0));
    } else {
      std::cerr << "unknown option: " << option << '\n';
      return 2;
    }
  }
  if (invocation_count == 0 || batch_size == 0) {
    std::cerr << "invocations and batch must be positive\n";
    return 2;
  }

  try {
    const auto started = std::chrono::steady_clock::now();
    const auto transition_metrics =
        obcx::core::run_transition_stress(invocation_count, batch_size, seed);
    const auto cancellation_metrics = obcx::core::run_cancellation_stress(2048);
    const auto transitions =
        transition_metrics.accepted + transition_metrics.running_resumes +
        transition_metrics.suspensions + transition_metrics.completed +
        cancellation_metrics.accepted + cancellation_metrics.running_resumes +
        cancellation_metrics.suspensions + cancellation_metrics.cancelled;
    if (transitions < 1'000'000) {
      throw std::runtime_error(
          "stress run did not reach one million transitions");
    }
    const auto elapsed = std::chrono::duration<double>(
                             std::chrono::steady_clock::now() - started)
                             .count();
    std::cout << "seed=" << seed << " invocations=" << invocation_count
              << " transitions=" << transitions
              << " completed=" << transition_metrics.completed
              << " cancelled=" << cancellation_metrics.cancelled
              << " late_completions=" << cancellation_metrics.late_completions
              << " elapsed_seconds=" << elapsed << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "actor_scheduler_v2_stress failed: " << error.what() << '\n';
    return 1;
  }
}
