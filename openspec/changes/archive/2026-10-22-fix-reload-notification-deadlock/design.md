## Context

`BotOperationDispatcher::invoke_owned()` currently spawns the success handler on `this_coro::executor`. When the caller is an actor, that executor can belong to its `RuntimeGeneration`. The application success handler converts successful Telegram sends into `BotMessageSentEvent` root ingress through `ActorRuntimeReloadController::process()`.

The observed failure is a lifetime dependency cycle, not a blocked actor destructor:

```text
reload closes ingress and drains route leases
  -> publishes the candidate
  -> previous->shutdown() joins the old actor I/O pool
  -> a success notification on that pool is waiting at the closed gate
  -> the gate will reopen only after the join returns
```

A gate waiter has not acquired route admission, so route drain can correctly reach zero while this coroutine still contributes Asio outstanding work. The captured incident showed a reload worker in `RuntimeGeneration::shutdown()` / `pthread_join` and an old-pool timer at an infinite deadline whose suspended caller was the application's successful-operation handler.

Bots, their operation endpoints, and the shared dispatcher are process-owned. Bot enabled state, identity, transport, and connection configuration are fingerprinted; changing them already fails preparation with `reload_restart_required`. This change preserves that boundary.

## Goals / Non-Goals

**Goals:**
- Remove the dependency of process success notifications on an operation caller's generation executor.
- Preserve the existing root-admission boundary, old-generation route drain, and asynchronous best-effort success observation.
- Give notification submission and executor retirement an explicit, race-safe process shutdown order.
- Verify the fix without modifying the live core workspace or adding unit tests.

**Non-Goals:**
- Bot hot enable/disable, per-generation Bot instances, or Bot resource leases.
- Early ingress reopening, force-stopping actor I/O, skipping joins, or changing DSO retention.
- A general event bus, durable notification queue, delivery retry policy, or exactly-once external delivery guarantee.
- New configuration options/defaults, actor ABI changes, or a general redesign of runtime shutdown and thread budgeting.

## Decisions

### 1. The application owns a dedicated notification executor

Create one process-lifetime asynchronous execution lane in normal application startup before registering the success handler. Use an explicitly constructed single-worker Asio owner: one worker is an internal control-plane implementation choice, not a configurable worker count or configuration default. Keep it alive across all actor reloads and retire it only during process teardown. Validation-only execution does not create the notification worker.

The owner belongs to the application, not `RuntimeGeneration` or an installation. The dispatcher receives an executor handle, not ownership of a thread that its own callback could end up joining. The application retains the dispatcher/controller and executor owner until callback drain completes.

Alternatives rejected:
- The caller executor reproduces the deadlock.
- The reload executor can itself be blocked joining an old generation.
- The blocking executor has different work and shutdown semantics.
- Borrowing a particular Bot installation executor introduces unnecessary coupling to that installation's lifecycle.

### 2. Register the success handler together with its executor

Change the core-side registration shape to `set_success_handler(notification_executor, handler)` with both arguments mandatory whenever a handler is installed. Keep the dispatcher constructor usable without a success observer, including the validation-only empty gateway. Retain existing validation that registration occurs once and before sealing.

Only a successful endpoint result schedules an observation. Dispatch observation asynchronously onto the supplied executor and return the original operation result without awaiting observation. Scheduling or handler failures cannot convert a completed send into failure or trigger a resend. Existing Telegram-send filtering and payload-free failure reporting remain in the application handler.

The queued task and coroutine must own the `SuccessfulBotOperation` value and keep the handler callable alive until completion, rather than referring to a local `std::function` copy or transient coroutine-lambda closure. Do not capture the originating actor, generation, route lease, or caller executor merely to deliver this process event. The actor-visible operation gateway remains unchanged.

### 3. Preserve ingress semantics instead of changing cutover

A success notification is a new root event, not a routed descendant of the original send. It continues to call the existing controller ingress:

| Notification position | Behavior |
| --- | --- |
| Admitted before gate closure | Uses the selected old generation and participates in its normal route drain. |
| Waiting at a closed gate | Holds no old-generation route lease or executor work; waits on the process notification executor. |
| Gate reopened after success | Enters the new generation exactly once at admission. |
| Gate reopened after drain timeout | Enters the retained old generation. |
| Runtime shutdown | Wakes, observes terminal shutdown state, and ends without entering another generation. |

Do not attach an old-generation lease to a notification that is waiting for gate reopening: that would move the same dependency cycle into route-drain accounting. Do not drop notifications simply because reload is active. Reload never stops or joins the notification executor, and the existing retirement-before-reopen ordering stays intact.

### 4. Close notification admission before draining its executor

Add an idempotent process-side operation to close success-notification admission independently of endpoint closure. The notification acceptance/enqueue decision after provider completion must be synchronized with this close operation. A handler snapshot taken before the provider call is not sufficient: it could submit after shutdown has already drained the executor.

Use a synchronized enqueue handoff, such as posting an owning launch task while holding the same state lock used to close admission. The post must not invoke the user handler inline under that lock. Work accepted before closure remains owned and runnable; completions reaching the boundary after closure skip observation but still return their original provider result. Handler execution and executor joins occur outside dispatcher locks.

Normal shutdown and early/exceptional exits after observer registration must establish this order:

1. Close notification admission without stopping Bot endpoints needed by existing work.
2. Call controller `begin_shutdown()` while the notification executor is still running, so gate waiters can observe shutdown.
3. Complete existing blocking-work and actor-runtime retirement with notification execution still available for completions.
4. Drain/join the notification executor from the application owner thread, with no remaining enqueue race; retain callback dependencies through this drain.
5. Close operation endpoints and stop/join Bot installations under their existing shutdown policy.

An application-scoped cleanup owner must enforce the ordering on exceptional exits as well as the normal CLI/TUI shutdown callback. A default thread-pool destructor running before controller gate cancellation is not an acceptable fallback. Do not use `stop()` to silently discard outstanding notification coroutine frames as a substitute for drain.

### 5. Preserve actor-only reload and use controlled runtime verification

Bot configuration fingerprints and restart-required rejection are unchanged. No new Bot lifetime protocol is needed for this fix because reload cannot stop or replace an installation. Thread-pool relocation is not presented as support for dynamic Bot lifecycle.

Implementation verification uses an isolated core build and an operator-controlled nonproduction runtime, not a new unit-test suite. Reproduce a successful send completing while reload has closed ingress, then verify old-pool retirement, gate reopening, notification completion, and acceptance of a subsequent reload. Also check a notification admitted before closure, a drain-timeout rollback, and process shutdown with a gate-waiting notification and a late provider completion. Record evidence in an implementation note when the implementation is performed; this proposal does not claim those checks have run.

## Risks / Trade-offs

- **An additional process worker** -> Keep it a single asynchronous control-plane lane, outside generation ownership; do not route blocking work onto it or change existing actor worker allocations.
- **Late enqueue races with executor join** -> Linearize notification admission closure with the actual enqueue, not an earlier handler lookup, and retain all already-accepted work.
- **Callable or dependency destruction before callback completion** -> Own callback values through their coroutine lifetime and retain the application controller, dispatcher, and Bot services until notification drain.
- **A new shutdown cycle** -> Cancel gate waits before notification join, leave the lane running throughout runtime retirement, and never join it from its worker or from reload.
- **Long or failing downstream routes** -> Preserve current route deadlines, cancellation, and best-effort semantics; this fix removes the observed cycle but does not claim to solve every source of an indefinitely suspended actor.

## Migration Plan

1. Implement and build the core in a separate build directory with at least six compilation workers, using all available cores when system load is low. Do not rebuild actor targets in the live core workspace.
2. Run the controlled smoke checks on a nonproduction instance and record terminal reload/notification outcomes. No unit-test additions or modifications are authorized by this change.
3. Deploy the rebuilt core only through an explicitly authorized process restart. No TOML, database, or actor-package migration is required. Rollback uses the previous core binary and a process restart; it also restores the known deadlock risk.

## Open Questions

None blocking this scoped proposal. Bot dynamic enable/disable remains a separate, unapproved change.
