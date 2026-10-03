#include "core/actor/actor_work_stealing_executor.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace obcx::core {
namespace {

using namespace std::chrono_literals;

TEST(ActorWorkStealingExecutorTest, ExecutesEveryInjectedItemExactlyOnce) {
  ActorWorkStealingExecutor executor(
      ActorWorkStealingExecutorOptions{.worker_count = 4});
  constexpr size_t task_count = 2000;
  std::vector<std::atomic_uint> executions(task_count);

  for (size_t index = 0; index < task_count; ++index) {
    ASSERT_TRUE(executor.submit(
        [index, &executions](size_t) { executions[index].fetch_add(1); }));
  }
  executor.shutdown(ActorExecutorShutdownMode::Drain);

  for (const auto &execution : executions) {
    EXPECT_EQ(execution.load(), 1U);
  }
  const auto metrics = executor.metrics();
  EXPECT_EQ(metrics.submitted, task_count);
  EXPECT_EQ(metrics.executed, task_count);
  EXPECT_EQ(metrics.work_items_created, task_count);
  EXPECT_EQ(metrics.work_item_heap_allocations, 0);
  EXPECT_GE(metrics.work_notifications, 1);
  EXPECT_GE(metrics.shutdown_notifications, 1);
  EXPECT_EQ(metrics.failed, 0);
  EXPECT_EQ(metrics.runnable, 0);
  EXPECT_EQ(metrics.active, 0);
}

TEST(ActorWorkStealingExecutorTest, BurstSubmissionPreservesWakeAccounting) {
  ActorWorkStealingExecutor executor(
      ActorWorkStealingExecutorOptions{.worker_count = 2});
  const auto sleep_deadline = std::chrono::steady_clock::now() + 2s;
  while (executor.metrics().worker_sleeps < 2 &&
         std::chrono::steady_clock::now() < sleep_deadline) {
    std::this_thread::yield();
  }
  ASSERT_GE(executor.metrics().worker_sleeps, 2);

  constexpr size_t task_count = 2000;
  std::atomic_size_t executed = 0;
  for (size_t index = 0; index < task_count; ++index) {
    ASSERT_TRUE(
        executor.submit([&executed](size_t) { executed.fetch_add(1); }));
  }
  executor.shutdown(ActorExecutorShutdownMode::Drain);

  const auto metrics = executor.metrics();
  EXPECT_EQ(executed.load(), task_count);
  EXPECT_EQ(metrics.submitted, task_count);
  EXPECT_EQ(metrics.executed, task_count);
  EXPECT_EQ(metrics.work_items_created, task_count);
  EXPECT_EQ(metrics.work_item_heap_allocations, 0);
  EXPECT_GT(metrics.work_notifications, 0);
  EXPECT_LE(metrics.work_notifications, task_count);
  EXPECT_GE(metrics.worker_wakes, 1);
  EXPECT_GE(metrics.shutdown_notifications, 1);
  EXPECT_EQ(metrics.runnable, 0);
  EXPECT_EQ(metrics.active, 0);
}

TEST(ActorWorkStealingExecutorTest,
     CoalescesQueuedBurstNotificationsToWorkerCapacity) {
  ActorWorkStealingExecutor executor(ActorWorkStealingExecutorOptions{
      .worker_count = 3, .start_immediately = false});
  constexpr size_t task_count = 2000;
  std::atomic_size_t executed = 0;
  for (size_t index = 0; index < task_count; ++index) {
    ASSERT_TRUE(
        executor.submit([&executed](size_t) { executed.fetch_add(1); }));
  }

  auto metrics = executor.metrics();
  EXPECT_EQ(metrics.work_notifications, executor.worker_count());
  EXPECT_EQ(metrics.runnable, task_count);

  executor.start();
  executor.shutdown(ActorExecutorShutdownMode::Drain);
  metrics = executor.metrics();
  EXPECT_EQ(executed.load(), task_count);
  EXPECT_EQ(metrics.executed, task_count);
  EXPECT_EQ(metrics.runnable, 0);
  EXPECT_EQ(metrics.active, 0);
}

TEST(ActorWorkStealingExecutorTest,
     NoPreferenceSubmissionUsesGlobalInjectorBeforeStart) {
  ActorWorkStealingExecutor executor(ActorWorkStealingExecutorOptions{
      .worker_count = 3, .start_immediately = false});
  ASSERT_TRUE(executor.submit([](size_t) {}));

  const auto queued = executor.metrics();
  EXPECT_EQ(queued.injector_depth, 1);
  EXPECT_EQ(queued.worker_queue_depths, (std::vector<size_t>{0, 0, 0}));

  executor.start();
  executor.shutdown(ActorExecutorShutdownMode::Drain);
  EXPECT_EQ(executor.metrics().executed, 1);
}

TEST(ActorWorkStealingExecutorTest, StealsFromWorkerBlockedByRunningItem) {
  ActorWorkStealingExecutor executor(
      ActorWorkStealingExecutorOptions{.worker_count = 2});
  auto release_blocker = std::make_shared<std::promise<void>>();
  auto release_future = release_blocker->get_future().share();
  std::promise<size_t> blocker_worker_promise;

  ASSERT_TRUE(executor.submit(
      [&blocker_worker_promise, release_future](size_t worker) {
        blocker_worker_promise.set_value(worker);
        release_future.wait();
      },
      0));
  const auto blocker_worker = blocker_worker_promise.get_future().get();

  constexpr size_t task_count = 64;
  std::atomic_size_t completed = 0;
  std::atomic_bool ran_on_thief = false;
  std::promise<void> all_done;
  for (size_t index = 0; index < task_count; ++index) {
    ASSERT_TRUE(executor.submit(
        [blocker_worker, &completed, &ran_on_thief, &all_done](size_t worker) {
          if (worker != blocker_worker) {
            ran_on_thief.store(true, std::memory_order_relaxed);
          }
          if (completed.fetch_add(1) + 1 == task_count) {
            all_done.set_value();
          }
        },
        blocker_worker));
  }

  EXPECT_EQ(all_done.get_future().wait_for(5s), std::future_status::ready);
  EXPECT_TRUE(ran_on_thief.load());
  release_blocker->set_value();
  executor.shutdown(ActorExecutorShutdownMode::Drain);

  const auto metrics = executor.metrics();
  EXPECT_GT(metrics.steal_attempts, 0);
  EXPECT_GT(metrics.successful_steals, 0);
}

TEST(ActorWorkStealingExecutorTest, QueueSynchronizationPublishesStateToThief) {
  ActorWorkStealingExecutor executor(
      ActorWorkStealingExecutorOptions{.worker_count = 2});
  auto release_blocker = std::make_shared<std::promise<void>>();
  auto release_future = release_blocker->get_future().share();
  std::promise<size_t> blocker_worker_promise;
  ASSERT_TRUE(executor.submit(
      [&blocker_worker_promise, release_future](size_t worker) {
        blocker_worker_promise.set_value(worker);
        release_future.wait();
      },
      0));
  const auto blocker_worker = blocker_worker_promise.get_future().get();

  auto published = std::make_shared<std::string>("published-before-enqueue");
  std::promise<std::string> observed;
  ASSERT_TRUE(executor.submit(
      [published, &observed](size_t) { observed.set_value(*published); },
      blocker_worker));

  EXPECT_EQ(observed.get_future().get(), "published-before-enqueue");
  release_blocker->set_value();
  executor.shutdown(ActorExecutorShutdownMode::Drain);
}

TEST(ActorWorkStealingExecutorTest, SleepingWorkerWakesForNewInjection) {
  ActorWorkStealingExecutor executor(
      ActorWorkStealingExecutorOptions{.worker_count = 2});
  const auto deadline = std::chrono::steady_clock::now() + 2s;
  while (executor.metrics().worker_sleeps == 0 &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::yield();
  }
  ASSERT_GT(executor.metrics().worker_sleeps, 0);

  std::promise<void> ran;
  ASSERT_TRUE(executor.submit([&ran](size_t) { ran.set_value(); }));
  EXPECT_EQ(ran.get_future().wait_for(2s), std::future_status::ready);
  executor.shutdown(ActorExecutorShutdownMode::Drain);
  EXPECT_GT(executor.metrics().worker_wakes, 0);
}

TEST(ActorWorkStealingExecutorTest, DrainAllowsInternalRescheduling) {
  ActorWorkStealingExecutor executor(ActorWorkStealingExecutorOptions{
      .worker_count = 2, .start_immediately = false});
  auto executions = std::make_shared<std::atomic_int>(0);
  auto work = std::make_shared<std::function<void(size_t)>>();
  const std::weak_ptr<std::function<void(size_t)>> weak_work = work;
  *work = [&executor, executions, weak_work](size_t worker) {
    const auto count = executions->fetch_add(1) + 1;
    if (count < 10) {
      EXPECT_TRUE(executor.reschedule(
          [weak_work](size_t next_worker) {
            if (const auto next = weak_work.lock()) {
              (*next)(next_worker);
            }
          },
          worker));
    }
  };

  ASSERT_TRUE(executor.submit([work](size_t worker) { (*work)(worker); }, 0));
  executor.start();
  executor.shutdown(ActorExecutorShutdownMode::Drain);

  EXPECT_EQ(executions->load(), 10);
  EXPECT_EQ(executor.metrics().rescheduled, 9);
  EXPECT_FALSE(executor.submit([](size_t) {}));
}

TEST(ActorWorkStealingExecutorTest, DrainWaitsForLastActiveWorkItem) {
  ActorWorkStealingExecutor executor(
      ActorWorkStealingExecutorOptions{.worker_count = 1});
  std::promise<void> started;
  auto release = std::make_shared<std::promise<void>>();
  auto release_future = release->get_future().share();
  ASSERT_TRUE(executor.submit([&started, release_future](size_t) {
    started.set_value();
    release_future.wait();
  }));
  ASSERT_EQ(started.get_future().wait_for(2s), std::future_status::ready);

  auto shutdown = std::async(std::launch::async, [&executor] {
    executor.shutdown(ActorExecutorShutdownMode::Drain);
  });
  EXPECT_EQ(shutdown.wait_for(50ms), std::future_status::timeout);
  release->set_value();
  EXPECT_EQ(shutdown.wait_for(2s), std::future_status::ready);
  shutdown.get();

  const auto metrics = executor.metrics();
  EXPECT_EQ(metrics.executed, 1);
  EXPECT_EQ(metrics.runnable, 0);
  EXPECT_EQ(metrics.active, 0);
  EXPECT_GE(metrics.shutdown_notifications, 2);
}

TEST(ActorWorkStealingExecutorTest,
     DrainRejectsExternalRescheduleAfterShutdownStarts) {
  ActorWorkStealingExecutor executor(
      ActorWorkStealingExecutorOptions{.worker_count = 1});
  std::promise<void> started;
  auto release = std::make_shared<std::promise<void>>();
  auto release_future = release->get_future().share();
  ASSERT_TRUE(executor.submit([&started, release_future](size_t) {
    started.set_value();
    release_future.wait();
  }));
  ASSERT_EQ(started.get_future().wait_for(2s), std::future_status::ready);

  auto shutdown = std::async(std::launch::async, [&executor] {
    executor.shutdown(ActorExecutorShutdownMode::Drain);
  });
  const auto deadline = std::chrono::steady_clock::now() + 2s;
  while (executor.accepting() && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::yield();
  }
  ASSERT_FALSE(executor.accepting());
  EXPECT_FALSE(executor.reschedule([](size_t) {}));

  release->set_value();
  ASSERT_EQ(shutdown.wait_for(2s), std::future_status::ready);
  shutdown.get();
  EXPECT_EQ(executor.metrics().runnable, 0);
}

TEST(ActorWorkStealingExecutorTest, CancelDropsQueuedItemsAndJoinsWorkers) {
  ActorWorkStealingExecutor executor(ActorWorkStealingExecutorOptions{
      .worker_count = 1, .start_immediately = false});
  std::atomic_size_t executed = 0;
  for (size_t index = 0; index < 100; ++index) {
    ASSERT_TRUE(
        executor.submit([&executed](size_t) { executed.fetch_add(1); }, 0));
  }

  executor.shutdown(ActorExecutorShutdownMode::Cancel);

  EXPECT_EQ(executed.load(), 0);
  EXPECT_EQ(executor.metrics().cancelled_queued, 100);
  EXPECT_EQ(executor.metrics().runnable, 0);
}

TEST(ActorWorkStealingExecutorTest, WorkExceptionDoesNotStopWorker) {
  ActorWorkStealingExecutor executor(
      ActorWorkStealingExecutorOptions{.worker_count = 1});
  std::promise<void> after_exception;
  ASSERT_TRUE(executor.submit(
      [](size_t) { throw std::runtime_error("synthetic failure"); }, 0));
  ASSERT_TRUE(executor.submit(
      [&after_exception](size_t) { after_exception.set_value(); }, 0));

  EXPECT_EQ(after_exception.get_future().wait_for(2s),
            std::future_status::ready);
  executor.shutdown(ActorExecutorShutdownMode::Drain);
  EXPECT_EQ(executor.metrics().failed, 1);
  EXPECT_EQ(executor.metrics().executed, 2);
}

TEST(ActorWorkStealingExecutorTest, RepeatedStartupAndShutdownIsStable) {
  for (size_t iteration = 0; iteration < 100; ++iteration) {
    ActorWorkStealingExecutor executor(
        ActorWorkStealingExecutorOptions{.worker_count = 2});
    std::atomic_int executions = 0;
    ASSERT_TRUE(
        executor.submit([&executions](size_t) { executions.fetch_add(1); }));
    executor.shutdown(ActorExecutorShutdownMode::Drain);
    ASSERT_EQ(executions.load(), 1);
  }
}

TEST(ActorWorkStealingExecutorTest, ThreadStartupFailureJoinsStartedWorkers) {
  auto attempts = std::make_shared<std::atomic_size_t>(0);
  ActorWorkStealingExecutor executor(ActorWorkStealingExecutorOptions{
      .worker_count = 3,
      .start_immediately = false,
      .thread_factory = [attempts](std::function<void()> entry) -> std::thread {
        if (attempts->fetch_add(1) == 1) {
          throw std::system_error(
              std::make_error_code(std::errc::resource_unavailable_try_again));
        }
        return std::thread(std::move(entry));
      },
  });

  EXPECT_THROW(executor.start(), std::system_error);
  EXPECT_FALSE(executor.started());
  EXPECT_FALSE(executor.accepting());
  EXPECT_FALSE(executor.submit([](size_t) {}));
  EXPECT_EQ(executor.metrics().runnable, 0);
  EXPECT_EQ(executor.metrics().active, 0);
}

} // namespace
} // namespace obcx::core
