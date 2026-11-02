#pragma once

#include <cstddef>

namespace obcx::common {
struct ActorRuntimeConfig;
}

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

[[nodiscard]] auto resolve_runtime_thread_budget(
    const common::ActorRuntimeConfig &runtime_config,
    size_t configured_io_sources) -> RuntimeThreadBudget;

} // namespace obcx::core
