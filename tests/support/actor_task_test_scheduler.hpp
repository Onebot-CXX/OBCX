#pragma once

#include "core/actor/actor_task.hpp"

#include <deque>
#include <functional>
#include <memory>
#include <stdexcept>
#include <utility>

namespace obcx::core::test {

class ActorTaskTestScheduler {
public:
  template <typename T> class Ticket {
  public:
    [[nodiscard]] auto done() const noexcept -> bool {
      return task_ && task_->done();
    }

    auto take_result() -> T
    requires(!std::is_void_v<T>)
    {
      return task_->take_result();
    }

    void take_result()
    requires std::is_void_v<T>
    {
      task_->take_result();
    }

    [[nodiscard]] auto task() -> ActorTask<T> & { return *task_; }

  private:
    friend class ActorTaskTestScheduler;
    explicit Ticket(std::shared_ptr<ActorTask<T>> task)
        : task_(std::move(task)) {}
    std::shared_ptr<ActorTask<T>> task_;
  };

  template <typename T> auto submit(ActorTask<T> task) -> Ticket<T> {
    auto owned = std::make_shared<ActorTask<T>>(std::move(task));
    schedule(owned);
    return Ticket<T>{std::move(owned)};
  }

  [[nodiscard]] auto ready_count() const noexcept -> size_t {
    return ready_.size();
  }

  auto run_one() -> bool {
    if (ready_.empty()) {
      return false;
    }
    auto work = std::move(ready_.front());
    ready_.pop_front();
    work();
    return true;
  }

  void run_until_idle() {
    while (run_one()) {
    }
  }

private:
  template <typename T> void schedule(std::shared_ptr<ActorTask<T>> task) {
    ready_.push_back([this, task = std::move(task)]() mutable {
      task->resume();
      if (!task->done()) {
        if (task->suspension() != ActorTaskSuspension::Yielded) {
          throw std::logic_error(
              "test scheduler cannot automatically resume this suspension");
        }
        schedule(std::move(task));
      }
    });
  }

  std::deque<std::function<void()>> ready_;
};

} // namespace obcx::core::test
