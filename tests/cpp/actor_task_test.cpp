#include "core/actor/actor.hpp"
#include "support/actor_task_test_scheduler.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>

namespace obcx::core {
namespace {

auto value_task(int &executions) -> ActorTask<int> {
  executions++;
  co_return 42;
}

auto void_task(int &executions) -> ActorTask<void> {
  executions++;
  co_return;
}

auto throwing_task() -> ActorTask<int> {
  throw std::runtime_error("task failed");
  co_return 0;
}

auto yielding_task(ActorContext &context, int &step) -> ActorTask<void> {
  step = 1;
  co_await context.yield();
  step = 2;
}

auto holds_owner(std::shared_ptr<int> owner) -> ActorTask<void> {
  (void)owner;
  co_return;
}

TEST(ActorTaskTest, DoesNotRunBeforeExplicitResumeAndReturnsValue) {
  int executions = 0;
  auto task = value_task(executions);

  EXPECT_TRUE(task.valid());
  EXPECT_FALSE(task.done());
  EXPECT_EQ(task.suspension(), ActorTaskSuspension::Initial);
  EXPECT_EQ(executions, 0);

  task.resume();

  EXPECT_TRUE(task.done());
  EXPECT_EQ(task.suspension(), ActorTaskSuspension::Completed);
  EXPECT_EQ(executions, 1);
  EXPECT_EQ(task.take_result(), 42);
}

TEST(ActorTaskTest, SupportsVoidTasksAndMoveOnlyOwnership) {
  int executions = 0;
  auto first = void_task(executions);
  auto second = std::move(first);

  EXPECT_FALSE(first.valid());
  EXPECT_TRUE(second.valid());
  second.resume();
  EXPECT_TRUE(second.done());
  EXPECT_NO_THROW(second.take_result());
  EXPECT_EQ(executions, 1);
}

TEST(ActorTaskTest, PropagatesUnhandledExceptionsAtResultBoundary) {
  auto task = throwing_task();
  task.resume();
  ASSERT_TRUE(task.done());
  EXPECT_THROW((void)task.take_result(), std::runtime_error);
}

TEST(ActorTaskTest, CarriesRuntimeMetadataAndPublishesCompletionOnce) {
  int executions = 0;
  int completions = 0;
  auto cancellation = std::make_shared<ActorCancellationState>();
  auto task = value_task(executions);
  task.attach_runtime(ActorTaskRuntimeContext{
      .scheduler = reinterpret_cast<void *>(0x1),
      .task_id = 17,
      .mailbox_generation = 9,
      .preferred_worker = 3,
      .cancellation = cancellation,
      .completion_target = [&completions] { completions++; },
  });

  task.resume();

  EXPECT_EQ(completions, 1);
  EXPECT_EQ(task.runtime().scheduler, reinterpret_cast<void *>(0x1));
  EXPECT_EQ(task.runtime().task_id, 17);
  EXPECT_EQ(task.runtime().mailbox_generation, 9);
  EXPECT_EQ(task.runtime().preferred_worker, 3);
  EXPECT_EQ(task.runtime().cancellation, cancellation);
}

TEST(ActorTaskTest, DestroysNeverStartedCoroutineFrame) {
  auto owner = std::make_shared<int>(7);
  std::weak_ptr<int> observer = owner;
  {
    auto task = holds_owner(owner);
    owner.reset();
    EXPECT_FALSE(observer.expired());
    EXPECT_FALSE(task.done());
  }
  EXPECT_TRUE(observer.expired());
}

TEST(ActorTaskTest, DeterministicSchedulerRequeuesExplicitYield) {
  test::ActorTaskTestScheduler scheduler;
  ActorContext context{"yielding"};
  int step = 0;
  auto ticket = scheduler.submit(yielding_task(context, step));

  ASSERT_TRUE(scheduler.run_one());
  EXPECT_EQ(step, 1);
  EXPECT_FALSE(ticket.done());
  EXPECT_EQ(scheduler.ready_count(), 1);

  ASSERT_TRUE(scheduler.run_one());
  EXPECT_EQ(step, 2);
  EXPECT_TRUE(ticket.done());
  EXPECT_NO_THROW(ticket.take_result());
}

TEST(ActorTaskTest, YieldObservesCancellation) {
  test::ActorTaskTestScheduler scheduler;
  auto cancellation = std::make_shared<ActorCancellationState>();
  ActorContext context{"cancelled", std::make_shared<ActorServices>(), "", "",
                       cancellation};
  int step = 0;
  auto ticket = scheduler.submit(yielding_task(context, step));

  ASSERT_TRUE(scheduler.run_one());
  cancellation->request_cancellation();
  ASSERT_TRUE(scheduler.run_one());

  EXPECT_TRUE(ticket.done());
  EXPECT_EQ(step, 1);
  EXPECT_THROW(ticket.take_result(), ActorTaskCancelled);
}

} // namespace
} // namespace obcx::core
