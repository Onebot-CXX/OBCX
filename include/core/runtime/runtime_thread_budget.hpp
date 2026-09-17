#ifndef OBCX_INCLUDE_CORE_RUNTIME_THREAD_BUDGET_HPP_
#define OBCX_INCLUDE_CORE_RUNTIME_THREAD_BUDGET_HPP_

#include <cstddef>

namespace obcx::core {

struct RuntimeThreadBudgetRequest {
  size_t total_threads = 0;
  size_t hardware_threads = 0;
  size_t actor_workers = 0;
  size_t io_workers = 0;
  size_t blocking_workers = 0;
};

struct RuntimeThreadBudget {
  size_t total_threads = 0;
  size_t actor_workers = 0;
  size_t io_workers = 0;
  size_t blocking_workers = 0;
};

[[nodiscard]] auto resolve_runtime_thread_budget(
    RuntimeThreadBudgetRequest request) -> RuntimeThreadBudget;

} // namespace obcx::core

#endif // OBCX_INCLUDE_CORE_RUNTIME_THREAD_BUDGET_HPP_
