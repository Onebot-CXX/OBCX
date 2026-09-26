#include "core/runtime/runtime_thread_budget.hpp"

#include <gtest/gtest.h>

namespace obcx::core {
namespace {

TEST(RuntimeThreadBudgetTest, SharesOneBudgetAcrossAllRuntimePools) {
  const auto budget = resolve_runtime_thread_budget(RuntimeThreadBudgetRequest{
      .total_threads = 12,
      .actor_workers = 6,
      .io_workers = 3,
      .blocking_workers = 2,
  });

  EXPECT_EQ(budget.total_threads, 12);
  EXPECT_EQ(budget.actor_workers + budget.io_workers + budget.blocking_workers,
            budget.total_threads);
  EXPECT_GE(budget.actor_workers, 6);
  EXPECT_GE(budget.io_workers, 3);
  EXPECT_GE(budget.blocking_workers, 2);
}

TEST(RuntimeThreadBudgetTest, DefaultsFavorActorWorkersWithoutMultiplication) {
  const auto budget = resolve_runtime_thread_budget(
      RuntimeThreadBudgetRequest{.total_threads = 8});

  EXPECT_EQ(budget.total_threads, 8);
  EXPECT_EQ(budget.actor_workers + budget.io_workers + budget.blocking_workers,
            8);
  EXPECT_GE(budget.actor_workers, budget.io_workers);
  EXPECT_GE(budget.actor_workers, budget.blocking_workers);
  EXPECT_GE(budget.io_workers, 1);
  EXPECT_GE(budget.blocking_workers, 1);
}

TEST(RuntimeThreadBudgetTest, AutomaticBlockingPoolSharesExplicitIoBudget) {
  const auto budget = resolve_runtime_thread_budget(RuntimeThreadBudgetRequest{
      .total_threads = 8,
      .io_workers = 2,
  });

  EXPECT_EQ(budget.total_threads, 8);
  EXPECT_EQ(budget.io_workers, 2);
  EXPECT_GT(budget.blocking_workers, 1);
  EXPECT_EQ(budget.actor_workers + budget.io_workers + budget.blocking_workers,
            8);
}

TEST(RuntimeThreadBudgetTest, ClampsOneOversizedPoolToLeaveRuntimeCapacity) {
  const auto budget = resolve_runtime_thread_budget(RuntimeThreadBudgetRequest{
      .total_threads = 8,
      .actor_workers = 64,
  });

  EXPECT_EQ(budget.total_threads, 8);
  EXPECT_EQ(budget.actor_workers, 6);
  EXPECT_EQ(budget.io_workers, 1);
  EXPECT_EQ(budget.blocking_workers, 1);
}

TEST(RuntimeThreadBudgetTest, NormalizesTinyOrUnknownHardwareBudgets) {
  const auto budget = resolve_runtime_thread_budget(
      RuntimeThreadBudgetRequest{.total_threads = 0, .hardware_threads = 0});

  EXPECT_GE(budget.total_threads, 3);
  EXPECT_GE(budget.actor_workers, 1);
  EXPECT_GE(budget.io_workers, 1);
  EXPECT_GE(budget.blocking_workers, 1);
}

} // namespace
} // namespace obcx::core
