# Implementation and verification

## Changes

- `include/core/bot/bot_operation_dispatcher.hpp` and `src/core/bot/bot_operation_dispatcher.cpp`: explicit executor/handler registration, owning asynchronous observation, and idempotent notification admission closure. The actual enqueue after provider completion shares the closure lock; there is no pre-I/O observer snapshot.
- `src/app/main.cpp`: one application-owned notification worker, retained across reloads. An idempotent shutdown closure and scope-exit guard close admission, release gate waits, retire runtime, drain notifications, and only then close endpoints and stop Bots. Validation-only startup returns before constructing this worker.
- Actor gateway, reload controller, route admission/drain, DSO retirement policy, and Bot configuration fingerprints are unchanged. No new configuration options, defaults, or unit tests were added.

## Isolated build

Verified on 2026-10-22 with GCC 16.1.0, Boost 1.89.0, and CMake 4.1.2. The machine exposed 20 CPUs; compilation used all 20. The workspace is a copy of the core-only example, renamed because the package resolver requires the basename `packages.toml`.

From `/home/vollate/Codes/OBCX`:

```sh
mkdir -p build/reload-notification-fix/workspace
cp packages-example.toml build/reload-notification-fix/workspace/packages.toml
cmake -S . -B build/reload-notification-fix -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DOBCX_PACKAGES_WORKSPACE="$PWD/build/reload-notification-fix/workspace/packages.toml" \
  -DOBCX_PACKAGES_CACHE="$PWD/build/reload-notification-fix/package-sources" \
  -DOBCX_PACKAGES_STATE_DIR="$PWD/build/reload-notification-fix/package-state" \
  -DOBCX_PACKAGES_MODE=development \
  -DOBCX_BUILD_BENCHMARKS=OFF \
  -DOBCX_ENABLE_IO_URING=ON
cmake --build build/reload-notification-fix --target obcx --parallel 20
```

Result: exit 0, all 130 build steps completed. No actor targets were built. One compiler warning originated in Boost.Asio resolver code through glibc's fortified `memcpy`; no compilation errors occurred. `configure.log`, `build.log`, and `build.exit` are under this isolated build directory.

Built artifact SHA-256:

```text
72f2a5a7afd050b768ca1c92ed6308e66c3654be5f3556e2894aaca57c18f93f  src/libobcx_core.so
5ce9328dcf6a0b280e1542fea8550cff6acbcf282370785faf4fb159c885c1a7  src/app/obcx
```

## Controlled application smoke checks

These checks exercised the actual rebuilt application with existing `bridge.so` and `message_store.so`; neither actor was rebuilt. Each session used a unique directory, a new SQLite database, synthetic identities, a loopback HTTP OneBot provider, and a loopback HTTPS Telegram provider with a local certificate. No production Bot credentials or real chat endpoints were used.

Six independent initial scenarios ran concurrently. Targeted debugger-controlled checks followed separately. Temporary integration drivers and raw evidence are local, ignored build artifacts, not unit tests or additions to the repository test suite:

- `build/reload-notification-fix/runtime_smoke.py`
- `build/reload-notification-fix/smoke_debug.py`
- Each session directory contains `config.toml`, `application.log`, `provider-requests.json`, and its isolated database; debugger sessions also contain `debugger.log`.

Evidence directory names below are relative to `build/reload-notification-fix/`.

| Scenario | Verified result | Evidence directory |
| --- | --- | --- |
| Successful send during closed ingress | First provider success persisted while a second old route held drain open. Telegram notification activity remained absent until cutover; then old pools retired, notification activity appeared, generation 2 -> 3 reload succeeded, and a further message was delivered. | `smoke-overlap-1792652543195863149` |
| Drain timeout | First success notification waited outside ingress while the second provider call remained held. After the 2500 ms drain timeout, generation 1 remained active and notification activity appeared **before** releasing the second call. A subsequent generation 1 -> 2 reload succeeded. | `smoke-rollback-1792652543203506547` |
| Notification admitted before closure | A non-stop debugger paused only the notification worker after admission to generation 1. The original send route had already persisted its mapping. Reload stayed in drain until that worker resumed; notification completion then allowed old-pool retirement and cutover. | `smoke-preclosure-1792652830590053544` |
| Shutdown waiter and late successful completion | With one notification waiting at the closed gate, SIGTERM closed notification admission and woke the waiter with `reload_shutdown`. The debugger held the application thread before runtime retirement while other workers continued. A second successful provider response reached the bridge's successful-delivery mapping branch after closure; no second notification ran. Resuming teardown exited 0. | `smoke-shutdown-1792652939585544090` |
| Observation failure | A trigger in the disposable database rejected Telegram heartbeat updates. The send still persisted its returned message mapping, one observation failure was logged, and no resend occurred. | `smoke-observer-failure-1792652543196140404` |
| Provider failure | A controlled Telegram rejection produced `bridge_delivery_failed`, with no success notification activity and no resend. | `smoke-provider-failure-1792653057575254145` |
| Early startup exit after observer registration | A message-store runtime with disabled Bots reached `No bot components started successfully`; scope-exit cleanup retired runtime and completed, returning the intended exit 1. | `smoke-early-exit-1792652543196186166` |
| Observer absent | With no actor runtime/controller, Bots started and normal shutdown exited 0 without a notification worker. | `smoke-no-observer-1792652543196368727` |

Additional evidence:

- Both overlap and rollback sessions attempted changing Telegram `enabled` before another reload. Both received `reload_restart_required` / `FULL PROCESS RESTART REQUIRED`; Bot hot enable/disable remains unsupported.
- Normal CLI shutdown invokes the explicit closure and subsequently the scope-exit guard. Each successful session logged exactly one shutdown start and completion, confirming repeated cleanup is harmless.
- In the controlled late-success shutdown check, mapping persistence was correctly rejected because blocking-work admission had already closed. This is distinct from provider failure: the bridge reached mapping persistence only after a successful provider result. Exactly one message-sent observation failure was logged, belonging to the earlier accepted gate waiter; there was no late observation.
- `--validate-config` against the isolated message-store configuration exited 0 and printed `Configuration and actor contracts are valid`; it did not reach process notification/Bot startup. Evidence: `validate-config.log`.
- `clang-format --dry-run --Werror` for the three changed C++ files, `git diff --check`, and `openspec validate fix-reload-notification-deadlock --strict` passed.

### Verification limits

- Allocation/post-launch failure and a directly throwing success callable were not fault-injected. Their exception containment was reviewed; the runtime failure smoke instead exercised a failing downstream observation route.
- Actual C++ exception unwinding after registration and the TUI frontend were not separately induced. The startup early-return path and repeated normal cleanup exercised the same scope-exit/idempotent owner; exception ordering was reviewed in source.
- No exhaustive concurrency stress, sanitizer run, or existing unit-test suite execution is claimed. Debugger control established specific admission/shutdown interleavings, not every possible schedule.
- Preliminary fixture attempts were not counted as passes: requests initially timed out too early; an early CLI status string was unsuitable as a completion marker; a SQLite write lock also blocked candidate preparation. Passing evidence uses longer explicit fixture timeouts, terminal reload markers, and thread-specific debugger suspension. An initial uncontrolled shutdown cancelled the late provider call, so it was replaced by the controlled successful-completion case above.

## Deployment boundary

**Nothing was deployed or restarted.** The live process observed as PID 1305330 remained running. The live workspace binaries retained their pre-build SHA-256 values:

```text
c6606f2816f24a85afafcfbd9351d38c9e3b942cfd7273f9c90454c46cc10867  build/src/libobcx_core.so
1e8f1c53322dd77f77955db4dff1c6dc2abe3da772a762afeca205b0a332b1e8  build/src/app/obcx
```

Deployment requires a **separately authorized rebuilt-core process restart**; actor hot reload cannot apply this core fix. No TOML, database, or actor-package migration is required. Rollback requires restoring the previous core and restarting, which also restores the original deadlock risk. Unrelated pre-existing working-tree changes were preserved; no commits were created.
