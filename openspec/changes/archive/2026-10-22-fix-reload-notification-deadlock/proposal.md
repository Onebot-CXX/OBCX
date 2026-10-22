## Why

Actor reload can deadlock after route drain because a successful Telegram-send notification runs on the retiring actor I/O executor and waits for the closed reload ingress gate. Reload waits for that executor to join before reopening the gate, leaving a published candidate unable to process messages and subsequent reload requests permanently busy.

## What Changes

- Dispatch successful-operation notifications on an explicitly supplied, process-owned executor rather than the operation caller's executor; preserve asynchronous, best-effort observation without changing the completed operation result.
- Keep that executor running across actor generations. Notifications already admitted to a generation retain normal route-drain behavior; notifications waiting outside ingress do not retain the retiring actor executor.
- Add a notification-admission shutdown boundary: stop new notification submissions, release shutdown gate waiters, complete runtime retirement while notifications can run, and drain notifications before stopping their process-owned dependencies.
- Preserve actor-only reload, existing ingress selection and drain timeout behavior, and restart-required rejection of Bot configuration changes. Do not introduce Bot hot enable/disable, Bot leases, configuration options/defaults, or changes to actor library unloading policy.
- Validate with a separate core build and controlled runtime smoke checks. Do not add or modify unit tests without an explicit user request.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `bot-operation-dispatch`: Require process-owned execution and explicit shutdown admission for asynchronous successful-operation observations.
- `actor-runtime-reload`: Require successful-operation notifications waiting at ingress to remain independent of retiring generation executors while retaining existing admission-boundary semantics.

## Impact

- Core-only integration: `include/core/bot/bot_operation_dispatcher.hpp`, `src/core/bot/bot_operation_dispatcher.cpp`, and application composition/shutdown in `src/app/main.cpp`, with a narrowly scoped process executor owner if needed.
- The core-side success-handler registration API will require an explicit executor. The actor-visible data-only operation gateway and actor ABI remain unchanged.
- Existing process-owned Bot installations and endpoints continue across reload. No Bot connection, database schema, package dependency, actor package, or configuration migration is required.
- Deploying the fix requires a rebuilt core and an explicit process restart; actor hot reload cannot patch the currently running core. Build verification must not replace the live process's core library.
