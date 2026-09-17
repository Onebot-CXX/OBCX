#include "core/actor/actor_work_stealing_executor.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <utility>

namespace obcx::core {
namespace {

thread_local ActorWorkStealingExecutor *current_executor = nullptr;
thread_local size_t current_worker = ACTOR_TASK_NO_PREFERRED_WORKER;

auto normalized_worker_count(const size_t requested) -> size_t {
  if (requested > 0) {
    return requested;
  }
  return std::max<size_t>(1, std::thread::hardware_concurrency());
}

} // namespace

ActorWorkStealingExecutor::ActorWorkStealingExecutor()
    : ActorWorkStealingExecutor(ActorWorkStealingExecutorOptions{}) {}

ActorWorkStealingExecutor::ActorWorkStealingExecutor(
    ActorWorkStealingExecutorOptions options)
    : options_(std::move(options)) {
  options_.worker_count = normalized_worker_count(options_.worker_count);
  workers_.reserve(options_.worker_count);
  for (size_t index = 0; index < options_.worker_count; ++index) {
    workers_.push_back(std::make_unique<WorkerState>());
  }
  if (options_.start_immediately) {
    start();
  }
}

ActorWorkStealingExecutor::~ActorWorkStealingExecutor() {
  if (current_executor == this) {
    std::terminate();
  }
  shutdown(ActorExecutorShutdownMode::Cancel);
}

void ActorWorkStealingExecutor::start() {
  std::unique_lock lock(lifecycle_mutex_);
  if (started_.load(std::memory_order_acquire)) {
    return;
  }
  if (shutdown_requested_.load(std::memory_order_acquire)) {
    throw std::logic_error("cannot start a stopped actor executor");
  }
  try {
    for (size_t index = 0; index < workers_.size(); ++index) {
      auto entry = [this, index] { worker_loop(index); };
      workers_[index]->thread = options_.thread_factory
                                    ? options_.thread_factory(std::move(entry))
                                    : std::thread(std::move(entry));
    }
    started_.store(true, std::memory_order_release);
  } catch (...) {
    auto failure = std::current_exception();
    accepting_.store(false, std::memory_order_release);
    shutdown_requested_.store(true, std::memory_order_release);
    cancelling_.store(true, std::memory_order_release);
    clear_queued_work();
    notify_all_workers();

    std::vector<std::thread *> started_threads;
    for (auto &worker : workers_) {
      if (worker->thread.joinable()) {
        started_threads.push_back(&worker->thread);
      }
    }
    lock.unlock();
    for (auto *thread : started_threads) {
      thread->join();
    }
    joined_.store(true, std::memory_order_release);
    std::rethrow_exception(failure);
  }
}

auto ActorWorkStealingExecutor::submit(Work work, const size_t preferred_worker)
    -> bool {
  return enqueue(std::move(work), preferred_worker, false);
}

auto ActorWorkStealingExecutor::reschedule(Work work,
                                           const size_t preferred_worker)
    -> bool {
  return enqueue(std::move(work), preferred_worker, true);
}

auto ActorWorkStealingExecutor::enqueue(Work work, size_t preferred_worker,
                                        const bool internal) -> bool {
  if (!work) {
    return false;
  }
  const auto worker_drain_reschedule = internal && current_executor == this;
  if (cancelling_.load(std::memory_order_acquire) ||
      (!internal && !accepting_.load(std::memory_order_acquire)) ||
      (shutdown_requested_.load(std::memory_order_acquire) &&
       !worker_drain_reschedule) ||
      joined_.load(std::memory_order_acquire)) {
    return false;
  }

  const auto rescheduled_locally =
      internal && current_executor == this &&
      preferred_worker != ACTOR_TASK_NO_PREFERRED_WORKER &&
      preferred_worker % workers_.size() == current_worker;
  size_t previous_runnable = 0;
  if (preferred_worker == ACTOR_TASK_NO_PREFERRED_WORKER) {
    std::scoped_lock lock(injector_mutex_);
    if (cancelling_.load(std::memory_order_acquire) ||
        (!internal && !accepting_.load(std::memory_order_acquire)) ||
        (shutdown_requested_.load(std::memory_order_acquire) &&
         !worker_drain_reschedule) ||
        joined_.load(std::memory_order_acquire)) {
      return false;
    }
    injector_.push_back(std::move(work));
    previous_runnable = runnable_count_.fetch_add(1, std::memory_order_release);
  } else {
    preferred_worker %= workers_.size();
    auto &worker = *workers_[preferred_worker];
    std::scoped_lock lock(worker.mutex);
    if (cancelling_.load(std::memory_order_acquire) ||
        (!internal && !accepting_.load(std::memory_order_acquire)) ||
        (shutdown_requested_.load(std::memory_order_acquire) &&
         !worker_drain_reschedule) ||
        joined_.load(std::memory_order_acquire)) {
      return false;
    }
    worker.queue.push_back(std::move(work));
    previous_runnable = runnable_count_.fetch_add(1, std::memory_order_release);
  }

  if (internal) {
    rescheduled_.fetch_add(1, std::memory_order_relaxed);
  } else {
    submitted_.fetch_add(1, std::memory_order_relaxed);
  }
  work_items_created_.fetch_add(1, std::memory_order_relaxed);
  // A continuation placed on the current worker's own deque is observed by
  // that worker as soon as the current item returns. Waking a peer here only
  // creates a futile steal race for single-continuation mailboxes.
  // Once runnable and active work already covers every worker, further
  // notifications cannot add parallelism. Workers finishing current items
  // observe the queued work directly, so coalescing here is lossless.
  const auto occupied =
      previous_runnable + active_count_.load(std::memory_order_acquire);
  if (!rescheduled_locally && occupied < workers_.size()) {
    notify_work_available();
  }
  return true;
}

auto ActorWorkStealingExecutor::try_pop_local(const size_t worker_id)
    -> std::optional<Work> {
  auto &worker = *workers_[worker_id];
  std::scoped_lock lock(worker.mutex);
  if (worker.queue.empty()) {
    return std::nullopt;
  }
  auto item = std::move(worker.queue.back());
  worker.queue.pop_back();
  // Claim accounting is linearized with queue removal. Incrementing active
  // first prevents drain from observing a false idle gap, while decrementing
  // runnable here prevents peers from trying to steal an item already owned.
  active_count_.fetch_add(1, std::memory_order_acq_rel);
  runnable_count_.fetch_sub(1, std::memory_order_acq_rel);
  return item;
}

auto ActorWorkStealingExecutor::try_pop_injector() -> std::optional<Work> {
  std::scoped_lock lock(injector_mutex_);
  if (injector_.empty()) {
    return std::nullopt;
  }
  auto item = std::move(injector_.front());
  injector_.pop_front();
  active_count_.fetch_add(1, std::memory_order_acq_rel);
  runnable_count_.fetch_sub(1, std::memory_order_acq_rel);
  return item;
}

auto ActorWorkStealingExecutor::try_steal(const size_t worker_id,
                                          std::minstd_rand &random)
    -> std::optional<Work> {
  if (workers_.size() < 2) {
    return std::nullopt;
  }

  const auto start = static_cast<size_t>(random()) % workers_.size();
  for (size_t offset = 0; offset < workers_.size(); ++offset) {
    const auto victim_id = (start + offset) % workers_.size();
    if (victim_id == worker_id) {
      continue;
    }

    workers_[worker_id]->steal_attempts.fetch_add(1, std::memory_order_relaxed);
    auto &victim = *workers_[victim_id];
    std::scoped_lock lock(victim.mutex);
    if (victim.queue.empty()) {
      continue;
    }
    auto item = std::move(victim.queue.front());
    victim.queue.pop_front();
    active_count_.fetch_add(1, std::memory_order_acq_rel);
    runnable_count_.fetch_sub(1, std::memory_order_acq_rel);
    workers_[worker_id]->successful_steals.fetch_add(1,
                                                     std::memory_order_relaxed);
    return item;
  }
  return std::nullopt;
}

auto ActorWorkStealingExecutor::try_take_work(const size_t worker_id,
                                              std::minstd_rand &random)
    -> std::optional<Work> {
  if (auto local = try_pop_local(worker_id)) {
    return local;
  }
  if (auto injected = try_pop_injector()) {
    return injected;
  }
  if (runnable_count_.load(std::memory_order_acquire) == 0) {
    return std::nullopt;
  }
  return try_steal(worker_id, random);
}

void ActorWorkStealingExecutor::worker_loop(const size_t worker_id) {
  current_executor = this;
  current_worker = worker_id;
  std::minstd_rand random(
      static_cast<unsigned int>(options_.random_seed + worker_id * 7919U));
  size_t idle_yields = 0;
  constexpr size_t idle_yield_limit = 32;

  while (true) {
    if (auto item = try_take_work(worker_id, random)) {
      idle_yields = 0;
      try {
        (*item)(worker_id);
      } catch (...) {
        workers_[worker_id]->failed.fetch_add(1, std::memory_order_relaxed);
      }
      workers_[worker_id]->executed.fetch_add(1, std::memory_order_relaxed);
      const auto previous_active =
          active_count_.fetch_sub(1, std::memory_order_acq_rel);
      if (previous_active == 1 &&
          shutdown_requested_.load(std::memory_order_acquire) &&
          runnable_count_.load(std::memory_order_acquire) == 0) {
        notify_all_workers();
      }
      continue;
    }

    // Capture the notification epoch before the final runnable check. If a
    // producer publishes work after this load, wait() observes the changed
    // epoch and returns immediately. If it published work before this load,
    // the acquire below observes the runnable-count release. Loading the
    // epoch after the runnable check would leave a lost-wakeup window.
    const auto epoch = wake_epoch_.load(std::memory_order_acquire);
    if (runnable_count_.load(std::memory_order_acquire) > 0) {
      continue;
    }
    if (should_exit_idle_worker()) {
      break;
    }
    // Short actor continuations commonly arrive immediately after their
    // completion is posted to the I/O executor. A small cooperative-yield
    // window avoids a futex sleep/wake round trip without polling indefinitely.
    if (idle_yields++ < idle_yield_limit) {
      std::this_thread::yield();
      continue;
    }
    idle_yields = 0;
    workers_[worker_id]->sleeps.fetch_add(1, std::memory_order_relaxed);
    wake_epoch_.wait(epoch, std::memory_order_acquire);
    workers_[worker_id]->wakes.fetch_add(1, std::memory_order_relaxed);
    if (should_exit_idle_worker() &&
        runnable_count_.load(std::memory_order_acquire) == 0) {
      break;
    }
  }

  current_worker = ACTOR_TASK_NO_PREFERRED_WORKER;
  current_executor = nullptr;
}

auto ActorWorkStealingExecutor::should_exit_idle_worker() const noexcept
    -> bool {
  if (!shutdown_requested_.load(std::memory_order_acquire)) {
    return false;
  }
  if (cancelling_.load(std::memory_order_acquire)) {
    return runnable_count_.load(std::memory_order_acquire) == 0;
  }
  return runnable_count_.load(std::memory_order_acquire) == 0 &&
         active_count_.load(std::memory_order_acquire) == 0;
}

auto ActorWorkStealingExecutor::clear_queued_work() -> size_t {
  size_t removed = 0;
  {
    std::scoped_lock lock(injector_mutex_);
    removed += injector_.size();
    injector_.clear();
  }
  for (auto &worker : workers_) {
    std::scoped_lock lock(worker->mutex);
    removed += worker->queue.size();
    worker->queue.clear();
  }
  if (removed > 0) {
    runnable_count_.fetch_sub(removed, std::memory_order_acq_rel);
    cancelled_queued_.fetch_add(removed, std::memory_order_relaxed);
  }
  return removed;
}

void ActorWorkStealingExecutor::shutdown(const ActorExecutorShutdownMode mode) {
  std::unique_lock lifecycle_lock(lifecycle_mutex_);
  if (joined_.load(std::memory_order_acquire)) {
    return;
  }
  if (current_executor == this) {
    throw std::logic_error("actor executor cannot join itself");
  }

  accepting_.store(false, std::memory_order_release);
  shutdown_requested_.store(true, std::memory_order_release);
  if (mode == ActorExecutorShutdownMode::Cancel) {
    cancelling_.store(true, std::memory_order_release);
    clear_queued_work();
  }
  notify_all_workers();

  auto threads = std::vector<std::thread *>{};
  threads.reserve(workers_.size());
  for (auto &worker : workers_) {
    if (worker->thread.joinable()) {
      threads.push_back(&worker->thread);
    }
  }
  lifecycle_lock.unlock();

  for (auto *thread : threads) {
    thread->join();
  }

  lifecycle_lock.lock();
  joined_.store(true, std::memory_order_release);
  lifecycle_lock.unlock();
}

void ActorWorkStealingExecutor::notify_work_available() noexcept {
  work_notifications_.fetch_add(1, std::memory_order_relaxed);
  wake_epoch_.fetch_add(1, std::memory_order_release);
  wake_epoch_.notify_one();
}

void ActorWorkStealingExecutor::notify_all_workers() noexcept {
  shutdown_notifications_.fetch_add(1, std::memory_order_relaxed);
  wake_epoch_.fetch_add(1, std::memory_order_release);
  wake_epoch_.notify_all();
}

auto ActorWorkStealingExecutor::worker_count() const noexcept -> size_t {
  return workers_.size();
}

auto ActorWorkStealingExecutor::accepting() const noexcept -> bool {
  return accepting_.load(std::memory_order_acquire);
}

auto ActorWorkStealingExecutor::started() const noexcept -> bool {
  return started_.load(std::memory_order_acquire);
}

auto ActorWorkStealingExecutor::metrics() const -> ActorWorkStealingMetrics {
  ActorWorkStealingMetrics result;
  result.submitted = submitted_.load(std::memory_order_relaxed);
  result.rescheduled = rescheduled_.load(std::memory_order_relaxed);
  for (const auto &worker : workers_) {
    result.executed += worker->executed.load(std::memory_order_relaxed);
    result.failed += worker->failed.load(std::memory_order_relaxed);
    result.steal_attempts +=
        worker->steal_attempts.load(std::memory_order_relaxed);
    result.successful_steals +=
        worker->successful_steals.load(std::memory_order_relaxed);
    result.worker_sleeps += worker->sleeps.load(std::memory_order_relaxed);
    result.worker_wakes += worker->wakes.load(std::memory_order_relaxed);
  }
  result.work_items_created =
      work_items_created_.load(std::memory_order_relaxed);
  result.work_item_heap_allocations = 0;
  result.work_notifications =
      work_notifications_.load(std::memory_order_relaxed);
  result.shutdown_notifications =
      shutdown_notifications_.load(std::memory_order_relaxed);
  result.cancelled_queued = cancelled_queued_.load(std::memory_order_relaxed);
  result.runnable = runnable_count_.load(std::memory_order_relaxed);
  result.active = active_count_.load(std::memory_order_relaxed);
  {
    std::scoped_lock lock(injector_mutex_);
    result.injector_depth = injector_.size();
  }
  result.worker_queue_depths.reserve(workers_.size());
  for (const auto &worker : workers_) {
    std::scoped_lock lock(worker->mutex);
    result.worker_queue_depths.push_back(worker->queue.size());
  }
  return result;
}

auto ActorWorkStealingExecutor::current_worker_id() noexcept
    -> std::optional<size_t> {
  if (current_worker == ACTOR_TASK_NO_PREFERRED_WORKER) {
    return std::nullopt;
  }
  return current_worker;
}

auto ActorWorkStealingExecutor::on_worker_thread() noexcept -> bool {
  return current_executor != nullptr;
}

} // namespace obcx::core
