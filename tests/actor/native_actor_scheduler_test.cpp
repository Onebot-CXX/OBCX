#include "core/actor/native_actor_scheduler.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace obcx::core {
namespace {

using namespace std::chrono_literals;

class ManualActorEvent {
public:
  class Awaiter {
  public:
    explicit Awaiter(ManualActorEvent &event) : event_(event) {}

    [[nodiscard]] auto await_ready() const -> bool {
      std::scoped_lock lock(event_.mutex_);
      return event_.ready_;
    }

    template <typename Promise>
    void await_suspend(std::coroutine_handle<Promise> handle) {
      static_assert(std::is_base_of_v<ActorTaskPromiseBase, Promise>);
      auto &promise = static_cast<ActorTaskPromiseBase &>(handle.promise());
      const auto epoch = promise.begin_io_suspension();
      auto notify = promise.runtime().make_runnable;
      bool complete_now = false;
      {
        std::scoped_lock lock(event_.mutex_);
        if (event_.ready_) {
          complete_now = true;
        } else {
          event_.waiters_.push_back(
              [notify = std::move(notify), epoch] { notify(epoch); });
        }
      }
      if (complete_now) {
        notify(epoch);
      }
    }

    void await_resume() const noexcept {}

  private:
    ManualActorEvent &event_;
  };

  [[nodiscard]] auto wait() -> Awaiter { return Awaiter{*this}; }

  void set(bool notify_twice = false) {
    std::vector<std::function<void()>> waiters;
    {
      std::scoped_lock lock(mutex_);
      ready_ = true;
      waiters.swap(waiters_);
    }
    for (auto &waiter : waiters) {
      waiter();
      if (notify_twice) {
        waiter();
      }
    }
  }

private:
  mutable std::mutex mutex_;
  bool ready_ = false;
  std::vector<std::function<void()>> waiters_;
};

struct NativeProbe {
  std::mutex mutex;
  std::condition_variable changed;
  int active = 0;
  int max_active = 0;
  std::vector<std::string> events;
};

class NativeProbeActor final : public IActorV2 {
public:
  NativeProbeActor(std::shared_ptr<NativeProbe> probe,
                   std::shared_ptr<ManualActorEvent> gate = nullptr)
      : probe_(std::move(probe)), gate_(std::move(gate)) {}

  [[nodiscard]] auto get_name() const -> std::string override {
    return "native";
  }
  [[nodiscard]] auto get_version() const -> std::string override {
    return "test";
  }

  auto handle_message(const MessageEnvelope &message, ActorContext &context)
      -> ActorTask<ActorResult> override {
    {
      std::scoped_lock lock(probe_->mutex);
      probe_->events.push_back("start:" + message.id);
      probe_->active++;
      probe_->max_active = std::max(probe_->max_active, probe_->active);
      probe_->changed.notify_all();
    }

    if (message.payload.value("yield", false)) {
      co_await context.yield();
    }
    if (gate_ && message.payload.value("wait", false)) {
      co_await gate_->wait();
    }

    {
      std::scoped_lock lock(probe_->mutex);
      probe_->events.push_back("finish:" + message.id);
      probe_->active--;
      probe_->changed.notify_all();
    }
    co_return ActorResult::success();
  }

private:
  std::shared_ptr<NativeProbe> probe_;
  std::shared_ptr<ManualActorEvent> gate_;
};

class DestructionProbeActor final : public IActorV2 {
public:
  explicit DestructionProbeActor(
      std::shared_ptr<std::atomic_size_t> destruction_count)
      : destruction_count_(std::move(destruction_count)) {}

  ~DestructionProbeActor() override {
    destruction_count_->fetch_add(1, std::memory_order_relaxed);
  }

  [[nodiscard]] auto get_name() const -> std::string override {
    return "destruction-probe";
  }
  [[nodiscard]] auto get_version() const -> std::string override {
    return "test";
  }

  auto handle_message(const MessageEnvelope &, ActorContext &)
      -> ActorTask<ActorResult> override {
    co_return ActorResult::success();
  }

private:
  std::shared_ptr<std::atomic_size_t> destruction_count_;
};

struct MigrationProbe {
  std::mutex mutex;
  std::condition_variable changed;
  std::vector<size_t> workers;
};

class MigrationActor final : public IActorV2 {
public:
  MigrationActor(std::shared_ptr<MigrationProbe> probe,
                 std::shared_ptr<ManualActorEvent> gate)
      : probe_(std::move(probe)), gate_(std::move(gate)) {}

  [[nodiscard]] auto get_name() const -> std::string override {
    return "migration";
  }
  [[nodiscard]] auto get_version() const -> std::string override {
    return "test";
  }

  auto handle_message(const MessageEnvelope &, ActorContext &)
      -> ActorTask<ActorResult> override {
    record_worker();
    co_await gate_->wait();
    record_worker();
    co_return ActorResult::success();
  }

private:
  void record_worker() {
    const auto worker = ActorWorkStealingExecutor::current_worker_id();
    if (!worker) {
      throw std::logic_error("native actor did not run on an actor worker");
    }
    std::scoped_lock lock(probe_->mutex);
    probe_->workers.push_back(*worker);
    probe_->changed.notify_all();
  }

  std::shared_ptr<MigrationProbe> probe_;
  std::shared_ptr<ManualActorEvent> gate_;
};

struct BlockingControl {
  std::mutex mutex;
  std::condition_variable changed;
  std::optional<size_t> worker;
  bool released = false;

  auto wait_for_start() -> size_t {
    std::unique_lock lock(mutex);
    if (!changed.wait_for(lock, 2s, [this] { return worker.has_value(); })) {
      throw std::runtime_error("blocking actor did not start");
    }
    return *worker;
  }

  void release() {
    std::scoped_lock lock(mutex);
    released = true;
    changed.notify_all();
  }
};

class BlockingActor final : public IActorV2 {
public:
  explicit BlockingActor(std::vector<std::shared_ptr<BlockingControl>> controls)
      : controls_(std::move(controls)) {}

  [[nodiscard]] auto get_name() const -> std::string override {
    return "blocker";
  }
  [[nodiscard]] auto get_version() const -> std::string override {
    return "test";
  }

  auto handle_message(const MessageEnvelope &message, ActorContext &)
      -> ActorTask<ActorResult> override {
    const auto index = message.payload.at("control").get<size_t>();
    auto control = controls_.at(index);
    const auto worker = ActorWorkStealingExecutor::current_worker_id();
    if (!worker) {
      throw std::logic_error("blocking actor did not run on an actor worker");
    }
    {
      std::unique_lock lock(control->mutex);
      control->worker = *worker;
      control->changed.notify_all();
      control->changed.wait(lock, [&control] { return control->released; });
    }
    co_return ActorResult::success();
  }

private:
  std::vector<std::shared_ptr<BlockingControl>> controls_;
};

class YieldLoopActor final : public IActorV2 {
public:
  explicit YieldLoopActor(std::shared_ptr<std::atomic_size_t> resumes)
      : resumes_(std::move(resumes)) {}

  [[nodiscard]] auto get_name() const -> std::string override {
    return "yield-loop";
  }
  [[nodiscard]] auto get_version() const -> std::string override {
    return "test";
  }

  auto handle_message(const MessageEnvelope &message, ActorContext &context)
      -> ActorTask<ActorResult> override {
    const auto iterations = message.payload.at("iterations").get<size_t>();
    for (size_t index = 0; index < iterations; ++index) {
      resumes_->fetch_add(1, std::memory_order_relaxed);
      co_await context.yield();
    }
    co_return ActorResult::success();
  }

private:
  std::shared_ptr<std::atomic_size_t> resumes_;
};

class OperationalMetricsActor final : public IActorV2 {
public:
  explicit OperationalMetricsActor(
      std::vector<std::shared_ptr<ManualActorEvent>> gates)
      : gates_(std::move(gates)) {}

  [[nodiscard]] auto get_name() const -> std::string override {
    return "metrics";
  }
  [[nodiscard]] auto get_version() const -> std::string override {
    return "test";
  }

  auto handle_message(const MessageEnvelope &message, ActorContext &)
      -> ActorTask<ActorResult> override {
    if (message.payload.value("slow", false)) {
      std::this_thread::sleep_for(5ms);
    }
    if (message.payload.contains("gate")) {
      co_await gates_.at(message.payload.at("gate").get<size_t>())->wait();
    }
    if (message.payload.value("fail", false)) {
      co_return ActorResult::failed("metrics_failure", "forced failure", false);
    }
    co_return ActorResult::success();
  }

private:
  std::vector<std::shared_ptr<ManualActorEvent>> gates_;
};

auto immediate_success_task() -> ActorTask<ActorResult> {
  co_return ActorResult::success();
}

class SynchronousBoundaryActor final : public IActorV2 {
public:
  [[nodiscard]] auto get_name() const -> std::string override {
    return "sync-boundary";
  }
  [[nodiscard]] auto get_version() const -> std::string override {
    return "test";
  }

  auto handle_message(const MessageEnvelope &message, ActorContext &)
      -> ActorTask<ActorResult> override {
    if (message.id == "throw") {
      throw std::runtime_error("synchronous actor failure");
    }
    return immediate_success_task();
  }
};

class ReentrantBoundaryActor final : public IActorV2 {
public:
  explicit ReentrantBoundaryActor(NativeActorScheduler &scheduler,
                                  NativeActorScheduler::Completion completion)
      : scheduler_(scheduler), completion_(std::move(completion)) {}

  [[nodiscard]] auto get_name() const -> std::string override {
    return "reentrant-boundary";
  }
  [[nodiscard]] auto get_version() const -> std::string override {
    return "test";
  }

  auto handle_message(const MessageEnvelope &message, ActorContext &)
      -> ActorTask<ActorResult> override {
    if (message.id == "outer") {
      ActorInvocation nested;
      nested.actor_id = "reentrant-boundary";
      nested.partition_key = "same";
      nested.message.id = "inner";
      nested.message.type = "NativeTest";
      if (!scheduler_.enqueue(std::move(nested), std::move(completion_))) {
        throw std::runtime_error("reentrant enqueue was rejected");
      }
    }
    return immediate_success_task();
  }

private:
  NativeActorScheduler &scheduler_;
  NativeActorScheduler::Completion completion_;
};

auto nested_io_task(ManualActorEvent &event,
                    const std::shared_ptr<std::atomic_bool> &started)
    -> ActorTask<ActorResult> {
  started->store(true, std::memory_order_release);
  co_await event.wait();
  co_return ActorResult::success();
}

class NestedIoActor final : public IActorV2 {
public:
  NestedIoActor(std::shared_ptr<ManualActorEvent> direct,
                std::shared_ptr<ManualActorEvent> nested,
                std::shared_ptr<std::atomic_bool> nested_started)
      : direct_(std::move(direct)), nested_(std::move(nested)),
        nested_started_(std::move(nested_started)) {}

  [[nodiscard]] auto get_name() const -> std::string override {
    return "nested-io";
  }
  [[nodiscard]] auto get_version() const -> std::string override {
    return "test";
  }

  auto handle_message(const MessageEnvelope &, ActorContext &)
      -> ActorTask<ActorResult> override {
    co_await direct_->wait();
    auto nested = nested_io_task(*nested_, nested_started_);
    nested.attach_runtime(co_await current_actor_task_runtime());
    while (!nested.done()) {
      nested.resume();
      if (!nested.done()) {
        co_await forward_actor_task_suspension(nested.suspension(),
                                               nested.io_suspension_epoch());
      }
    }
    co_return nested.take_result();
  }

private:
  std::shared_ptr<ManualActorEvent> direct_;
  std::shared_ptr<ManualActorEvent> nested_;
  std::shared_ptr<std::atomic_bool> nested_started_;
};

struct SlowDiagnosticProbe {
  std::mutex mutex;
  std::condition_variable changed;
  std::vector<NativeActorSlowResumeDiagnostic> diagnostics;

  void record(NativeActorSlowResumeDiagnostic diagnostic) {
    std::scoped_lock lock(mutex);
    diagnostics.push_back(std::move(diagnostic));
    changed.notify_all();
  }

  auto wait_for_diagnostic(std::string_view partition_key)
      -> NativeActorSlowResumeDiagnostic {
    std::unique_lock lock(mutex);
    const auto matches = [this, partition_key] {
      return std::ranges::any_of(
          diagnostics, [partition_key](const auto &diagnostic) {
            return diagnostic.partition_key == partition_key;
          });
    };
    if (!changed.wait_for(lock, 2s, matches)) {
      throw std::runtime_error("slow actor diagnostic was not emitted");
    }
    return *std::ranges::find_if(
        diagnostics, [partition_key](const auto &diagnostic) {
          return diagnostic.partition_key == partition_key;
        });
  }
};

auto histogram_bucket_total(const NativeActorDurationHistogram &histogram)
    -> uint64_t {
  return std::accumulate(histogram.buckets.begin(), histogram.buckets.end(),
                         uint64_t{0});
}

auto invocation(std::string id, std::string partition, bool wait = false,
                bool yield = false) -> ActorInvocation {
  MessageEnvelope message;
  message.id = std::move(id);
  message.type = "NativeTest";
  message.payload["wait"] = wait;
  message.payload["yield"] = yield;
  return ActorInvocation{.actor_id = "native",
                         .partition_key = std::move(partition),
                         .message = std::move(message)};
}

auto actor_invocation(std::string actor_id, std::string id,
                      std::string partition) -> ActorInvocation {
  auto result = invocation(std::move(id), std::move(partition));
  result.actor_id = std::move(actor_id);
  return result;
}

auto future_completion(std::promise<ActorResult> &promise)
    -> NativeActorScheduler::Completion {
  return
      [&promise](ActorResult result) { promise.set_value(std::move(result)); };
}

template <typename Predicate>
auto wait_until(Predicate predicate,
                const std::chrono::steady_clock::duration timeout = 2s)
    -> bool {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (!predicate()) {
    if (std::chrono::steady_clock::now() >= deadline) {
      return false;
    }
    std::this_thread::sleep_for(1ms);
  }
  return true;
}

TEST(NativeActorSchedulerTest, SerializesSameMailboxAcrossIoSuspension) {
  auto probe = std::make_shared<NativeProbe>();
  auto gate = std::make_shared<ManualActorEvent>();
  NativeActorScheduler scheduler(
      NativeActorSchedulerOptions{.worker_count = 2});
  scheduler.register_actor(std::make_shared<NativeProbeActor>(probe, gate));

  std::promise<ActorResult> first;
  std::promise<ActorResult> second;
  auto first_result = first.get_future();
  auto second_result = second.get_future();
  ASSERT_TRUE(scheduler.enqueue(invocation("first", "same", true),
                                future_completion(first)));
  {
    std::unique_lock lock(probe->mutex);
    ASSERT_TRUE(probe->changed.wait_for(
        lock, 2s, [&probe] { return probe->events.size() == 1; }));
  }
  ASSERT_TRUE(scheduler.enqueue(invocation("second", "same"),
                                future_completion(second)));
  EXPECT_EQ(second_result.wait_for(20ms), std::future_status::timeout);

  gate->set();
  EXPECT_TRUE(first_result.get().ok());
  EXPECT_TRUE(second_result.get().ok());
  scheduler.shutdown();

  EXPECT_EQ(probe->max_active, 1);
  EXPECT_EQ(probe->events,
            (std::vector<std::string>{"start:first", "finish:first",
                                      "start:second", "finish:second"}));
}

TEST(NativeActorSchedulerTest, ReleasesActorAliasesAfterShutdown) {
  auto destruction_count = std::make_shared<std::atomic_size_t>(0);
  NativeActorScheduler scheduler(
      NativeActorSchedulerOptions{.worker_count = 1});
  scheduler.register_actor(
      std::make_shared<DestructionProbeActor>(destruction_count));

  scheduler.shutdown(ActorExecutorShutdownMode::Drain);
  EXPECT_EQ(destruction_count->load(std::memory_order_relaxed), 0);

  scheduler.release_actors();
  EXPECT_EQ(destruction_count->load(std::memory_order_relaxed), 1);
}

TEST(NativeActorSchedulerTest,
     SynchronousHandleThrowFailsInvocationAndMailboxRecovers) {
  NativeActorScheduler scheduler(
      NativeActorSchedulerOptions{.worker_count = 1});
  scheduler.register_actor(std::make_shared<SynchronousBoundaryActor>());

  std::promise<ActorResult> failed;
  std::promise<ActorResult> recovered;
  auto failed_result = failed.get_future();
  auto recovered_result = recovered.get_future();
  ASSERT_TRUE(
      scheduler.enqueue(actor_invocation("sync-boundary", "throw", "same"),
                        future_completion(failed)));
  ASSERT_TRUE(
      scheduler.enqueue(actor_invocation("sync-boundary", "recover", "same"),
                        future_completion(recovered)));

  ASSERT_EQ(failed_result.wait_for(2s), std::future_status::ready);
  ASSERT_EQ(recovered_result.wait_for(2s), std::future_status::ready);
  const auto failure = failed_result.get();
  ASSERT_FALSE(failure.ok());
  ASSERT_TRUE(failure.failure.has_value());
  EXPECT_EQ(failure.failure->code, "actor_exception");
  EXPECT_EQ(failure.failure->message, "synchronous actor failure");
  EXPECT_TRUE(recovered_result.get().ok());

  scheduler.shutdown(ActorExecutorShutdownMode::Drain);
  EXPECT_EQ(scheduler.metrics().pending, 0);
}

TEST(NativeActorSchedulerTest, SynchronousHandleCanReenterEnqueue) {
  NativeActorScheduler scheduler(
      NativeActorSchedulerOptions{.worker_count = 1});
  std::promise<ActorResult> nested;
  std::promise<ActorResult> outer;
  auto nested_result = nested.get_future();
  auto outer_result = outer.get_future();
  scheduler.register_actor(std::make_shared<ReentrantBoundaryActor>(
      scheduler, future_completion(nested)));

  ASSERT_TRUE(
      scheduler.enqueue(actor_invocation("reentrant-boundary", "outer", "same"),
                        future_completion(outer)));
  ASSERT_EQ(outer_result.wait_for(2s), std::future_status::ready);
  ASSERT_EQ(nested_result.wait_for(2s), std::future_status::ready);
  EXPECT_TRUE(outer_result.get().ok());
  EXPECT_TRUE(nested_result.get().ok());
}

TEST(NativeActorSchedulerTest, NestedIoUsesInvocationWideSuspensionEpochs) {
  auto direct = std::make_shared<ManualActorEvent>();
  auto nested = std::make_shared<ManualActorEvent>();
  auto nested_started = std::make_shared<std::atomic_bool>(false);
  NativeActorScheduler scheduler(
      NativeActorSchedulerOptions{.worker_count = 1});
  scheduler.register_actor(
      std::make_shared<NestedIoActor>(direct, nested, nested_started));

  std::promise<ActorResult> completion;
  auto result = completion.get_future();
  ASSERT_TRUE(
      scheduler.enqueue(actor_invocation("nested-io", "nested-io", "same"),
                        future_completion(completion)));
  ASSERT_TRUE(wait_until(
      [&scheduler] { return scheduler.metrics().suspended_mailboxes == 1; }));

  direct->set();
  ASSERT_TRUE(wait_until([&nested_started] {
    return nested_started->load(std::memory_order_acquire);
  }));
  nested->set();

  ASSERT_EQ(result.wait_for(2s), std::future_status::ready);
  EXPECT_TRUE(result.get().ok());
  EXPECT_EQ(scheduler.metrics().pending, 0);
}

TEST(NativeActorSchedulerTest, UnrelatedMailboxesRunWithoutHashShardBlocking) {
  auto probe = std::make_shared<NativeProbe>();
  auto gate = std::make_shared<ManualActorEvent>();
  NativeActorScheduler scheduler(
      NativeActorSchedulerOptions{.worker_count = 2});
  scheduler.register_actor(std::make_shared<NativeProbeActor>(probe, gate));

  std::promise<ActorResult> first;
  std::promise<ActorResult> second;
  ASSERT_TRUE(scheduler.enqueue(invocation("one", "collision-a", true),
                                future_completion(first)));
  ASSERT_TRUE(scheduler.enqueue(invocation("two", "collision-b", true),
                                future_completion(second)));
  {
    std::unique_lock lock(probe->mutex);
    ASSERT_TRUE(probe->changed.wait_for(
        lock, 2s, [&probe] { return probe->active == 2; }));
  }
  EXPECT_EQ(probe->max_active, 2);

  gate->set();
  EXPECT_TRUE(first.get_future().get().ok());
  EXPECT_TRUE(second.get_future().get().ok());
  scheduler.shutdown();
}

TEST(NativeActorSchedulerTest, SuspendedContinuationCanMigrateByStealing) {
  auto migration = std::make_shared<MigrationProbe>();
  auto gate = std::make_shared<ManualActorEvent>();
  auto first_blocker = std::make_shared<BlockingControl>();
  auto second_blocker = std::make_shared<BlockingControl>();
  NativeActorScheduler scheduler(
      NativeActorSchedulerOptions{.worker_count = 2});
  scheduler.register_actor(std::make_shared<MigrationActor>(migration, gate));
  scheduler.register_actor(std::make_shared<BlockingActor>(
      std::vector{first_blocker, second_blocker}));

  std::promise<ActorResult> migrated_promise;
  auto migrated_result = migrated_promise.get_future();
  ASSERT_TRUE(
      scheduler.enqueue(actor_invocation("migration", "migrate", "mailbox"),
                        future_completion(migrated_promise)));
  ASSERT_TRUE(wait_until(
      [&scheduler] { return scheduler.metrics().suspended_mailboxes == 1; }));

  size_t original_worker = ACTOR_TASK_NO_PREFERRED_WORKER;
  {
    std::scoped_lock lock(migration->mutex);
    ASSERT_EQ(migration->workers.size(), 1);
    original_worker = migration->workers.front();
  }

  std::promise<ActorResult> first_blocker_promise;
  auto first_blocker_result = first_blocker_promise.get_future();
  auto first = actor_invocation("blocker", "block-0", "block-0");
  first.message.payload["control"] = 0;
  ASSERT_TRUE(scheduler.enqueue(std::move(first),
                                future_completion(first_blocker_promise)));
  const auto first_worker = first_blocker->wait_for_start();

  std::promise<ActorResult> second_blocker_promise;
  auto second_blocker_result = second_blocker_promise.get_future();
  std::shared_ptr<BlockingControl> original_worker_blocker = first_blocker;
  if (first_worker != original_worker) {
    auto second = actor_invocation("blocker", "block-1", "block-1");
    second.message.payload["control"] = 1;
    ASSERT_TRUE(scheduler.enqueue(std::move(second),
                                  future_completion(second_blocker_promise)));
    ASSERT_EQ(second_blocker->wait_for_start(), original_worker);
    original_worker_blocker = second_blocker;
    first_blocker->release();
    ASSERT_TRUE(first_blocker_result.get().ok());
  }

  gate->set();
  ASSERT_TRUE(migrated_result.get().ok());
  {
    std::scoped_lock lock(migration->mutex);
    ASSERT_EQ(migration->workers.size(), 2);
    EXPECT_EQ(migration->workers.front(), original_worker);
    EXPECT_NE(migration->workers.back(), original_worker);
  }
  EXPECT_GT(scheduler.metrics().executor.successful_steals, 0);
  EXPECT_GE(scheduler.metrics().executor.steal_attempts,
            scheduler.metrics().executor.successful_steals);
  EXPECT_EQ(scheduler.metrics().executor.worker_queue_depths.size(), 2);

  original_worker_blocker->release();
  if (first_worker == original_worker) {
    EXPECT_TRUE(first_blocker_result.get().ok());
  } else {
    EXPECT_TRUE(second_blocker_result.get().ok());
  }
  scheduler.shutdown();
}

TEST(NativeActorSchedulerTest,
     OperationalMetricsTrackLifecycleLatencyAndPrivateDiagnostics) {
  auto first_gate = std::make_shared<ManualActorEvent>();
  auto cancellation_gate = std::make_shared<ManualActorEvent>();
  auto diagnostic_probe = std::make_shared<SlowDiagnosticProbe>();
  NativeActorSchedulerOptions options;
  options.worker_count = 2;
  options.slow_resume_warning_ms = 1;
  options.slow_resume_handler =
      [diagnostic_probe](NativeActorSlowResumeDiagnostic diagnostic) {
        diagnostic_probe->record(std::move(diagnostic));
      };
  NativeActorScheduler scheduler(options);
  scheduler.register_actor(std::make_shared<OperationalMetricsActor>(
      std::vector{first_gate, cancellation_gate}));

  std::promise<ActorResult> suspended_promise;
  auto suspended_result = suspended_promise.get_future();
  auto suspended = actor_invocation("metrics", "suspended", "io-partition");
  suspended.message.payload["gate"] = 0;
  ASSERT_TRUE(scheduler.enqueue(std::move(suspended),
                                future_completion(suspended_promise)));
  ASSERT_TRUE(wait_until([&scheduler] {
    const auto metrics = scheduler.metrics();
    return metrics.suspended == 1 && metrics.running == 0 &&
           metrics.runnable == 0;
  }));
  EXPECT_GE(scheduler.metrics().suspensions, 1);

  first_gate->set();
  EXPECT_TRUE(suspended_result.get().ok());

  std::promise<ActorResult> slow_promise;
  auto slow_result = slow_promise.get_future();
  auto slow = actor_invocation("metrics", "slow", "slow-partition");
  slow.message.payload["slow"] = true;
  slow.message.payload["secret"] = "payload-must-not-appear";
  slow.message.raw["secret"] = "raw-must-not-appear";
  ASSERT_TRUE(
      scheduler.enqueue(std::move(slow), future_completion(slow_promise)));
  EXPECT_TRUE(slow_result.get().ok());

  const auto diagnostic =
      diagnostic_probe->wait_for_diagnostic("slow-partition");
  EXPECT_GT(diagnostic.task_id, 0);
  EXPECT_EQ(diagnostic.actor_id, "metrics");
  EXPECT_EQ(diagnostic.partition_key, "slow-partition");
  EXPECT_GT(diagnostic.mailbox_generation, 0);
  EXPECT_LT(diagnostic.worker_id, 2);
  EXPECT_GE(diagnostic.elapsed_ns,
            std::chrono::duration_cast<std::chrono::nanoseconds>(1ms).count());
  const auto diagnostic_text = format_native_actor_slow_resume(diagnostic);
  EXPECT_EQ(diagnostic_text.find("payload-must-not-appear"), std::string::npos);
  EXPECT_EQ(diagnostic_text.find("raw-must-not-appear"), std::string::npos);

  std::promise<ActorResult> failed_promise;
  auto failed_result = failed_promise.get_future();
  auto failed = actor_invocation("metrics", "failed", "fail-partition");
  failed.message.payload["fail"] = true;
  ASSERT_TRUE(
      scheduler.enqueue(std::move(failed), future_completion(failed_promise)));
  ASSERT_FALSE(failed_result.get().ok());

  std::promise<ActorResult> cancelled_promise;
  auto cancelled_result = cancelled_promise.get_future();
  auto cancelled = actor_invocation("metrics", "cancelled", "cancel-partition");
  cancelled.message.payload["gate"] = 1;
  ASSERT_TRUE(scheduler.enqueue(std::move(cancelled),
                                future_completion(cancelled_promise)));
  ASSERT_TRUE(
      wait_until([&scheduler] { return scheduler.metrics().suspended == 1; }));
  scheduler.shutdown(ActorExecutorShutdownMode::Cancel);
  ASSERT_FALSE(cancelled_result.get().ok());

  const auto metrics = scheduler.metrics();
  EXPECT_EQ(metrics.accepted, 4);
  EXPECT_EQ(metrics.rejected, 0);
  EXPECT_EQ(metrics.completed, 2);
  EXPECT_EQ(metrics.failed, 1);
  EXPECT_EQ(metrics.cancelled, 1);
  EXPECT_EQ(metrics.pending, 0);
  EXPECT_EQ(metrics.runnable, 0);
  EXPECT_EQ(metrics.running, 0);
  EXPECT_EQ(metrics.suspended, 0);
  EXPECT_GE(metrics.runnable_transitions, 4);
  EXPECT_GE(metrics.running_resumes, 5);
  EXPECT_GE(metrics.suspensions, 2);
  EXPECT_GE(metrics.slow_resumes, 1);
  EXPECT_EQ(metrics.mailbox_queue_delay.count, 4);
  EXPECT_GE(metrics.actor_resume_duration.count, 5);
  EXPECT_EQ(metrics.invocation_latency.count, 4);
  EXPECT_EQ(histogram_bucket_total(metrics.mailbox_queue_delay),
            metrics.mailbox_queue_delay.count);
  EXPECT_EQ(histogram_bucket_total(metrics.actor_resume_duration),
            metrics.actor_resume_duration.count);
  EXPECT_EQ(histogram_bucket_total(metrics.invocation_latency),
            metrics.invocation_latency.count);
  EXPECT_EQ(metrics.executor.worker_queue_depths.size(), 2);

  cancellation_gate->set();
}

TEST(NativeActorSchedulerTest, SuspendedInvocationConsumesBackpressure) {
  auto probe = std::make_shared<NativeProbe>();
  auto gate = std::make_shared<ManualActorEvent>();
  NativeActorScheduler scheduler(NativeActorSchedulerOptions{
      .worker_count = 2,
      .max_pending_tasks_per_partition = 1,
  });
  scheduler.register_actor(std::make_shared<NativeProbeActor>(probe, gate));

  std::promise<ActorResult> first;
  std::promise<ActorResult> rejected;
  ASSERT_TRUE(scheduler.enqueue(invocation("first", "same", true),
                                future_completion(first)));
  {
    std::unique_lock lock(probe->mutex);
    ASSERT_TRUE(probe->changed.wait_for(
        lock, 2s, [&probe] { return probe->active == 1; }));
  }
  EXPECT_FALSE(scheduler.enqueue(invocation("second", "same"),
                                 future_completion(rejected)));
  const auto rejected_result = rejected.get_future().get();
  ASSERT_FALSE(rejected_result.ok());
  EXPECT_EQ(rejected_result.failure->code, "scheduler_backpressure");

  gate->set();
  EXPECT_TRUE(first.get_future().get().ok());
  scheduler.shutdown();
  EXPECT_EQ(scheduler.metrics().pending, 0);
}

TEST(NativeActorSchedulerTest, RequeuesYieldAndCompletesExactlyOnce) {
  auto probe = std::make_shared<NativeProbe>();
  NativeActorScheduler scheduler(
      NativeActorSchedulerOptions{.worker_count = 2});
  scheduler.register_actor(std::make_shared<NativeProbeActor>(probe));

  std::atomic_int completions = 0;
  std::promise<ActorResult> result;
  ASSERT_TRUE(
      scheduler.enqueue(invocation("yield", "partition", false, true),
                        [&result, &completions](ActorResult actor_result) {
                          completions.fetch_add(1);
                          result.set_value(std::move(actor_result));
                        }));

  EXPECT_TRUE(result.get_future().get().ok());
  scheduler.shutdown();
  EXPECT_EQ(completions.load(), 1);
  EXPECT_EQ(scheduler.metrics().yielded, 1);
}

TEST(NativeActorSchedulerTest,
     RecurrentMailboxHandsCompletionEnqueuesToLocalWorker) {
  auto probe = std::make_shared<NativeProbe>();
  NativeActorScheduler scheduler(
      NativeActorSchedulerOptions{.worker_count = 2});
  scheduler.register_actor(std::make_shared<NativeProbeActor>(probe));

  constexpr size_t invocation_count = 128;
  std::atomic_size_t completions = 0;
  std::atomic_size_t failures = 0;
  std::promise<void> all_done;
  auto completion = std::make_shared<NativeActorScheduler::Completion>();
  *completion = [&scheduler, completion, &completions, &failures,
                 &all_done](ActorResult result) {
    if (!result.ok()) {
      failures.fetch_add(1, std::memory_order_relaxed);
    }
    const auto completed =
        completions.fetch_add(1, std::memory_order_acq_rel) + 1;
    if (completed == invocation_count) {
      all_done.set_value();
      return;
    }
    const auto accepted = scheduler.enqueue(
        invocation("handoff-" + std::to_string(completed), "same"),
        *completion);
    if (!accepted) {
      failures.fetch_add(1, std::memory_order_relaxed);
      all_done.set_value();
    }
  };

  ASSERT_TRUE(scheduler.enqueue(invocation("handoff-0", "same"), *completion));
  ASSERT_EQ(all_done.get_future().wait_for(5s), std::future_status::ready);
  scheduler.shutdown();

  const auto metrics = scheduler.metrics();
  EXPECT_EQ(completions.load(), invocation_count);
  EXPECT_EQ(failures.load(), 0);
  EXPECT_EQ(metrics.completed, invocation_count);
  EXPECT_EQ(metrics.running_resumes, invocation_count);
  EXPECT_EQ(metrics.mailbox_queue_delay.count, invocation_count);
  EXPECT_LT(metrics.executor.executed, invocation_count / 2);
  EXPECT_EQ(metrics.executor.executed, metrics.executor.work_items_created);
  EXPECT_EQ(metrics.pending, 0);
  EXPECT_EQ(metrics.running, 0);
  *completion = {};
}

TEST(NativeActorSchedulerTest, DuplicateIoNotificationDoesNotDoubleResume) {
  auto probe = std::make_shared<NativeProbe>();
  auto gate = std::make_shared<ManualActorEvent>();
  NativeActorScheduler scheduler(
      NativeActorSchedulerOptions{.worker_count = 2});
  scheduler.register_actor(std::make_shared<NativeProbeActor>(probe, gate));

  std::atomic_int completions = 0;
  std::promise<ActorResult> result;
  ASSERT_TRUE(
      scheduler.enqueue(invocation("duplicate", "partition", true),
                        [&result, &completions](ActorResult actor_result) {
                          completions.fetch_add(1);
                          result.set_value(std::move(actor_result));
                        }));
  {
    std::unique_lock lock(probe->mutex);
    ASSERT_TRUE(probe->changed.wait_for(
        lock, 2s, [&probe] { return probe->active == 1; }));
  }
  ASSERT_TRUE(wait_until(
      [&scheduler] { return scheduler.metrics().suspended_mailboxes == 1; }));

  gate->set(true);
  EXPECT_TRUE(result.get_future().get().ok());
  scheduler.shutdown();
  EXPECT_EQ(completions.load(), 1);
  EXPECT_GE(scheduler.metrics().late_completions, 1);
}

TEST(NativeActorSchedulerTest,
     CancellingShutdownCompletesSuspendedAndQueuedWork) {
  auto probe = std::make_shared<NativeProbe>();
  auto gate = std::make_shared<ManualActorEvent>();
  NativeActorScheduler scheduler(
      NativeActorSchedulerOptions{.worker_count = 2});
  scheduler.register_actor(std::make_shared<NativeProbeActor>(probe, gate));

  std::promise<ActorResult> active;
  std::promise<ActorResult> queued;
  ASSERT_TRUE(scheduler.enqueue(invocation("active", "same", true),
                                future_completion(active)));
  ASSERT_TRUE(scheduler.enqueue(invocation("queued", "same"),
                                future_completion(queued)));
  {
    std::unique_lock lock(probe->mutex);
    ASSERT_TRUE(probe->changed.wait_for(
        lock, 2s, [&probe] { return probe->active == 1; }));
  }

  scheduler.shutdown(ActorExecutorShutdownMode::Cancel);
  const auto active_result = active.get_future().get();
  const auto queued_result = queued.get_future().get();
  ASSERT_FALSE(active_result.ok());
  ASSERT_FALSE(queued_result.ok());
  EXPECT_EQ(active_result.failure->code, "scheduler_cancelled");
  EXPECT_EQ(queued_result.failure->code, "scheduler_cancelled");
  EXPECT_EQ(scheduler.metrics().pending, 0);

  gate->set();
  EXPECT_EQ(scheduler.metrics().pending, 0);
}

TEST(NativeActorSchedulerTest, SeededManyMailboxStressLosesNoInvocations) {
  auto probe = std::make_shared<NativeProbe>();
  NativeActorScheduler scheduler(
      NativeActorSchedulerOptions{.worker_count = 4});
  scheduler.register_actor(std::make_shared<NativeProbeActor>(probe));

  constexpr size_t task_count = 4000;
  std::atomic_size_t completed = 0;
  std::atomic_size_t failures = 0;
  std::promise<void> all_done;
  for (size_t index = 0; index < task_count; ++index) {
    ASSERT_TRUE(scheduler.enqueue(
        invocation("stress-" + std::to_string(index),
                   "partition-" + std::to_string((index * 17) % 257), false,
                   index % 7 == 0),
        [&completed, &failures, &all_done](ActorResult result) {
          if (!result.ok()) {
            failures.fetch_add(1);
          }
          if (completed.fetch_add(1) + 1 == task_count) {
            all_done.set_value();
          }
        }));
  }

  EXPECT_EQ(all_done.get_future().wait_for(10s), std::future_status::ready);
  scheduler.shutdown();
  EXPECT_EQ(completed.load(), task_count);
  EXPECT_EQ(failures.load(), 0);
  EXPECT_EQ(scheduler.metrics().pending, 0);
}

TEST(NativeActorSchedulerTest,
     ReclaimsIdlePartitionMailboxesAndPreservesTheirMetrics) {
  auto probe = std::make_shared<NativeProbe>();
  NativeActorScheduler scheduler(NativeActorSchedulerOptions{
      .worker_count = 4,
      .max_pending_tasks_per_partition = 2,
  });
  scheduler.register_actor(std::make_shared<NativeProbeActor>(probe));

  constexpr size_t partition_count = 256;
  std::atomic_size_t completed = 0;
  std::promise<void> all_done;
  for (size_t index = 0; index < partition_count; ++index) {
    ASSERT_TRUE(scheduler.enqueue(
        invocation("reclaim-" + std::to_string(index),
                   "partition-" + std::to_string(index)),
        [&completed, &all_done](ActorResult result) {
          EXPECT_TRUE(result.ok());
          if (completed.fetch_add(1, std::memory_order_acq_rel) + 1 ==
              partition_count) {
            all_done.set_value();
          }
        }));
  }

  ASSERT_EQ(all_done.get_future().wait_for(5s), std::future_status::ready);
  ASSERT_TRUE(wait_until([&scheduler] {
    const auto metrics = scheduler.metrics();
    return metrics.idle_mailboxes == 0 && metrics.runnable_mailboxes == 0 &&
           metrics.running_mailboxes == 0 && metrics.suspended_mailboxes == 0;
  }));

  const auto metrics = scheduler.metrics();
  EXPECT_EQ(metrics.completed, partition_count);
  EXPECT_EQ(metrics.pending, 0);
  EXPECT_EQ(metrics.mailbox_queue_delay.count, partition_count);
  EXPECT_EQ(metrics.invocation_latency.count, partition_count);
  scheduler.shutdown();
}

TEST(NativeActorSchedulerTest, SeededEnqueueVersusCompletionRaceLosesNoWork) {
  constexpr uint32_t seed = 0x4f424358U;
  SCOPED_TRACE(::testing::Message() << "seed=" << seed);
  std::minstd_rand random(seed);
  auto probe = std::make_shared<NativeProbe>();
  auto gate = std::make_shared<ManualActorEvent>();
  NativeActorScheduler scheduler(
      NativeActorSchedulerOptions{.worker_count = 4});
  scheduler.register_actor(std::make_shared<NativeProbeActor>(probe, gate));

  constexpr size_t queued_count = 500;
  std::atomic_size_t completions = 0;
  std::atomic_bool start = false;
  std::promise<void> all_done;
  ASSERT_TRUE(scheduler.enqueue(invocation("race-active", "same", true),
                                [&completions, &all_done](ActorResult result) {
                                  EXPECT_TRUE(result.ok());
                                  if (completions.fetch_add(1) + 1 ==
                                      queued_count + 1) {
                                    all_done.set_value();
                                  }
                                }));
  ASSERT_TRUE(wait_until(
      [&scheduler] { return scheduler.metrics().suspended_mailboxes == 1; }));

  std::thread producer([&] {
    while (!start.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
    for (size_t index = 0; index < queued_count; ++index) {
      ASSERT_TRUE(scheduler.enqueue(
          invocation("race-" + std::to_string(index), "same"),
          [&completions, &all_done](ActorResult result) {
            EXPECT_TRUE(result.ok());
            if (completions.fetch_add(1) + 1 == queued_count + 1) {
              all_done.set_value();
            }
          }));
    }
  });
  const auto completion_delay = std::chrono::microseconds(random() % 200);
  std::thread completer([&, completion_delay] {
    while (!start.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
    std::this_thread::sleep_for(completion_delay);
    gate->set();
  });
  start.store(true, std::memory_order_release);
  producer.join();
  completer.join();

  EXPECT_EQ(all_done.get_future().wait_for(10s), std::future_status::ready);
  scheduler.shutdown();
  EXPECT_EQ(completions.load(), queued_count + 1);
  EXPECT_EQ(probe->max_active, 1);
  EXPECT_EQ(scheduler.metrics().pending, 0);
}

TEST(NativeActorSchedulerTest,
     SeededCancellationVersusCompletionRaceCompletesExactlyOnce) {
  constexpr uint32_t seed = 0x43414e43U;
  std::minstd_rand random(seed);
  for (size_t iteration = 0; iteration < 25; ++iteration) {
    SCOPED_TRACE(::testing::Message()
                 << "seed=" << seed << " iteration=" << iteration);
    auto probe = std::make_shared<NativeProbe>();
    auto gate = std::make_shared<ManualActorEvent>();
    NativeActorScheduler scheduler(
        NativeActorSchedulerOptions{.worker_count = 2});
    scheduler.register_actor(std::make_shared<NativeProbeActor>(probe, gate));

    std::promise<ActorResult> promise;
    auto future = promise.get_future();
    std::atomic_int completions = 0;
    ASSERT_TRUE(scheduler.enqueue(invocation("cancel-race", "same", true),
                                  [&promise, &completions](ActorResult result) {
                                    completions.fetch_add(1);
                                    promise.set_value(std::move(result));
                                  }));
    ASSERT_TRUE(wait_until(
        [&scheduler] { return scheduler.metrics().suspended_mailboxes == 1; }));

    std::atomic_bool start = false;
    const auto completion_delay = std::chrono::microseconds(random() % 100);
    const auto shutdown_delay = std::chrono::microseconds(random() % 100);
    std::thread completer([&, completion_delay] {
      while (!start.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      std::this_thread::sleep_for(completion_delay);
      gate->set(true);
    });
    std::thread canceller([&, shutdown_delay] {
      while (!start.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      std::this_thread::sleep_for(shutdown_delay);
      scheduler.shutdown(ActorExecutorShutdownMode::Cancel);
    });
    start.store(true, std::memory_order_release);
    completer.join();
    canceller.join();

    ASSERT_EQ(future.wait_for(2s), std::future_status::ready);
    const auto result = future.get();
    if (!result.ok()) {
      ASSERT_TRUE(result.failure.has_value());
      EXPECT_EQ(result.failure->code, "scheduler_cancelled");
    }
    EXPECT_EQ(completions.load(), 1);
    EXPECT_EQ(scheduler.metrics().pending, 0);
  }
}

TEST(NativeActorSchedulerTest,
     SeededShutdownVersusYieldRequeueCompletesExactlyOnce) {
  constexpr uint32_t seed = 0x5949454cU;
  std::minstd_rand random(seed);
  for (size_t iteration = 0; iteration < 25; ++iteration) {
    SCOPED_TRACE(::testing::Message()
                 << "seed=" << seed << " iteration=" << iteration);
    auto resumes = std::make_shared<std::atomic_size_t>(0);
    NativeActorScheduler scheduler(
        NativeActorSchedulerOptions{.worker_count = 2});
    scheduler.register_actor(std::make_shared<YieldLoopActor>(resumes));

    std::promise<ActorResult> promise;
    auto future = promise.get_future();
    std::atomic_int completions = 0;
    auto work = actor_invocation("yield-loop", "yield-race", "same");
    work.message.payload["iterations"] = 2000;
    ASSERT_TRUE(scheduler.enqueue(std::move(work),
                                  [&promise, &completions](ActorResult result) {
                                    completions.fetch_add(1);
                                    promise.set_value(std::move(result));
                                  }));
    ASSERT_TRUE(wait_until(
        [&resumes] { return resumes->load(std::memory_order_relaxed) > 0; }));

    std::this_thread::sleep_for(std::chrono::microseconds(random() % 100));
    scheduler.shutdown(ActorExecutorShutdownMode::Cancel);
    ASSERT_EQ(future.wait_for(2s), std::future_status::ready);
    const auto result = future.get();
    if (!result.ok()) {
      ASSERT_TRUE(result.failure.has_value());
      EXPECT_EQ(result.failure->code, "scheduler_cancelled");
    }
    EXPECT_EQ(completions.load(), 1);
    EXPECT_EQ(scheduler.metrics().pending, 0);
  }
}

} // namespace
} // namespace obcx::core
