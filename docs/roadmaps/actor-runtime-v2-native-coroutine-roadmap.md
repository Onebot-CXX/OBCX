# Actor Runtime V2 Native Coroutine And Work-Stealing Roadmap

> Historical rollout record. It preserves the pre-cutover implementation and
> admission sequence; use `actor-only-runtime-cutover-roadmap.md` and the
> current architecture guides for supported behavior.

> Status options: `TODO`, `DOING`, `DONE`, `BLOCKED`, `DEFERRED`.

Status: `TODO`

## Goal

Separate actor execution from Boost.Asio coroutine execution so OBCX can own
mailbox scheduling, cooperative suspension, continuation placement, and work
stealing while continuing to use Boost.Asio for networking, timers, and other
I/O operations.

The target execution boundary is:

```text
Boost.Asio ingress / network coroutine
  -> submits an actor invocation

ActorScheduler
  -> places a runnable actor continuation on a worker deque
  -> an idle worker may steal the continuation
  -> resumes ActorTask until completion or suspension

ActorTask awaiting I/O
  -> suspends without occupying an actor worker
  -> starts an operation on an Asio executor

Asio completion
  -> stores the result or exception
  -> re-enqueues the ActorTask continuation
  -> never resumes actor code inline on the I/O thread
```

## Motivation

The V1 scheduler hashes every `actor_id + partition_key` mailbox to a fixed
shard and runs one drainer coroutine per shard. This preserves mailbox order,
but unrelated mailboxes that collide on the same shard block one another. The
scheduler also inherits the executor of the enqueue caller and uses
`boost::asio::awaitable` in the public actor interface, preventing the actor
runtime from independently controlling coroutine placement and resumption.

A scheduler-owned C++20 coroutine type gives OBCX a resumable work item that can
move between actor workers. Boost.Asio remains the I/O substrate, but I/O
completion becomes an event that makes an actor continuation runnable again.

## Architecture Locks

- OBCX uses standard C++20 coroutines and implements an OBCX coroutine return
  type; it does not implement compiler-level coroutine machinery.
- The V2 actor coroutine type is named `ActorTask<T>`.
- The existing message scheduling payload named `ActorTask` is renamed to
  `ActorInvocation` before introducing the coroutine template.
- `ActorScheduler` owns the actor worker pool and all actor continuation
  placement.
- Boost.Asio remains responsible for sockets, HTTP/WebSocket operations,
  timers, and I/O completion delivery.
- Actor code is never resumed directly by an Asio completion callback. The
  callback must enqueue the continuation into `ActorScheduler`.
- Work stealing operates on runnable mailbox continuations, never on individual
  pending messages inside a mailbox.
- At most one actor coroutine is active for a mailbox identified by
  `actor_id + partition_key`.
- A mailbox remains occupied while its current actor coroutine is suspended for
  I/O. Later messages for that mailbox remain queued.
- Different partitions of one actor may execute concurrently, preserving the V1
  partition model. Actor implementations that share state across partitions
  must remain thread-safe.
- Actor execution is cooperative. The scheduler does not preempt actor code.
  Long CPU work must explicitly yield or use blocking/CPU middleware.
- V1 actors remain loadable through a compatibility adapter until V2 has passed
  the rollout gates.
- Actor ABI generation is distinct from an actor's semantic implementation
  version.

## Non-Goals

- Replacing Boost.Asio in networking or bot protocol implementations.
- Rewriting all bridge QQ/Telegram forwarding coroutines as `ActorTask`.
- Preemptive interruption of actor code.
- Distributed scheduling or stealing work between processes.
- Durable mailbox persistence, replay, offsets, or broker semantics.
- A lock-free deque in the first implementation. Correctness and observability
  come before queue micro-optimization.
- Concurrent processing of two messages from the same mailbox.

## Terminology

```text
ActorInvocation
  The actor id, partition key, DB binding, and MessageEnvelope submitted to the
  scheduler. This replaces the current non-coroutine ActorTask struct.

ActorTask<T>
  A move-only C++20 coroutine return type controlled by ActorScheduler.

Mailbox
  FIFO queue and state for one actor_id + partition_key pair.

ActorContinuation
  A runnable coroutine handle plus mailbox ownership and scheduling metadata.

Actor worker
  Scheduler-owned thread that resumes ActorContinuation objects.

I/O executor
  Boost.Asio executor that owns networking, timers, and I/O completion handlers.
```

## Required Mailbox State Machine

```text
                 first message
       Idle ------------------------> Runnable
        ^                                |
        | current task completes         | worker claims continuation
        | and no pending message          v
        +----------------------------- Running
                                          |
                    +---------------------+---------------------+
                    |                                           |
                    | co_await I/O                              | cooperative yield
                    v                                           v
                Suspended                                    Runnable
                    |
                    | I/O completion enqueues continuation
                    v
                Runnable

Running -- task completes with pending messages --> Runnable
Running -- task completes without pending messages -> Idle
```

State requirements:

- A mailbox transition to `Runnable` publishes exactly one continuation.
- Only a worker that successfully claims `Runnable -> Running` may resume it.
- `Running -> Suspended` removes the continuation from all runnable queues.
- An I/O completion performs `Suspended -> Runnable` exactly once.
- Cancellation and late completion must not resume a destroyed coroutine.
- Completion of one message publishes its result before the next message for
  that mailbox begins.
- Every state transition must have a defined happens-before edge so an actor may
  safely migrate between worker threads.

## Work-Stealing Policy

Initial policy:

- Create a configurable number of actor workers.
- Give each worker a mutex-protected double-ended runnable deque.
- The owning worker pushes and pops from its local end.
- An idle worker chooses another worker as a victim and steals from the opposite
  end.
- External submissions enter a global injector queue or are distributed to
  workers with a low-contention round-robin injector.
- A continuation resumed from I/O prefers its previous worker for locality but
  remains stealable.
- A worker that has no local, injected, or stealable work sleeps on a scheduler
  notification primitive rather than polling indefinitely.
- A global runnable-work count and wake-up protocol must prevent lost wakeups.
- Victim selection begins with randomized selection. Topology- or NUMA-aware
  selection is deferred until benchmarks justify it.

The first implementation may use `std::mutex`, `std::deque`, and
`std::condition_variable` or `std::counting_semaphore`. A lock-free Chase-Lev
deque is a later optimization with its own correctness and benchmark gate.

## Cooperative Execution Contract

Calling `resume()` runs an `ActorTask` until one of these boundaries:

- `co_return` or an uncaught exception;
- `co_await context.yield()`;
- `co_await` of an OBCX asynchronous operation;
- `co_await` of an Asio bridge operation;
- cancellation observed at a cancellation point.

The runtime cannot safely preempt arbitrary C++ actor code. Add a configurable
slow-resume warning that records actor id, partition, worker, and elapsed resume
time. CPU-heavy and blocking operations must be offloaded through an
`ActorContext` service.

## ActorTask Contract

`ActorTask<T>` must provide:

- move-only ownership of a coroutine handle;
- `promise_type` result or exception storage;
- scheduler and mailbox context attachment before first resume;
- `initial_suspend` so the scheduler owns the first execution;
- `final_suspend` that reports completion without resuming arbitrary code
  inline;
- correct destruction for never-started, completed, cancelled, and suspended
  frames;
- an awaiter for composing one `ActorTask` from another without bypassing the
  scheduler;
- cancellation state access;
- debug-only lifecycle assertions and a stable task identifier;
- allocator customization as a deferred optimization, not a V2 prerequisite.

The public V2 actor interface is expected to resemble:

```cpp
class IActorV2 {
public:
  virtual ~IActorV2() = default;

  [[nodiscard]] virtual auto get_name() const -> std::string = 0;
  [[nodiscard]] virtual auto get_version() const -> std::string = 0;

  virtual auto handle_message(const MessageEnvelope &message,
                              ActorContext &context)
      -> ActorTask<ActorResult> = 0;
};
```

## Boost.Asio Boundary

Two adapters are required.

### Asio Caller To Actor Runtime

Expose actor submission as a normal Asio asynchronous initiating operation:

```cpp
template <typename CompletionToken>
auto async_enqueue(ActorInvocation invocation, CompletionToken &&token);
```

This allows the existing Asio orchestrator to use `use_awaitable`, a callback,
or another completion token without making actor execution an Asio coroutine.
The completion handler must run on its associated executor.

### Actor Runtime To Asio

Provide a migration adapter in `ActorContext`:

```cpp
template <typename Factory>
auto await_asio(boost::asio::any_io_executor executor, Factory &&factory);
```

The factory may initially return `boost::asio::awaitable<T>`. The adapter:

1. captures scheduler, mailbox, coroutine handle, and cancellation state;
2. suspends the actor coroutine;
3. starts the Asio coroutine with `co_spawn` on the selected I/O executor;
4. stores the returned value or exception in shared operation state;
5. enqueues the actor continuation exactly once;
6. ignores or safely records a late completion after cancellation/destruction.

The long-term adapter should use Boost.Asio's completion-token model directly
so actor code can await Asio operations without nesting an Asio coroutine. Add
an OBCX completion token only after the migration adapter is correct and tested.

## Cancellation And Shutdown Rules

- Scheduler shutdown first rejects new actor submissions.
- Runnable continuations drain or cancel according to shutdown mode.
- Suspended operations receive cancellation when their underlying service
  supports it.
- Coroutine frames remain owned until completion acknowledges cancellation or a
  safe detached-operation state owns all completion data.
- A late I/O callback may release state but must never resume an abandoned
  mailbox.
- Awaiting callers receive a defined cancellation failure rather than hanging.
- Worker threads join only after no runnable continuation can be published.
- No actor coroutine is detached without runtime-owned lifetime tracking.

## ABI And Compatibility Strategy

The current `IActor::handle_message` return type is part of the standalone actor
SDK ABI. V2 must not silently reinterpret a V1 actor.

Required compatibility work:

- Introduce an explicit numeric actor ABI generation query.
- Add versioned V2 create/destroy symbols or an equivalent versioned factory
  table.
- Keep the current V1 symbols and loader path unchanged during migration.
- Add an `AsioActorV1Adapter` that invokes the old
  `boost::asio::awaitable<ActorResult>` on an I/O executor and returns completion
  to the native actor scheduler.
- Add `OBCX_ACTOR_EXPORT_V2` for new actors.
- Reject unsupported ABI generations with an actionable loader error.
- Update SDK installation, CMake helpers, fixture actors, and generic external
  SDK-consumer smoke tests.
- Document compiler and standard-library compatibility requirements for passing
  coroutine frames across shared-library boundaries.
- Do not remove V1 until the generic external SDK smoke harness has used V2
  for at least one compatibility window.

## Configuration Shape

Proposed configuration:

```toml
[actor_runtime.scheduler]
engine = "native-v2"
policy = "stealing"
workers = 0                 # 0 = runtime-selected
slow_resume_warning_ms = 10

[actor_runtime.compatibility]
allow_v1_actors = true
```

During rollout, `engine = "asio-v1"` remains available as the rollback path.
Actor-worker, I/O, and blocking-worker counts must be selected from one runtime
thread-budget policy to avoid accidental oversubscription.

## Observability

Add counters and histograms for:

- submissions accepted and rejected;
- runnable, running, and I/O-suspended mailboxes;
- local deque pushes and pops;
- steal attempts, successful steals, and stolen batch size;
- injector queue depth and per-worker queue depth;
- actor resume duration;
- mailbox queue delay and end-to-end invocation latency;
- explicit yields;
- actor completions, failures, exceptions, and cancellations;
- late I/O completions after cancellation;
- worker sleep and wake transitions.

Debug logging must include task id, actor id, partition key, mailbox generation,
and worker id without logging message payloads by default.

## Implementation Phases

### Phase 0: Baseline And Decision Record

Status: `COMPLETE`

- [x] Add an ADR accepting the native actor coroutine and Asio boundary.
- [x] Record current V1 behavior, thread topology, and shutdown behavior.
- [x] Add a forced same-shard collision benchmark for the V1 scheduler.
- [x] Record balanced, skewed, I/O-heavy, and completion-storm baselines.
- [x] Define performance acceptance thresholds from those baselines.
- [x] Reserve `native-v2` configuration keys without enabling them by default.

Exit criteria:

- [x] The actor/Asio execution boundary and mailbox suspension semantics are
      accepted.
- [x] Baselines and rollout thresholds are checked into the repository.
- [x] V1 behavior has regression tests before implementation begins.

### Phase 1: ActorTask Prototype

Status: `COMPLETE`

- [x] Rename the existing scheduling payload `ActorTask` to `ActorInvocation`.
- [x] Implement `ActorTask<void>` and `ActorTask<T>`.
- [x] Implement result, exception, initial-suspend, and final-suspend behavior.
- [x] Add an inline deterministic test scheduler for unit tests.
- [x] Implement `context.yield()` and cancellation observation.
- [x] Add debug lifecycle validation for frame ownership and destruction.

Exit criteria:

- [x] A task never runs before explicit scheduler submission.
- [x] Values and exceptions cross the coroutine boundary exactly once.
- [x] Suspended and never-started tasks are destroyed without leaks or double
      destruction.
- [x] AddressSanitizer and UndefinedBehaviorSanitizer tests pass.

### Phase 2: Native Work-Stealing Executor

Status: `COMPLETE`

- [x] Implement scheduler-owned worker threads.
- [x] Implement mutex-protected worker deques.
- [x] Implement external injection and preferred-worker requeue.
- [x] Implement randomized victim selection and opposite-end stealing.
- [x] Implement idle sleep/wake without lost wakeups.
- [x] Implement graceful and cancelling shutdown modes.
- [x] Add worker and stealing metrics.

Exit criteria:

- [x] Every submitted synthetic continuation executes exactly once.
- [x] Idle workers steal under forced skew.
- [x] No worker remains asleep while runnable work exists.
- [x] ThreadSanitizer stress tests report no scheduler data races.
- [x] Worker startup and shutdown are repeatable in a looped stress test.

### Phase 3: Mailbox Integration

Status: `COMPLETE`

- [x] Implement the mailbox state machine from this roadmap.
- [x] Retain FIFO pending-message order per actor partition.
- [x] Make only runnable continuations visible to worker queues.
- [x] Keep a suspended mailbox exclusively owned by its current task.
- [x] Preserve global, per-actor, and per-partition backpressure.
- [x] Replace V2 promise/future timer polling with event-driven completion.
- [x] Remove fixed-shard assignment from the V2 engine.

Exit criteria:

- [x] Same-mailbox maximum concurrency is exactly one.
- [x] Different mailboxes cannot block merely because their hashes collide.
- [x] Forced enqueue, completion, cancellation, and shutdown races lose no
      messages and resume no task twice.
- [x] Backpressure counts queued, running, and suspended work consistently.

### Phase 4: Boost.Asio Bridge

Status: `COMPLETE`

- [x] Implement `ActorContext::await_asio` for value and void results.
- [x] Propagate Asio exceptions into `ActorTask`.
- [x] Enforce scheduler requeue instead of inline I/O-thread resume.
- [x] Implement cancellation and late-completion protection.
- [x] Implement `ActorScheduler::async_enqueue` with completion-token support.
- [x] Verify associated-executor delivery for enqueue completion handlers.
- [x] Prototype a direct OBCX completion token for Asio operations.

Exit criteria:

- [x] An actor suspending on a timer does not occupy an actor worker.
- [x] I/O completion makes the actor continuation stealable.
- [x] Actor code never resumes on the I/O callback stack.
- [x] Cancellation before, during, and after completion is deterministic.
- [x] Nested Asio operations preserve values and exceptions.

### Phase 5: Actor ABI V2

Status: `COMPLETE`

- [x] Add `IActorV2` returning `ActorTask<ActorResult>`.
- [x] Add explicit ABI-generation discovery and V2 export helpers.
- [x] Extend `ActorManager` to load V1 and V2 actors safely.
- [x] Implement and test `AsioActorV1Adapter`.
- [x] Update installed headers and actor CMake helpers.
- [x] Add V1, V2, invalid-version, and mixed-runtime fixture actors.
- [x] Publish an actor-author migration guide.

Exit criteria:

- [x] V1 and V2 actors can run in one process.
- [x] A V1 actor uses the native scheduler without executing actor state inline
      on an I/O thread.
- [x] Unsupported ABI versions fail during loading, not during dispatch.
- [x] Existing standalone actor builds remain green while V1 compatibility is
      enabled.

### Phase 6: Orchestrator And Runtime Integration

Status: `COMPLETE`

- [x] Construct one native scheduler in the runtime bundle.
- [x] Wire scheduler options from configuration.
- [x] Route awaited stages through `async_enqueue`.
- [x] Route terminal asynchronous stages through runtime-owned task tracking.
- [x] Preserve emitted-message recursion and failure-envelope behavior.
- [x] Define actor-worker, Asio, and blocking-pool thread budgets.
- [x] Add `asio-v1` and `native-v2` runtime selection.

Exit criteria:

- [x] Existing orchestrator behavior tests pass against both engines.
- [x] Terminal tasks survive owning facade destruction but not runtime shutdown.
- [x] Runtime shutdown leaves no actor, timer, or detached completion pending.
- [x] Switching engines requires configuration only.

### Phase 7: Actor Migration

Status: `COMPLETE`

- [x] Convert `message_store` to `IActorV2` without changing repository logic.
- [x] Convert the bridge actor boundary to `IActorV2`.
- [x] Keep bridge QQ/TG networking and forwarding internals as Asio coroutines.
- [x] Adapt bridge forwarding through `ActorContext::await_asio`.
- [x] Update actor repository smoke and integration tests.
- [x] Validate mixed V1/V2 pipelines during the compatibility window.

Exit criteria:

- [x] `obcx::core::events::RawMessageEvent -> obcx::message_store::events::MessageStored -> bridge::events::MessageForwarded` passes on V2.
- [x] Bridge I/O suspension releases the actor worker and preserves mailbox
      exclusivity.
- [x] Standalone message_store and bridge repositories build against the V2 SDK.
- [x] Mixed-version pipelines produce the same observable envelopes as V1.

### Phase 8: Stress, Performance, And Rollout

Status: `GATED — ASIO-V1 REMAINS DEFAULT`

- [x] Run sanitizer suites for core and standalone actors.
- [x] Run at least one million synthetic enqueue/resume/completion transitions
      without loss or duplication.
- [x] Benchmark balanced mailboxes, one hot mailbox, skewed worker placement,
      I/O suspension, completion storms, and shutdown under load.
- [x] Compare queue delay, throughput, tail latency, CPU usage, and memory usage
      against the Phase 0 baseline.
- [x] Verify successful steals improve forced-skew queue delay.
- [ ] Enable `native-v2` by default only after acceptance thresholds pass.
- [x] Keep `asio-v1` rollback support for one compatibility window.
- [x] Defer V1 removal to a separate change after external compatibility ends.

Exit criteria:

- [ ] Correctness and performance thresholds defined in Phase 0 pass.
- [x] No known task loss, duplicate resume, mailbox overlap, or shutdown hang
      remains.
- [x] Metrics can identify imbalance, long resumes, and suspended mailboxes.
- [x] Process-restart rollback has been exercised successfully.
- [ ] V2 is the default engine; balanced performance currently blocks this gate.

## Required Test Matrix

| Area | Required cases |
| --- | --- |
| ActorTask | value, void, exception, nested task, yield, cancellation, destruction |
| Worker pool | local pop, injection, forced steal, sleep/wake, shutdown |
| Mailbox | FIFO, one active task, suspended ownership, next-message handoff |
| Races | enqueue vs completion, completion vs cancellation, stop vs requeue |
| Asio bridge | immediate completion, delayed completion, exception, cancellation, late callback |
| ABI | V1, V2, mixed versions, missing symbols, unsupported generation |
| Orchestrator | awaited stage, terminal async stage, emitted routing, failure routing |
| Performance | balanced, skewed, hot mailbox, I/O-heavy, completion storm |

All concurrency stress tests must be repeatable with deterministic seeds. Tests
that assert stealing must force placement rather than depend on timing alone.

## Rollback Plan

- Keep V1 source and tests until the final compatibility phase.
- Select the runtime engine through configuration.
- Do not share V1 and V2 mailbox state implementations; route at the scheduler
  facade so rollback does not depend on partially migrated state.
- Keep message envelopes and actor results wire-compatible between engines.
- If V2 fails rollout gates, switch the default back to `asio-v1`, retain V2
  behind an opt-in flag, and preserve captured metrics and failing seeds.

## Risks And Mitigations

| Risk | Mitigation |
| --- | --- |
| Coroutine frame use-after-free | Runtime-owned operation state, generation checks, sanitizer stress |
| Duplicate I/O completion/resume | Atomic exactly-once completion transition |
| Lost worker wakeup | Runnable counter plus tested notification protocol |
| Actor state races after migration | One active task per mailbox and synchronized worker queues |
| Same actor object used by partitions | Preserve and document existing cross-partition thread-safety requirement |
| Blocking actor stalls a worker | Slow-resume metrics, cooperative yield, blocking middleware |
| Thread oversubscription | One configured runtime thread-budget policy |
| ABI break for standalone actors | V2 symbols, explicit generation, V1 adapter and compatibility window |
| Two coroutine systems complicate debugging | Task ids, transition metrics, boundary tracing, deterministic scheduler tests |
| Work stealing regresses light-load power use | Sleeping workers and optional sharing/V1 policy during evaluation |

## Final Completion Criteria

This roadmap is complete only when:

- the public V2 actor ABI no longer exposes `boost::asio::awaitable`;
- actor continuations are scheduled and stolen by the native actor worker pool;
- Asio is confined to I/O execution and explicit actor/Asio adapters;
- same-mailbox FIFO and exclusive execution hold across suspension and stealing;
- V1 actors have a tested compatibility and removal path;
- message_store and bridge run through V2 in their standalone repositories;
- sanitizer, stress, shutdown, and performance gates pass; and
- production can roll back to V1 for the agreed compatibility window.

## References

- CAF scheduler and work-stealing model:
  <https://actor-framework.readthedocs.io/en/0.18.4/Scheduler.html>
- CAF scheduler policy and throughput configuration:
  <https://actor-framework.readthedocs.io/en/latest/core/ConfiguringActorApplications.html>
- Boost.Asio asynchronous model:
  <https://www.boost.org/doc/libs/latest/doc/html/boost_asio/overview/model.html>
- Boost.Asio completion-token model:
  <https://www.boost.org/doc/libs/latest/doc/html/boost_asio/overview/model/completion_tokens.html>
- Boost.Asio `async_initiate`:
  <https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference/async_initiate.html>
