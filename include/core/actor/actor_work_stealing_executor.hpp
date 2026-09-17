#ifndef OBCX_INCLUDE_CORE_ACTOR_WORK_STEALING_EXECUTOR_HPP_
#define OBCX_INCLUDE_CORE_ACTOR_WORK_STEALING_EXECUTOR_HPP_

#include "core/actor/actor_task.hpp"

#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <random>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace obcx::core {

enum class ActorExecutorShutdownMode {
  Drain,
  Cancel,
};

class ActorExecutorWork {
public:
  ActorExecutorWork() noexcept = default;
  ActorExecutorWork(const ActorExecutorWork &) = delete;
  auto operator=(const ActorExecutorWork &) -> ActorExecutorWork & = delete;

  ActorExecutorWork(ActorExecutorWork &&other) noexcept {
    move_from(std::move(other));
  }

  auto operator=(ActorExecutorWork &&other) noexcept -> ActorExecutorWork & {
    if (this != &other) {
      reset();
      move_from(std::move(other));
    }
    return *this;
  }

  template <typename Function>
  requires(!std::same_as<std::remove_cvref_t<Function>, ActorExecutorWork> &&
           std::invocable<std::remove_cvref_t<Function> &, size_t>)
  ActorExecutorWork(Function &&function) {
    using Stored = std::remove_cvref_t<Function>;
    static_assert(sizeof(Stored) <= storage_size,
                  "actor executor work capture exceeds inline storage");
    static_assert(alignof(Stored) <= alignof(std::max_align_t),
                  "actor executor work capture is over-aligned");
    static_assert(std::is_nothrow_move_constructible_v<Stored>,
                  "actor executor work must be nothrow move constructible");
    std::construct_at(reinterpret_cast<Stored *>(storage_),
                      std::forward<Function>(function));
    operations_ = &operations_for<Stored>;
  }

  ~ActorExecutorWork() { reset(); }

  explicit operator bool() const noexcept { return operations_ != nullptr; }

  void operator()(const size_t worker_id) {
    if (!operations_) {
      throw std::bad_function_call{};
    }
    operations_->invoke(storage_, worker_id);
  }

private:
  static constexpr size_t storage_size = 64;

  struct Operations {
    void (*invoke)(void *, size_t);
    void (*move)(void *, void *) noexcept;
    void (*destroy)(void *) noexcept;
  };

  template <typename Stored>
  static constexpr Operations operations_for{
      .invoke =
          [](void *storage, const size_t worker_id) {
            (*reinterpret_cast<Stored *>(storage))(worker_id);
          },
      .move =
          [](void *source, void *destination) noexcept {
            auto *typed_source = reinterpret_cast<Stored *>(source);
            std::construct_at(reinterpret_cast<Stored *>(destination),
                              std::move(*typed_source));
            std::destroy_at(typed_source);
          },
      .destroy =
          [](void *storage) noexcept {
            std::destroy_at(reinterpret_cast<Stored *>(storage));
          },
  };

  void reset() noexcept {
    if (operations_) {
      operations_->destroy(storage_);
      operations_ = nullptr;
    }
  }

  void move_from(ActorExecutorWork &&other) noexcept {
    if (!other.operations_) {
      return;
    }
    operations_ = other.operations_;
    operations_->move(other.storage_, storage_);
    other.operations_ = nullptr;
  }

  alignas(std::max_align_t) std::byte storage_[storage_size]{};
  const Operations *operations_ = nullptr;
};

struct ActorWorkStealingExecutorOptions {
  size_t worker_count = 0; // 0 = hardware concurrency, normalized to at least 1
  uint64_t random_seed = 0x4f424358ULL;
  bool start_immediately = true;
  std::function<std::thread(std::function<void()>)> thread_factory;
};

struct ActorWorkStealingMetrics {
  uint64_t submitted = 0;
  uint64_t rescheduled = 0;
  uint64_t executed = 0;
  uint64_t failed = 0;
  uint64_t steal_attempts = 0;
  uint64_t successful_steals = 0;
  uint64_t worker_sleeps = 0;
  uint64_t worker_wakes = 0;
  uint64_t work_items_created = 0;
  uint64_t work_item_heap_allocations = 0;
  uint64_t work_notifications = 0;
  uint64_t shutdown_notifications = 0;
  uint64_t cancelled_queued = 0;
  size_t runnable = 0;
  size_t active = 0;
  size_t injector_depth = 0;
  std::vector<size_t> worker_queue_depths;
};

class ActorWorkStealingExecutor {
public:
  using Work = ActorExecutorWork;

  ActorWorkStealingExecutor();
  explicit ActorWorkStealingExecutor(ActorWorkStealingExecutorOptions options);
  ActorWorkStealingExecutor(const ActorWorkStealingExecutor &) = delete;
  auto operator=(const ActorWorkStealingExecutor &)
      -> ActorWorkStealingExecutor & = delete;
  ActorWorkStealingExecutor(ActorWorkStealingExecutor &&) = delete;
  auto operator=(ActorWorkStealingExecutor &&)
      -> ActorWorkStealingExecutor & = delete;
  ~ActorWorkStealingExecutor();

  void start();

  auto submit(Work work,
              size_t preferred_worker = ACTOR_TASK_NO_PREFERRED_WORKER) -> bool;
  auto reschedule(Work work,
                  size_t preferred_worker = ACTOR_TASK_NO_PREFERRED_WORKER)
      -> bool;

  void shutdown(
      ActorExecutorShutdownMode mode = ActorExecutorShutdownMode::Drain);

  [[nodiscard]] auto worker_count() const noexcept -> size_t;
  [[nodiscard]] auto accepting() const noexcept -> bool;
  [[nodiscard]] auto started() const noexcept -> bool;
  [[nodiscard]] auto metrics() const -> ActorWorkStealingMetrics;

  [[nodiscard]] static auto current_worker_id() noexcept
      -> std::optional<size_t>;
  [[nodiscard]] static auto on_worker_thread() noexcept -> bool;

private:
  struct WorkerState {
    std::mutex mutex;
    std::deque<Work> queue;
    std::thread thread;
    std::atomic_uint64_t executed = 0;
    std::atomic_uint64_t failed = 0;
    std::atomic_uint64_t steal_attempts = 0;
    std::atomic_uint64_t successful_steals = 0;
    std::atomic_uint64_t sleeps = 0;
    std::atomic_uint64_t wakes = 0;
  };

  auto enqueue(Work work, size_t preferred_worker, bool internal) -> bool;
  auto try_pop_local(size_t worker_id) -> std::optional<Work>;
  auto try_pop_injector() -> std::optional<Work>;
  auto try_steal(size_t worker_id, std::minstd_rand &random)
      -> std::optional<Work>;
  auto try_take_work(size_t worker_id, std::minstd_rand &random)
      -> std::optional<Work>;
  void worker_loop(size_t worker_id);
  [[nodiscard]] auto should_exit_idle_worker() const noexcept -> bool;
  auto clear_queued_work() -> size_t;
  void notify_work_available() noexcept;
  void notify_all_workers() noexcept;

  ActorWorkStealingExecutorOptions options_;
  std::vector<std::unique_ptr<WorkerState>> workers_;
  mutable std::mutex injector_mutex_;
  std::deque<Work> injector_;
  mutable std::mutex lifecycle_mutex_;

  std::atomic_bool started_ = false;
  std::atomic_bool accepting_ = true;
  std::atomic_bool shutdown_requested_ = false;
  std::atomic_bool cancelling_ = false;
  std::atomic_bool joined_ = false;
  alignas(64) std::atomic_size_t runnable_count_ = 0;
  alignas(64) std::atomic_size_t active_count_ = 0;
  alignas(64) std::atomic_uint64_t wake_epoch_ = 0;

  alignas(64) std::atomic_uint64_t submitted_ = 0;
  alignas(64) std::atomic_uint64_t rescheduled_ = 0;
  alignas(64) std::atomic_uint64_t work_items_created_ = 0;
  alignas(64) std::atomic_uint64_t work_notifications_ = 0;
  alignas(64) std::atomic_uint64_t shutdown_notifications_ = 0;
  alignas(64) std::atomic_uint64_t cancelled_queued_ = 0;
};

} // namespace obcx::core

#endif // OBCX_INCLUDE_CORE_ACTOR_WORK_STEALING_EXECUTOR_HPP_
