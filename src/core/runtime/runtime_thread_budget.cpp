#include "core/runtime/runtime_thread_budget.hpp"

#include <algorithm>
#include <thread>

namespace obcx::core {
namespace {

auto normalized_total(const RuntimeThreadBudgetRequest &request) -> size_t {
  auto total = request.total_threads;
  if (total == 0) {
    total = request.hardware_threads;
  }
  if (total == 0) {
    total = std::thread::hardware_concurrency();
  }
  // Actor, I/O, and blocking work are distinct execution domains. Preserve
  // one thread for each even on a tiny or unknown hardware report.
  return std::max<size_t>(3, total);
}

} // namespace

auto resolve_runtime_thread_budget(RuntimeThreadBudgetRequest request)
    -> RuntimeThreadBudget {
  RuntimeThreadBudget result;
  result.total_threads = normalized_total(request);
  result.actor_workers = std::max<size_t>(1, request.actor_workers);
  result.io_workers = std::max<size_t>(1, request.io_workers);
  result.blocking_workers = std::max<size_t>(1, request.blocking_workers);

  auto assigned =
      result.actor_workers + result.io_workers + result.blocking_workers;
  if (assigned > result.total_threads) {
    // Rebuild from the mandatory one-per-domain floor and satisfy explicit
    // requests in actor, I/O, then blocking priority without exceeding the
    // one process-wide budget.
    result.actor_workers = 1;
    result.io_workers = 1;
    result.blocking_workers = 1;
    auto remaining = result.total_threads - 3;
    const auto grant = [&remaining](const size_t requested) {
      const auto extra = requested > 0 ? requested - 1 : 0;
      const auto value = std::min(extra, remaining);
      remaining -= value;
      return value;
    };
    result.actor_workers += grant(request.actor_workers);
    result.io_workers += grant(request.io_workers);
    result.blocking_workers += grant(request.blocking_workers);
    result.actor_workers += remaining;
    return result;
  }

  auto remaining = result.total_threads - assigned;
  const auto has_automatic_pool = request.actor_workers == 0 ||
                                  request.io_workers == 0 ||
                                  request.blocking_workers == 0;
  while (remaining > 0 && has_automatic_pool) {
    if (request.actor_workers == 0) {
      const auto actor_grant = std::min<size_t>(2, remaining);
      result.actor_workers += actor_grant;
      remaining -= actor_grant;
    }
    if (remaining > 0 && request.io_workers == 0) {
      ++result.io_workers;
      --remaining;
    }
    if (remaining > 0 && request.blocking_workers == 0) {
      ++result.blocking_workers;
      --remaining;
    }
  }
  // Actor continuations are the latency-sensitive cooperative work; give them
  // any unclaimed remainder after explicit pool requests are honored.
  result.actor_workers += remaining;
  return result;
}

} // namespace obcx::core
