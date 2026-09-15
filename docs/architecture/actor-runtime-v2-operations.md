# Actor Runtime Operations

Status: current actor-only operations guide (2026-07-30)

## Runtime configuration

The native scheduler is always constructed. Its supported configuration is:

```toml
[actor_runtime.scheduler]
policy = "stealing"       # or "sharing"
workers = 0               # 0 = resolve from the process thread budget
blocking_workers = 0      # 0 = share the remaining process thread budget
slow_resume_warning_ms = 10

[actor_runtime.reload]
drain_timeout_ms = 5000 # valid range: 100..300000
```

`stealing` favors worker-local queues and permits opposite-end stealing.
`sharing` publishes runnable work through the shared injector. The process
resolves one budget across actor workers, Asio I/O sources, and blocking work;
operators should not size each pool independently from
`hardware_concurrency()`.

## Process blocking executor

Startup creates one fixed-size `BlockingExecutor` from the resolved
`blocking_workers` budget and registers the same shared service in the active
generation and every reload candidate. Validation-only mode validates the
budget but creates no blocking worker threads. A reload that changes the
resolved worker count reports `reload_restart_required`.

Actors use `ActorContext::run_blocking`; nested Asio code may use
`BlockingExecutor::run` only inside a generation-tracked coroutine. Both paths
publish completion through the caller's executor without future/timer polling.
The operation counters are:

- `submitted`: accepted callables;
- `running` and `pending`: current blocking-worker and queue occupancy;
- `completed` and `failed`: terminal callable outcomes;
- `rejected`: submissions refused after admission closes or posting fails.

The executor exposes these counters through `BlockingExecutor::metrics()`.
Startup and shutdown logs publish the same payload-free fields so operators can
distinguish saturation, callable failures, and shutdown rejection.

## Generation preparation

A reflected actor may implement synchronous
`prepare_generation(ActorContext&) -> ActorPreparationResult`. The V2 export
helper exposes this through an additive optional ABI symbol. The runtime calls
it after validated configuration and generation services are available and
before scheduler/orchestrator registration, so actor-owned schema preparation
can finish before any message, notice, command, retry worker, or media buffer
uses that state. `Ready` continues construction, `Failed` rejects the
generation, and `RestartRequired` produces `reload_restart_required` without
publishing the candidate.

Actors built before this additive symbol and reflected actors without the hook
remain `Ready`; their construction and dispatch behavior is unchanged. A hook
must inspect `ActorGenerationInfo::purpose`: validation-only preparation may
validate actor-specific configuration but must not mutate state, while a reload
candidate must return restart-required before any startup-only migration.

## Reload operation

Enter `reload` in either the TUI command box or the `--no-tui` standard-input
command loop. The command returns immediately after the single-flight reload
slot is acquired and tells the operator to wait for the terminal result. A
second request reports `reload_busy`; candidate parsing, staging, validation,
drain, and publication continue on the reload worker. The terminal result is a
highlighted `ACTOR RELOAD SUCCEEDED`, `ACTOR RELOAD FAILED`, or
`ACTOR RELOAD CANCELLED` banner. Routine phase timings and bridge mapping-cache
initialization remain debug-level details so the active generation and final
outcome are visually authoritative at the default log level.

Candidate preparation leaves the active generation and bot connections
untouched. During cutover, new root messages wait at the ingress gate while
routes already admitted to the old generation finish, including suspended
Asio work, recursive emissions, and terminal async stages. A successful drain
publishes the candidate once and releases waiting messages onto it. The last
route reference keeps the old actors, services, scheduler, DSO handles, and
staging tree alive until old code can no longer resume.

`actor_runtime.reload.drain_timeout_ms` defaults to 5000 ms and accepts values
from 100 through 300000 ms. `reload_drain_timeout` reopens ingress on the old
generation and destroys only the candidate; it does not cancel the live old
routes. Increase the deadline only when actor work has a known, bounded reason
to take longer, and diagnose non-cooperative actors before retrying.

Actor entries, actor-owned tables, pipelines, and routing policy are
reloadable. Bot definitions (enabled or disabled), database instance
definitions, and resolved actor/I/O/blocking thread budgets are process-owned;
changing any of them reports `reload_restart_required`. Deploy those changes
with a process restart. Logs include only changed domain names and stable
failure codes, never tokens, connection values, message payloads, or actor
configuration values.

The transaction has one observable order: `prepare -> gate -> drain ->
publish -> reopen -> retire`. Parse, staging, contract checks, actor
construction, typed actor generation preparation, and activation all occur
during `prepare`, before ingress is closed. Publication is a single pointer
exchange after the old generation has drained; no fallible actor work occurs
between publication and reopening the gate.

Common operator-facing failure codes are:

| Code | Meaning and action |
| --- | --- |
| `reload_busy` | Another attempt owns the single-flight slot; wait for its terminal log. |
| `reload_parse_failed` / `reload_config_invalid` | The candidate file is unreadable or invalid; correct it and retry. The active snapshot is unchanged. |
| `reload_actor_unavailable` / `reload_contract_invalid` | An actor artifact or ABI 2 contract is invalid; restore a matching artifact set. |
| `reload_dependency_identity_conflict` | A private shared-library closure could not be isolated; rebuild/package the complete actor closure. |
| `reload_activation_failed` | Candidate actor construction or scheduler registration failed; inspect candidate logs. |
| `reload_actor_initialization_failed` | An actor's pre-ingress generation preparation failed; correct its actor-specific configuration/state before retrying. |
| `reload_bot_unavailable` | A configured identity is absent from the live process-owned registry; restore the startup identity or restart. |
| `reload_restart_required` | A process-owned setting changed or an actor reported that startup-only state preparation is required; restart the process. |
| `reload_drain_timeout` | Old work missed the deadline; ingress has reopened on the old generation. Diagnose the actor before retrying. |
| `reload_shutdown` | Process shutdown won the race; do not retry in that process. |

Every terminal result includes the attempt id and authoritative generation.
Success banners identify the previous and active generations plus total
latency; debug diagnostics retain preparation and drain timings. Failure
banners include a stable code, use error level for actual failures, and state
`FULL PROCESS RESTART REQUIRED` when hot reload cannot safely replace a
process-owned dependency. Treat a success banner—not command acceptance—as
proof that the candidate is active.

## Operational invariants

- Exactly one task owns an actor/partition mailbox at a time.
- A suspended task retains mailbox ownership until completion or cancellation.
- Backpressure rejects excess work without creating an unbounded queue.
- Asio completion republishes work to the actor scheduler and never executes
  actor code on an I/O callback stack.
- Shutdown closes actor ingress and blocking admission, joins already-admitted
  blocking work, retires actor instances while retaining their DSO leases,
  drains generation I/O callbacks and completion bridges, and only then
  releases those leases and unloads the DSOs.

Slow-resume warnings identify cooperative actor code that holds a worker too
long. Blocking calls should use `ActorContext::run_blocking`; waiting for
network or timer operations should use `ActorContext::await_asio`.

## Verification

Run the standard checks with:

```bash
ctest --test-dir build --output-on-failure
ctest --test-dir build -L actor-runtime --output-on-failure
ctest --test-dir build -R '^actor_sdk_v2_smoke$' --output-on-failure
```

The clean external SDK and installed-surface check is `actor_sdk_v2_smoke`.
Root verification uses only root-owned source and generic actor fixtures;
standalone actor repositories own and run their separate release and
integration suites.

Run the isolated release install and continuous pipeline soak from an empty
build/install root with:

```bash
nix develop --ignore-environment --command \
  python3 scripts/verify_actor_release.py \
    --work-dir /tmp/obcx-actor-release-verification \
    --soak-messages 100000 --jobs 2
```

The command removes loader-path overrides, performs a Release configure and
build, installs core and all actor packages into one prefix, starts the
installed application, and runs the installed message-store-to-bridge
pipeline. `scripts/rehearse_actor_release_rollback.py` rehearses an atomic
deployment-link switch between immutable candidate and previous install roots.

After all source gates are green, prepare deterministic coordinated artifacts
without publishing them:

```bash
python3 scripts/package_actor_release.py \
  --deployment /tmp/obcx-actor-release-verification/build/actor-package-conformance/sdk \
  --output-dir /tmp/obcx-actor-release-artifacts
sha256sum --check /tmp/obcx-actor-release-artifacts/SHA256SUMS
```

The generated manifest remains `prepared-not-published`; repository rename,
tagging, and upload are separate external release actions.

## Failure handling

Actor business failures are returned as `ActorResult::failed(code, message,
retryable)`. Infrastructure exceptions become failed invocations and must not
escape a worker thread. Pipeline configuration determines whether downstream
stages run after an emitted result.

For shutdown incidents, capture scheduler metrics and logs before restarting.
Operational recovery from a bad release is deployment of the preceding OBCX
release and its matching configuration/artifacts. The current binary contains
no alternate execution path.

For a rejected candidate, fix or restore the configuration/artifacts and issue
`reload` again; the old generation is already authoritative. After a
successful but behaviorally bad actor deployment, restore the previous actor
artifact set and its matching actor-owned configuration, then issue another
`reload`. Use a full process restart when any process-owned domain changed or
when artifact provenance cannot be established. Never overwrite a library in
place while packaging it: deploy immutable versioned roots so both generations
can retain their complete closures until old work releases its final reference.
