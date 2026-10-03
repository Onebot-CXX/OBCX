#include "core/actor/native_actor_scheduler.hpp"

#include "common/logger.hpp"

#include <boost/container/small_vector.hpp>

#include <algorithm>
#include <chrono>
#include <deque>
#include <exception>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace obcx::core {

auto format_native_actor_slow_resume(
    const NativeActorSlowResumeDiagnostic &diagnostic) -> std::string {
  return "slow actor resume task_id=" + std::to_string(diagnostic.task_id) +
         " actor_id=" + diagnostic.actor_id +
         " partition_key=" + diagnostic.partition_key + " mailbox_generation=" +
         std::to_string(diagnostic.mailbox_generation) +
         " worker_id=" + std::to_string(diagnostic.worker_id) +
         " elapsed_ns=" + std::to_string(diagnostic.elapsed_ns);
}

namespace {

void record_duration(NativeActorDurationHistogram &histogram,
                     const uint64_t elapsed_ns) {
  histogram.count++;
  histogram.total_ns += elapsed_ns;
  histogram.max_ns = std::max(histogram.max_ns, elapsed_ns);
  size_t bucket = 0;
  while (bucket < NativeActorDurationHistogram::upper_bounds_ns.size() &&
         elapsed_ns > NativeActorDurationHistogram::upper_bounds_ns[bucket]) {
    bucket++;
  }
  histogram.buckets[bucket]++;
}

void merge_duration(NativeActorDurationHistogram &target,
                    const NativeActorDurationHistogram &source) {
  target.count += source.count;
  target.total_ns += source.total_ns;
  target.max_ns = std::max(target.max_ns, source.max_ns);
  for (size_t index = 0; index < target.buckets.size(); ++index) {
    target.buckets[index] += source.buckets[index];
  }
}

auto elapsed_ns_since(const std::chrono::steady_clock::time_point start)
    -> uint64_t {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now() - start)
          .count());
}

struct PendingNativeInvocation {
  ActorInvocation invocation;
  NativeActorScheduler::Completion completion;
  std::shared_ptr<IActorV2> actor;
  std::shared_ptr<std::atomic_size_t> actor_pending_count;
  std::shared_ptr<std::atomic_size_t> partition_pending_count;
  std::chrono::steady_clock::time_point admitted_at =
      std::chrono::steady_clock::now();
};

struct NativeActorMailbox;

struct ActiveNativeInvocation {
  PendingNativeInvocation pending;
  std::shared_ptr<IActorV2> actor;
  std::shared_ptr<ActorCancellationState> cancellation =
      std::make_shared<ActorCancellationState>();
  ActorContext context;
  ActorTask<ActorResult> task;
  uint64_t generation = 0;
  uint64_t ready_io_epoch = 0;
  uint64_t completed_io_epoch = 0;
  size_t preferred_worker = ACTOR_TASK_NO_PREFERRED_WORKER;
  bool cancel_after_resume = false;
  bool started = false;

  ActiveNativeInvocation(PendingNativeInvocation pending_invocation,
                         std::shared_ptr<IActorV2> target_actor,
                         const std::shared_ptr<ActorServices> &services,
                         const uint64_t mailbox_generation)
      : pending(std::move(pending_invocation)), actor(std::move(target_actor)),
        context(pending.invocation.actor_id, services,
                pending.invocation.db_instance, pending.invocation.db_namespace,
                cancellation, actor),
        generation(mailbox_generation) {}
};

struct NativeActorMailbox {
  std::string key;
  mutable std::mutex mutex;
  NativeActorMailboxStatus status = NativeActorMailboxStatus::Idle;
  bool local_handoff_claimed = false;
  uint64_t generation = 0;
  std::deque<PendingNativeInvocation> pending;
  std::optional<ActiveNativeInvocation> active;
  uint64_t completed = 0;
  uint64_t failed = 0;
  uint64_t cancelled = 0;
  uint64_t yielded = 0;
  uint64_t suspensions = 0;
  uint64_t slow_resumes = 0;
  NativeActorDurationHistogram mailbox_queue_delay;
  NativeActorDurationHistogram actor_resume_duration;
  NativeActorDurationHistogram invocation_latency;
};

struct CompletionEvent {
  NativeActorScheduler::Completion completion;
  ActorResult result;
};

struct ScheduleRequest {
  std::shared_ptr<NativeActorMailbox> mailbox;
  uint64_t generation = 0;
  size_t preferred_worker = ACTOR_TASK_NO_PREFERRED_WORKER;
  bool internal = false;
};

using CompletionEvents = boost::container::small_vector<CompletionEvent, 2>;
using ScheduleRequests = boost::container::small_vector<ScheduleRequest, 2>;

auto mailbox_key_for_invocation(const ActorInvocation &invocation)
    -> std::string {
  const auto partition = invocation.partition_key.empty()
                             ? std::string{"global"}
                             : invocation.partition_key;
  return invocation.actor_id + ":" + partition;
}

auto backpressure_result() -> ActorResult {
  return ActorResult::failed(
      "scheduler_backpressure",
      "native actor scheduler pending task limit reached", true);
}

auto cancellation_result() -> ActorResult {
  return ActorResult::failed("scheduler_cancelled",
                             "native actor invocation was cancelled", true);
}

auto scheduler_stopped_result() -> ActorResult {
  return ActorResult::failed("scheduler_stopped",
                             "native actor scheduler is stopped", true);
}

void invoke_completions(CompletionEvents &&completions) {
  for (auto &event : completions) {
    if (!event.completion) {
      continue;
    }
    try {
      event.completion(std::move(event.result));
    } catch (...) {
      // Completion handlers are outside scheduler ownership. One throwing
      // handler must not prevent delivery to other callers.
    }
  }
}

} // namespace

struct NativeActorSchedulerState
    : public std::enable_shared_from_this<NativeActorSchedulerState> {
  NativeActorSchedulerOptions options;
  std::shared_ptr<ActorServices> runtime_services;
  ActorWorkStealingExecutor executor;
  std::unordered_map<std::string, std::shared_ptr<IActorV2>> actors;
  std::unordered_map<std::string, std::shared_ptr<NativeActorMailbox>>
      mailboxes;
  std::unordered_map<std::string, std::shared_ptr<std::atomic_size_t>>
      actor_pending_counts;
  std::unordered_map<std::string, std::shared_ptr<std::atomic_size_t>>
      partition_pending_counts;
  std::atomic_size_t pending_tasks = 0;
  std::atomic_uint64_t next_task_id = 1;
  bool accepting = true;
  std::atomic_bool stopping = false;
  std::atomic_bool cancelling = false;
  std::atomic_bool stopped = false;
  mutable std::mutex mutex;

  uint64_t accepted = 0;
  uint64_t rejected = 0;
  NativeActorSchedulerMetrics retired_mailbox_metrics;
  std::atomic_uint64_t late_completions = 0;
  std::atomic_uint64_t runnable_transitions = 0;
  std::atomic_uint64_t running_resumes = 0;

  NativeActorSchedulerState(NativeActorSchedulerOptions scheduler_options,
                            std::shared_ptr<ActorServices> services)
      : options(std::move(scheduler_options)),
        runtime_services(std::move(services)),
        executor(ActorWorkStealingExecutorOptions{
            .worker_count = options.worker_count,
        }) {}
};

namespace {

void accumulate_mailbox_metrics(NativeActorSchedulerMetrics &target,
                                const NativeActorMailbox &mailbox) {
  target.completed += mailbox.completed;
  target.failed += mailbox.failed;
  target.cancelled += mailbox.cancelled;
  target.yielded += mailbox.yielded;
  target.suspensions += mailbox.suspensions;
  target.slow_resumes += mailbox.slow_resumes;
  merge_duration(target.mailbox_queue_delay, mailbox.mailbox_queue_delay);
  merge_duration(target.actor_resume_duration, mailbox.actor_resume_duration);
  merge_duration(target.invocation_latency, mailbox.invocation_latency);
}

void reclaim_idle_mailbox(
    const std::shared_ptr<NativeActorSchedulerState> &state,
    const std::shared_ptr<NativeActorMailbox> &mailbox) {
  std::scoped_lock state_lock(state->mutex);
  const auto mailbox_it = state->mailboxes.find(mailbox->key);
  if (mailbox_it == state->mailboxes.end() ||
      mailbox_it->second.get() != mailbox.get()) {
    return;
  }

  std::scoped_lock mailbox_lock(mailbox->mutex);
  if (mailbox->status != NativeActorMailboxStatus::Idle || mailbox->active ||
      !mailbox->pending.empty() || mailbox->local_handoff_claimed) {
    return;
  }

  accumulate_mailbox_metrics(state->retired_mailbox_metrics, *mailbox);
  if (const auto counter = state->partition_pending_counts.find(mailbox->key);
      counter != state->partition_pending_counts.end() &&
      counter->second->load(std::memory_order_relaxed) == 0) {
    state->partition_pending_counts.erase(counter);
  }
  state->mailboxes.erase(mailbox_it);
}

void emit_slow_resume_diagnostic(
    const std::shared_ptr<NativeActorSchedulerState> &state,
    NativeActorSlowResumeDiagnostic diagnostic) {
  if (state->options.slow_resume_handler) {
    try {
      state->options.slow_resume_handler(std::move(diagnostic));
    } catch (...) {
      // Diagnostic consumers are outside scheduler ownership.
    }
    return;
  }
  OBCX_WARN("{}", format_native_actor_slow_resume(diagnostic));
}

auto pending_limit_exceeded(const NativeActorSchedulerState &state,
                            const ActorInvocation &invocation,
                            const std::string &mailbox_key) -> bool {
  if (state.options.max_pending_tasks > 0 &&
      state.pending_tasks.load(std::memory_order_relaxed) >=
          state.options.max_pending_tasks) {
    return true;
  }
  if (state.options.max_pending_tasks_per_actor > 0) {
    if (const auto it = state.actor_pending_counts.find(invocation.actor_id);
        it != state.actor_pending_counts.end() &&
        it->second->load(std::memory_order_relaxed) >=
            state.options.max_pending_tasks_per_actor) {
      return true;
    }
  }
  if (state.options.max_pending_tasks_per_partition > 0) {
    if (const auto it = state.partition_pending_counts.find(mailbox_key);
        it != state.partition_pending_counts.end() &&
        it->second->load(std::memory_order_relaxed) >=
            state.options.max_pending_tasks_per_partition) {
      return true;
    }
  }
  return false;
}

void increment_pending(NativeActorSchedulerState &state,
                       PendingNativeInvocation &pending,
                       const std::string &mailbox_key) {
  state.pending_tasks.fetch_add(1, std::memory_order_relaxed);
  if (state.options.max_pending_tasks_per_actor > 0) {
    auto &counter = state.actor_pending_counts[pending.invocation.actor_id];
    if (!counter) {
      counter = std::make_shared<std::atomic_size_t>(0);
    }
    counter->fetch_add(1, std::memory_order_relaxed);
    pending.actor_pending_count = counter;
  }
  if (state.options.max_pending_tasks_per_partition > 0) {
    auto &counter = state.partition_pending_counts[mailbox_key];
    if (!counter) {
      counter = std::make_shared<std::atomic_size_t>(0);
    }
    counter->fetch_add(1, std::memory_order_relaxed);
    pending.partition_pending_count = counter;
  }
}

void decrement_pending(NativeActorSchedulerState &state,
                       const PendingNativeInvocation &pending) {
  if (pending.actor_pending_count) {
    pending.actor_pending_count->fetch_sub(1, std::memory_order_relaxed);
  }
  if (pending.partition_pending_count) {
    pending.partition_pending_count->fetch_sub(1, std::memory_order_relaxed);
  }
  const auto previous =
      state.pending_tasks.fetch_sub(1, std::memory_order_acq_rel);
  if (previous == 1) {
    state.pending_tasks.notify_all();
  }
}

void run_mailbox(const std::shared_ptr<NativeActorSchedulerState> &state,
                 const std::shared_ptr<NativeActorMailbox> &mailbox,
                 uint64_t generation, size_t worker_id,
                 size_t local_handoffs = 0);

void fail_schedule_request(
    const std::shared_ptr<NativeActorSchedulerState> &state,
    const ScheduleRequest &request, CompletionEvents &completions);

void publish_request(const std::shared_ptr<NativeActorSchedulerState> &state,
                     const ScheduleRequest &request) {
  auto work = [state, mailbox = request.mailbox,
               generation = request.generation](const size_t worker_id) {
    run_mailbox(state, mailbox, generation, worker_id);
  };
  const auto accepted =
      request.internal
          ? state->executor.reschedule(std::move(work),
                                       state->options.use_global_sharing
                                           ? ACTOR_TASK_NO_PREFERRED_WORKER
                                           : request.preferred_worker)
          : state->executor.submit(std::move(work),
                                   state->options.use_global_sharing
                                       ? ACTOR_TASK_NO_PREFERRED_WORKER
                                       : request.preferred_worker);
  if (accepted) {
    return;
  }

  CompletionEvents completions;
  {
    std::scoped_lock mailbox_lock(request.mailbox->mutex);
    fail_schedule_request(state, request, completions);
  }
  invoke_completions(std::move(completions));
  reclaim_idle_mailbox(state, request.mailbox);
}

void publish_requests(const std::shared_ptr<NativeActorSchedulerState> &state,
                      ScheduleRequests &&requests) {
  for (const auto &request : requests) {
    publish_request(state, request);
  }
}

void on_external_ready(
    const std::weak_ptr<NativeActorSchedulerState> &weak_state,
    const std::weak_ptr<NativeActorMailbox> &weak_mailbox,
    const uint64_t generation, const uint64_t io_epoch) {
  auto state = weak_state.lock();
  auto mailbox = weak_mailbox.lock();
  if (!state || !mailbox) {
    return;
  }

  std::optional<ScheduleRequest> request;
  {
    std::scoped_lock lock(mailbox->mutex);
    if (!mailbox->active || mailbox->active->generation != generation ||
        state->cancelling.load(std::memory_order_acquire)) {
      state->late_completions.fetch_add(1, std::memory_order_relaxed);
      return;
    }

    // A completion can run before a nested ActorTask has propagated its
    // suspension epoch to the root task. Preserve it while the root is still
    // running; run_mailbox will consume the matching epoch after propagation.
    if (mailbox->status == NativeActorMailboxStatus::Running) {
      if (mailbox->active->ready_io_epoch == io_epoch ||
          mailbox->active->completed_io_epoch == io_epoch) {
        state->late_completions.fetch_add(1, std::memory_order_relaxed);
        return;
      }
      mailbox->active->ready_io_epoch = io_epoch;
      return;
    }
    if (mailbox->active->task.io_suspension_epoch() != io_epoch ||
        mailbox->active->completed_io_epoch == io_epoch) {
      state->late_completions.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    if (mailbox->status != NativeActorMailboxStatus::Suspended) {
      state->late_completions.fetch_add(1, std::memory_order_relaxed);
      return;
    }

    mailbox->status = NativeActorMailboxStatus::Runnable;
    mailbox->active->completed_io_epoch = io_epoch;
    state->runnable_transitions.fetch_add(1, std::memory_order_relaxed);
    request =
        ScheduleRequest{.mailbox = mailbox,
                        .generation = generation,
                        .preferred_worker = mailbox->active->preferred_worker,
                        .internal = true};
  }
  publish_request(state, *request);
}

void prepare_next_locked(
    const std::shared_ptr<NativeActorSchedulerState> &state,
    const std::shared_ptr<NativeActorMailbox> &mailbox,
    ScheduleRequests &requests, CompletionEvents &completions,
    size_t preferred_worker, const bool internal) {
  while (!mailbox->pending.empty() &&
         !state->cancelling.load(std::memory_order_acquire)) {
    auto pending = std::move(mailbox->pending.front());
    mailbox->pending.pop_front();
    if (!pending.actor) {
      decrement_pending(*state, pending);
      mailbox->failed++;
      record_duration(mailbox->invocation_latency,
                      elapsed_ns_since(pending.admitted_at));
      completions.push_back(CompletionEvent{
          .completion = std::move(pending.completion),
          .result = ActorResult::failed("actor_not_registered",
                                        "actor is not registered", false),
      });
      continue;
    }

    if (state->options.use_global_sharing ||
        preferred_worker == ACTOR_TASK_NO_PREFERRED_WORKER) {
      preferred_worker = ACTOR_TASK_NO_PREFERRED_WORKER;
    } else {
      preferred_worker %= state->executor.worker_count();
    }

    const auto generation = ++mailbox->generation;
    auto actor = std::move(pending.actor);
    mailbox->active.emplace(std::move(pending), std::move(actor),
                            state->runtime_services, generation);
    mailbox->active->preferred_worker = preferred_worker;
    mailbox->status = NativeActorMailboxStatus::Runnable;
    state->runnable_transitions.fetch_add(1, std::memory_order_relaxed);
    requests.push_back(ScheduleRequest{
        .mailbox = mailbox,
        .generation = generation,
        .preferred_worker = preferred_worker,
        .internal = internal,
    });
    return;
  }

  mailbox->status = state->stopping.load(std::memory_order_acquire)
                        ? NativeActorMailboxStatus::Stopping
                        : NativeActorMailboxStatus::Idle;
}

void complete_active_locked(
    const std::shared_ptr<NativeActorSchedulerState> &state,
    const std::shared_ptr<NativeActorMailbox> &mailbox, ActorResult result,
    ScheduleRequests &requests, CompletionEvents &completions,
    const bool cancelled = false) {
  if (!mailbox->active) {
    return;
  }
  const auto preferred_worker = mailbox->active->preferred_worker;
  auto pending = std::move(mailbox->active->pending);
  record_duration(mailbox->invocation_latency,
                  elapsed_ns_since(pending.admitted_at));
  decrement_pending(*state, pending);
  if (cancelled) {
    mailbox->cancelled++;
  } else if (result.ok()) {
    mailbox->completed++;
  } else {
    mailbox->failed++;
  }
  completions.push_back(CompletionEvent{
      .completion = std::move(pending.completion),
      .result = std::move(result),
  });
  mailbox->active.reset();
  prepare_next_locked(state, mailbox, requests, completions, preferred_worker,
                      true);
}

void fail_schedule_request(
    const std::shared_ptr<NativeActorSchedulerState> &state,
    const ScheduleRequest &request, CompletionEvents &completions) {
  if (!request.mailbox->active ||
      request.mailbox->active->generation != request.generation ||
      request.mailbox->status != NativeActorMailboxStatus::Runnable) {
    return;
  }
  ScheduleRequests ignored;
  complete_active_locked(state, request.mailbox, scheduler_stopped_result(),
                         ignored, completions, true);
}

void run_mailbox(const std::shared_ptr<NativeActorSchedulerState> &state,
                 const std::shared_ptr<NativeActorMailbox> &mailbox,
                 const uint64_t generation, const size_t worker_id,
                 const size_t local_handoffs) {
  ActorTask<ActorResult> *task = nullptr;
  std::shared_ptr<IActorV2> actor_to_start;
  std::shared_ptr<ActorCancellationState> cancellation_to_start;
  MessageEnvelope *message_to_start = nullptr;
  ActorContext *context_to_start = nullptr;
  NativeActorSlowResumeDiagnostic slow_diagnostic;
  std::optional<uint64_t> queue_delay_ns;
  {
    std::scoped_lock lock(mailbox->mutex);
    if (!mailbox->active || mailbox->active->generation != generation ||
        mailbox->status != NativeActorMailboxStatus::Runnable) {
      return;
    }
    mailbox->status = NativeActorMailboxStatus::Running;
    state->running_resumes.fetch_add(1, std::memory_order_relaxed);
    mailbox->active->preferred_worker = worker_id;
    if (!mailbox->active->task.valid()) {
      actor_to_start = mailbox->active->actor;
      cancellation_to_start = mailbox->active->cancellation;
      message_to_start = &mailbox->active->pending.invocation.message;
      context_to_start = &mailbox->active->context;
    } else {
      mailbox->active->task.runtime().preferred_worker = worker_id;
      task = &mailbox->active->task;
    }
    if (!mailbox->active->started) {
      mailbox->active->started = true;
      queue_delay_ns = elapsed_ns_since(mailbox->active->pending.admitted_at);
    }
  }

  // A custom IActorV2 implementation can execute arbitrary code before it
  // returns an ActorTask (and coroutine-frame allocation itself can throw).
  // Invoke that boundary without either scheduler mutex held, then publish the
  // task back into the still-running mailbox.
  if (actor_to_start) {
    ActorTask<ActorResult> started_task;
    std::exception_ptr start_error;
    try {
      started_task =
          actor_to_start->handle_message(*message_to_start, *context_to_start);
      auto weak_state = std::weak_ptr<NativeActorSchedulerState>{state};
      auto weak_mailbox = std::weak_ptr<NativeActorMailbox>{mailbox};
      const auto task_id =
          state->next_task_id.fetch_add(1, std::memory_order_relaxed);
      started_task.attach_runtime(ActorTaskRuntimeContext{
          .scheduler = state.get(),
          .task_id = task_id,
          .mailbox_generation = generation,
          .preferred_worker = worker_id,
          .cancellation = std::move(cancellation_to_start),
          .completion_target = {},
          .make_runnable =
              [weak_state, weak_mailbox, generation](const uint64_t io_epoch) {
                on_external_ready(weak_state, weak_mailbox, generation,
                                  io_epoch);
              },
      });
    } catch (...) {
      start_error = std::current_exception();
    }

    if (start_error) {
      ActorResult result;
      try {
        std::rethrow_exception(start_error);
      } catch (const std::exception &error) {
        result = ActorResult::failed("actor_exception", error.what(), true);
      } catch (...) {
        result = ActorResult::failed("actor_exception",
                                     "unknown actor exception", true);
      }
      ScheduleRequests requests;
      CompletionEvents completions;
      {
        std::scoped_lock mailbox_lock(mailbox->mutex);
        if (mailbox->active && mailbox->active->generation == generation) {
          complete_active_locked(state, mailbox, std::move(result), requests,
                                 completions);
        }
      }
      invoke_completions(std::move(completions));
      publish_requests(state, std::move(requests));
      reclaim_idle_mailbox(state, mailbox);
      return;
    }

    {
      std::scoped_lock mailbox_lock(mailbox->mutex);
      if (!mailbox->active || mailbox->active->generation != generation) {
        return;
      }
      mailbox->active->task = std::move(started_task);
      task = &mailbox->active->task;
    }
  }

  const auto resume_started = std::chrono::steady_clock::now();
  try {
    task->resume();
  } catch (...) {
    // ActorTask normally captures exceptions in promise_type. This protects the
    // scheduler from misuse such as resuming an invalid task.
  }
  const auto resume_elapsed = std::chrono::steady_clock::now() - resume_started;
  const auto resume_elapsed_ns = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(resume_elapsed)
          .count());
  const auto slow_resume =
      state->options.slow_resume_warning_ms > 0 &&
      resume_elapsed >=
          std::chrono::milliseconds(state->options.slow_resume_warning_ms);

  ScheduleRequests requests;
  CompletionEvents completions;
  bool owns_idle_mailbox = false;
  {
    std::scoped_lock mailbox_lock(mailbox->mutex);
    if (queue_delay_ns) {
      record_duration(mailbox->mailbox_queue_delay, *queue_delay_ns);
    }
    record_duration(mailbox->actor_resume_duration, resume_elapsed_ns);
    if (slow_resume) {
      slow_diagnostic = NativeActorSlowResumeDiagnostic{
          .task_id = task->runtime().task_id,
          .actor_id = mailbox->active->pending.invocation.actor_id,
          .partition_key =
              mailbox->active->pending.invocation.partition_key.empty()
                  ? std::string{"global"}
                  : mailbox->active->pending.invocation.partition_key,
          .mailbox_generation = generation,
          .worker_id = worker_id,
          .elapsed_ns = resume_elapsed_ns,
      };
      mailbox->slow_resumes++;
    }
    if (!mailbox->active || mailbox->active->generation != generation) {
      return;
    }

    if (state->cancelling.load(std::memory_order_acquire) ||
        mailbox->active->cancel_after_resume) {
      complete_active_locked(state, mailbox, cancellation_result(), requests,
                             completions, true);
    } else if (task->done()) {
      ActorResult result;
      try {
        result = task->take_result();
      } catch (const std::exception &error) {
        result = ActorResult::failed("actor_exception", error.what(), true);
      } catch (...) {
        result = ActorResult::failed("actor_exception",
                                     "unknown actor exception", true);
      }
      complete_active_locked(state, mailbox, std::move(result), requests,
                             completions);
    } else if (task->suspension() == ActorTaskSuspension::Yielded) {
      mailbox->yielded++;
      mailbox->status = NativeActorMailboxStatus::Runnable;
      state->runnable_transitions.fetch_add(1, std::memory_order_relaxed);
      requests.push_back(ScheduleRequest{
          .mailbox = mailbox,
          .generation = generation,
          .preferred_worker = worker_id,
          .internal = true,
      });
    } else if (task->suspension() == ActorTaskSuspension::AwaitingIo) {
      mailbox->suspensions++;
      const auto epoch = task->io_suspension_epoch();
      if (mailbox->active->ready_io_epoch == epoch) {
        mailbox->active->ready_io_epoch = 0;
        mailbox->active->completed_io_epoch = epoch;
        mailbox->status = NativeActorMailboxStatus::Runnable;
        state->runnable_transitions.fetch_add(1, std::memory_order_relaxed);
        requests.push_back(ScheduleRequest{
            .mailbox = mailbox,
            .generation = generation,
            .preferred_worker = worker_id,
            .internal = true,
        });
      } else {
        mailbox->status = NativeActorMailboxStatus::Suspended;
      }
    } else {
      complete_active_locked(
          state, mailbox,
          ActorResult::failed("actor_invalid_suspension",
                              "actor task suspended without a runtime boundary",
                              false),
          requests, completions);
    }

    // A recurrent mailbox can briefly become empty while its producer is
    // still publishing the next invocation. Keep a one-yield local claim so
    // that enqueue can hand the next item directly to this worker instead of
    // creating a fresh executor item and futex wake. First-use mailboxes skip
    // the grace period, preserving the cost profile for isolated messages.
    if (!state->options.use_global_sharing && requests.empty() &&
        !mailbox->active && mailbox->status == NativeActorMailboxStatus::Idle &&
        mailbox->generation > 1) {
      mailbox->local_handoff_claimed = true;
      owns_idle_mailbox = true;
    }
  }

  if (slow_resume) {
    emit_slow_resume_diagnostic(state, std::move(slow_diagnostic));
  }
  invoke_completions(std::move(completions));

  if (owns_idle_mailbox) {
    CompletionEvents handoff_completions;
    constexpr size_t max_idle_handoff_yields = 4;
    for (size_t attempt = 0; attempt < max_idle_handoff_yields; ++attempt) {
      std::this_thread::yield();
      bool finalized = false;
      {
        std::scoped_lock mailbox_lock(mailbox->mutex);
        if (mailbox->active ||
            mailbox->status != NativeActorMailboxStatus::Idle ||
            !mailbox->local_handoff_claimed) {
          break;
        }
        if (!mailbox->pending.empty() ||
            attempt + 1 == max_idle_handoff_yields) {
          mailbox->local_handoff_claimed = false;
          prepare_next_locked(state, mailbox, requests, handoff_completions,
                              worker_id, true);
          finalized = true;
        }
      }
      if (finalized) {
        break;
      }
    }
    invoke_completions(std::move(handoff_completions));
  }

  constexpr size_t max_local_handoffs = 15;
  std::optional<ScheduleRequest> local_handoff;
  if (!state->options.use_global_sharing &&
      local_handoffs < max_local_handoffs && requests.size() == 1 &&
      requests.front().internal &&
      requests.front().preferred_worker == worker_id) {
    local_handoff = std::move(requests.front());
    requests.clear();
  }

  if (local_handoff) {
    run_mailbox(state, local_handoff->mailbox, local_handoff->generation,
                worker_id, local_handoffs + 1);
    return;
  }
  publish_requests(state, std::move(requests));
  reclaim_idle_mailbox(state, mailbox);
}

} // namespace

NativeActorScheduler::NativeActorScheduler()
    : NativeActorScheduler(NativeActorSchedulerOptions{}) {}

NativeActorScheduler::NativeActorScheduler(NativeActorSchedulerOptions options)
    : NativeActorScheduler(std::move(options),
                           std::make_shared<ActorServices>()) {}

NativeActorScheduler::NativeActorScheduler(
    NativeActorSchedulerOptions options,
    std::shared_ptr<ActorServices> runtime_services)
    : runtime_services_(std::move(runtime_services)),
      state_(std::make_shared<NativeActorSchedulerState>(std::move(options),
                                                         runtime_services_)) {}

NativeActorScheduler::~NativeActorScheduler() {
  shutdown(ActorExecutorShutdownMode::Cancel);
}

void NativeActorScheduler::register_actor(std::shared_ptr<IActorV2> actor) {
  if (!actor) {
    return;
  }
  std::scoped_lock lock(state_->mutex);
  state_->actors[actor->get_name()] = std::move(actor);
}

auto NativeActorScheduler::enqueue(ActorInvocation invocation,
                                   Completion completion) -> bool {
  ScheduleRequests requests;
  CompletionEvents completions;
  std::shared_ptr<NativeActorMailbox> touched_mailbox;
  bool accepted = false;
  {
    std::scoped_lock lock(state_->mutex);
    if (!state_->accepting) {
      state_->rejected++;
      completions.push_back(CompletionEvent{
          .completion = std::move(completion),
          .result = scheduler_stopped_result(),
      });
    } else {
      const auto mailbox_key = mailbox_key_for_invocation(invocation);
      if (pending_limit_exceeded(*state_, invocation, mailbox_key)) {
        state_->rejected++;
        completions.push_back(CompletionEvent{
            .completion = std::move(completion),
            .result = backpressure_result(),
        });
      } else {
        accepted = true;
        state_->accepted++;
        std::shared_ptr<IActorV2> actor;
        if (const auto actor_it = state_->actors.find(invocation.actor_id);
            actor_it != state_->actors.end()) {
          actor = actor_it->second;
        }
        auto pending = PendingNativeInvocation{
            .invocation = std::move(invocation),
            .completion = std::move(completion),
            .actor = std::move(actor),
        };
        increment_pending(*state_, pending, mailbox_key);
        auto &mailbox = state_->mailboxes[mailbox_key];
        if (!mailbox) {
          mailbox = std::make_shared<NativeActorMailbox>();
          mailbox->key = mailbox_key;
        }
        touched_mailbox = mailbox;
        std::scoped_lock mailbox_lock(mailbox->mutex);
        mailbox->pending.push_back(std::move(pending));
        if (mailbox->status == NativeActorMailboxStatus::Idle &&
            !mailbox->local_handoff_claimed) {
          prepare_next_locked(state_, mailbox, requests, completions,
                              ACTOR_TASK_NO_PREFERRED_WORKER, false);
        }
      }
    }
  }

  invoke_completions(std::move(completions));
  publish_requests(state_, std::move(requests));
  if (touched_mailbox) {
    reclaim_idle_mailbox(state_, touched_mailbox);
  }
  return accepted;
}

auto NativeActorScheduler::cancel(const ActorId &actor_id,
                                  const std::string &partition_key,
                                  const std::string &message_id) -> bool {
  ScheduleRequests requests;
  CompletionEvents completions;
  std::shared_ptr<NativeActorMailbox> touched_mailbox;
  bool cancelled = false;
  {
    std::scoped_lock state_lock(state_->mutex);
    const auto mailbox_key =
        actor_id + ":" +
        (partition_key.empty() ? std::string{"global"} : partition_key);
    const auto mailbox_it = state_->mailboxes.find(mailbox_key);
    if (mailbox_it == state_->mailboxes.end()) {
      return false;
    }
    touched_mailbox = mailbox_it->second;
    std::scoped_lock mailbox_lock(touched_mailbox->mutex);

    const auto pending = std::ranges::find_if(
        touched_mailbox->pending, [&message_id](const auto &invocation) {
          return invocation.invocation.message.id == message_id;
        });
    if (pending != touched_mailbox->pending.end()) {
      auto removed = std::move(*pending);
      touched_mailbox->pending.erase(pending);
      record_duration(touched_mailbox->invocation_latency,
                      elapsed_ns_since(removed.admitted_at));
      decrement_pending(*state_, removed);
      touched_mailbox->cancelled++;
      completions.push_back(CompletionEvent{
          .completion = std::move(removed.completion),
          .result = cancellation_result(),
      });
      cancelled = true;
    } else if (touched_mailbox->active &&
               touched_mailbox->active->pending.invocation.message.id ==
                   message_id) {
      touched_mailbox->active->cancellation->request_cancellation();
      touched_mailbox->active->task.request_cancellation();
      if (touched_mailbox->status == NativeActorMailboxStatus::Running) {
        touched_mailbox->active->cancel_after_resume = true;
      } else {
        complete_active_locked(state_, touched_mailbox, cancellation_result(),
                               requests, completions, true);
      }
      cancelled = true;
    }
  }

  invoke_completions(std::move(completions));
  publish_requests(state_, std::move(requests));
  if (touched_mailbox) {
    reclaim_idle_mailbox(state_, touched_mailbox);
  }
  return cancelled;
}

void NativeActorScheduler::shutdown(const ActorExecutorShutdownMode mode) {
  CompletionEvents completions;
  {
    std::unique_lock lock(state_->mutex);
    if (state_->stopped.load(std::memory_order_acquire)) {
      return;
    }
    if (state_->stopping.exchange(true, std::memory_order_acq_rel)) {
      lock.unlock();
      state_->stopped.wait(false, std::memory_order_acquire);
      return;
    }
    state_->accepting = false;

    if (mode == ActorExecutorShutdownMode::Drain) {
      lock.unlock();
      auto remaining = state_->pending_tasks.load(std::memory_order_acquire);
      while (remaining != 0) {
        state_->pending_tasks.wait(remaining, std::memory_order_acquire);
        remaining = state_->pending_tasks.load(std::memory_order_acquire);
      }
    } else {
      state_->cancelling.store(true, std::memory_order_release);
      for (auto &[key, mailbox] : state_->mailboxes) {
        (void)key;
        std::scoped_lock mailbox_lock(mailbox->mutex);
        while (!mailbox->pending.empty()) {
          auto pending = std::move(mailbox->pending.front());
          mailbox->pending.pop_front();
          record_duration(mailbox->invocation_latency,
                          elapsed_ns_since(pending.admitted_at));
          decrement_pending(*state_, pending);
          mailbox->cancelled++;
          completions.push_back(CompletionEvent{
              .completion = std::move(pending.completion),
              .result = cancellation_result(),
          });
        }
        if (!mailbox->active) {
          mailbox->local_handoff_claimed = false;
          mailbox->status = NativeActorMailboxStatus::Stopping;
          continue;
        }
        mailbox->active->task.request_cancellation();
        if (mailbox->status == NativeActorMailboxStatus::Running) {
          mailbox->active->cancel_after_resume = true;
          continue;
        }
        ScheduleRequests ignored;
        complete_active_locked(state_, mailbox, cancellation_result(), ignored,
                               completions, true);
        mailbox->status = NativeActorMailboxStatus::Stopping;
      }
    }
  }

  invoke_completions(std::move(completions));
  state_->executor.shutdown(ActorExecutorShutdownMode::Drain);

  state_->stopped.store(true, std::memory_order_release);
  state_->stopped.notify_all();
}

void NativeActorScheduler::release_actors() noexcept {
  try {
    shutdown(ActorExecutorShutdownMode::Cancel);
    std::scoped_lock lock(state_->mutex);
    state_->actors.clear();
  } catch (...) {
  }
}

auto NativeActorScheduler::accepting() const noexcept -> bool {
  std::scoped_lock lock(state_->mutex);
  return state_->accepting;
}

auto NativeActorScheduler::metrics() const -> NativeActorSchedulerMetrics {
  NativeActorSchedulerMetrics result;
  std::scoped_lock lock(state_->mutex);
  result.accepted = state_->accepted;
  result.rejected = state_->rejected;
  result.late_completions =
      state_->late_completions.load(std::memory_order_relaxed);
  result.runnable_transitions =
      state_->runnable_transitions.load(std::memory_order_relaxed);
  result.running_resumes =
      state_->running_resumes.load(std::memory_order_relaxed);
  result.pending = state_->pending_tasks.load(std::memory_order_relaxed);
  result.completed = state_->retired_mailbox_metrics.completed;
  result.failed = state_->retired_mailbox_metrics.failed;
  result.cancelled = state_->retired_mailbox_metrics.cancelled;
  result.yielded = state_->retired_mailbox_metrics.yielded;
  result.suspensions = state_->retired_mailbox_metrics.suspensions;
  result.slow_resumes = state_->retired_mailbox_metrics.slow_resumes;
  result.mailbox_queue_delay =
      state_->retired_mailbox_metrics.mailbox_queue_delay;
  result.actor_resume_duration =
      state_->retired_mailbox_metrics.actor_resume_duration;
  result.invocation_latency =
      state_->retired_mailbox_metrics.invocation_latency;
  for (const auto &[key, mailbox] : state_->mailboxes) {
    (void)key;
    std::scoped_lock mailbox_lock(mailbox->mutex);
    accumulate_mailbox_metrics(result, *mailbox);
    switch (mailbox->status) {
    case NativeActorMailboxStatus::Idle:
      result.idle_mailboxes++;
      break;
    case NativeActorMailboxStatus::Runnable:
      result.runnable_mailboxes++;
      result.runnable++;
      break;
    case NativeActorMailboxStatus::Running:
      result.running_mailboxes++;
      result.running++;
      break;
    case NativeActorMailboxStatus::Suspended:
      result.suspended_mailboxes++;
      result.suspended++;
      break;
    case NativeActorMailboxStatus::Stopping:
      result.stopping_mailboxes++;
      break;
    }
  }
  result.executor = state_->executor.metrics();
  return result;
}

} // namespace obcx::core
