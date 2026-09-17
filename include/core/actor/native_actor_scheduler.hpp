#ifndef OBCX_INCLUDE_CORE_NATIVE_ACTOR_SCHEDULER_HPP_
#define OBCX_INCLUDE_CORE_NATIVE_ACTOR_SCHEDULER_HPP_

#include "core/actor/actor.hpp"
#include "core/actor/actor_work_stealing_executor.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace obcx::core {

struct NativeActorSlowResumeDiagnostic {
  uint64_t task_id = 0;
  std::string actor_id;
  std::string partition_key;
  uint64_t mailbox_generation = 0;
  size_t worker_id = ACTOR_TASK_NO_PREFERRED_WORKER;
  uint64_t elapsed_ns = 0;
};

[[nodiscard]] auto format_native_actor_slow_resume(
    const NativeActorSlowResumeDiagnostic &diagnostic) -> std::string;

struct NativeActorDurationHistogram {
  inline static constexpr std::array<uint64_t, 8> upper_bounds_ns = {
      1'000,      10'000,      100'000,       1'000'000,
      10'000'000, 100'000'000, 1'000'000'000, 10'000'000'000,
  };

  uint64_t count = 0;
  uint64_t total_ns = 0;
  uint64_t max_ns = 0;
  std::array<uint64_t, 9> buckets{};
};

enum class NativeActorMailboxStatus : uint8_t {
  Idle,
  Runnable,
  Running,
  Suspended,
  Stopping,
};

struct NativeActorSchedulerOptions {
  size_t worker_count = 0;
  size_t max_pending_tasks = 0;
  size_t max_pending_tasks_per_actor = 0;
  size_t max_pending_tasks_per_partition = 0;
  size_t slow_resume_warning_ms = 10;
  bool use_global_sharing = false;
  std::function<void(NativeActorSlowResumeDiagnostic)> slow_resume_handler;
};

struct NativeActorSchedulerMetrics {
  uint64_t accepted = 0;
  uint64_t rejected = 0;
  uint64_t completed = 0;
  uint64_t failed = 0;
  uint64_t cancelled = 0;
  uint64_t late_completions = 0;
  uint64_t yielded = 0;
  uint64_t runnable_transitions = 0;
  uint64_t running_resumes = 0;
  uint64_t suspensions = 0;
  uint64_t slow_resumes = 0;
  size_t pending = 0;
  size_t runnable = 0;
  size_t running = 0;
  size_t suspended = 0;
  size_t idle_mailboxes = 0;
  size_t runnable_mailboxes = 0;
  size_t running_mailboxes = 0;
  size_t suspended_mailboxes = 0;
  size_t stopping_mailboxes = 0;
  NativeActorDurationHistogram mailbox_queue_delay;
  NativeActorDurationHistogram actor_resume_duration;
  NativeActorDurationHistogram invocation_latency;
  ActorWorkStealingMetrics executor;
};

struct NativeActorSchedulerState;

class NativeActorScheduler {
public:
  using Completion = std::function<void(ActorResult)>;

  NativeActorScheduler();
  explicit NativeActorScheduler(NativeActorSchedulerOptions options);
  NativeActorScheduler(NativeActorSchedulerOptions options,
                       std::shared_ptr<ActorServices> runtime_services);
  NativeActorScheduler(const NativeActorScheduler &) = delete;
  auto operator=(const NativeActorScheduler &)
      -> NativeActorScheduler & = delete;
  NativeActorScheduler(NativeActorScheduler &&) = delete;
  auto operator=(NativeActorScheduler &&) -> NativeActorScheduler & = delete;
  ~NativeActorScheduler();

  void register_actor(std::shared_ptr<IActorV2> actor);

  template <typename Service>
  void register_service(std::shared_ptr<Service> service) {
    runtime_services_->register_service<Service>(std::move(service));
  }

  auto enqueue(ActorInvocation invocation, Completion completion) -> bool;
  auto cancel(const ActorId &actor_id, const std::string &partition_key,
              const std::string &message_id) -> bool;

  void shutdown(
      ActorExecutorShutdownMode mode = ActorExecutorShutdownMode::Drain);

  /** Release scheduler-owned actor aliases after shutdown has completed. */
  void release_actors() noexcept;

  [[nodiscard]] auto accepting() const noexcept -> bool;
  [[nodiscard]] auto metrics() const -> NativeActorSchedulerMetrics;

private:
  std::shared_ptr<ActorServices> runtime_services_;
  std::shared_ptr<NativeActorSchedulerState> state_;
};

} // namespace obcx::core

#endif // OBCX_INCLUDE_CORE_NATIVE_ACTOR_SCHEDULER_HPP_
