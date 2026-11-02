#include "core/actor/blocking_executor.hpp"

#include <common/logger.hpp>

namespace obcx::core {

// Both Asio handler types and this control block are instantiated in core.
// No actor-defined worker trampoline remains on the pool thread once an I/O
// completion can resume (and potentially retire) the awaiting generation.
struct BlockingExecutor::Work {
  std::shared_ptr<State> state;
  boost::asio::any_io_executor executor;
  std::move_only_function<void()> work;
  std::move_only_function<void(std::exception_ptr)> completion;
  std::exception_ptr error;
  std::shared_ptr<Work> failed_completion_pin;

  static void publish(const std::shared_ptr<Work> &self) noexcept {
    try {
      boost::asio::post(self->executor, [self] {
        // The worker has already destroyed its callable. Clear the completion
        // and any moved-from result carrier on the associated executor, not
        // when the pool thread releases its last copy of self.
        try {
          self->completion(std::exchange(self->error, {}));
          self->completion = nullptr;
        } catch (...) {
          // A throwing completion is not safely replayable. Do not let its
          // remaining actor-owned carrier be destroyed by the pool thread.
          self->error = std::current_exception();
          retain_failure(
              self, "Blocking executor completion threw; operation retained");
        }
      });
    } catch (...) {
      // Never abandon the suspended awaitable (and its generation pin) on a
      // failed terminal post. There is no safe retry or successful drain here.
      retain_failure(
          self, "Blocking executor completion post failed; operation pinned");
    }
  }

  static void retain_failure(const std::shared_ptr<Work> &self,
                             const char *message) noexcept {
    self->failed_completion_pin = self;
    try {
      OBCX_ERROR("{}", message);
    } catch (...) {
    }
  }
};

void BlockingExecutor::dispatch_work(
    std::shared_ptr<State> state, boost::asio::any_io_executor executor,
    std::move_only_function<void()> work,
    std::move_only_function<void(std::exception_ptr)> completion) {
  auto task =
      std::make_shared<Work>(state, std::move(executor), std::move(work),
                             std::move(completion), nullptr, nullptr);
  {
    std::scoped_lock lock(state->admission_mutex);
    if (!state->accepting.load(std::memory_order_acquire)) {
      state->rejected.fetch_add(1, std::memory_order_relaxed);
      task->error = std::make_exception_ptr(BlockingExecutorStopped{});
    } else {
      state->submitted.fetch_add(1, std::memory_order_relaxed);
      state->pending.fetch_add(1, std::memory_order_relaxed);
      try {
        boost::asio::post(state->pool, [task] {
          auto &state = *task->state;
          state.pending.fetch_sub(1, std::memory_order_relaxed);
          state.running.fetch_add(1, std::memory_order_relaxed);
          try {
            task->work();
            state.completed.fetch_add(1, std::memory_order_relaxed);
          } catch (...) {
            task->error = std::current_exception();
            state.failed.fetch_add(1, std::memory_order_relaxed);
          }
          // Includes destruction of captures, file guards and actor-owned
          // repository/config objects before the caller observes completion.
          task->work = nullptr;
          state.running.fetch_sub(1, std::memory_order_relaxed);
          Work::publish(task);
        });
        return;
      } catch (...) {
        state->pending.fetch_sub(1, std::memory_order_relaxed);
        state->rejected.fetch_add(1, std::memory_order_relaxed);
        task->error = std::current_exception();
      }
    }
  }
  // Callable destructors must not run under the pool's admission mutex.
  // Rejection/post failure never invokes work but still owns its completion.
  task->work = nullptr;
  Work::publish(task);
}

} // namespace obcx::core
