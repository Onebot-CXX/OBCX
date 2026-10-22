## 1. Explicit notification execution and admission

- [x] 1.1 Change core success-handler registration to require a valid explicit notification executor together with the handler, preserving one-time/pre-seal validation and dispatchers with no observer.
- [x] 1.2 Dispatch successful-operation observation asynchronously on the supplied executor with owned operation/callable lifetime; remove caller-executor inheritance and preserve the original provider result on observation failure.
- [x] 1.3 Add idempotent notification-admission closure synchronized with the actual post-completion enqueue, preventing late submissions after closure without executing handlers or joining executors under dispatcher locks.

## 2. Application ownership and shutdown

- [x] 2.1 Create the application-owned single-worker notification executor only for normal runtime, inject it into success-handler registration, and keep it unchanged across actor reloads without adding configuration options or changing the actor-visible gateway.
- [x] 2.2 Enforce close-notification-admission -> release shutdown gate waits -> retire runtime with notification execution available -> drain notifications -> stop Bot dependencies on normal shutdown and early/exceptional cleanup; retain owners and prevent self-join.

## 3. Isolated build and controlled verification

- [x] 3.1 Build the affected core in a separate build directory with at least six parallel workers, using all cores when load permits; verify the explicit registration call sites compile without rebuilding actors in the live workspace. Do not add or modify unit tests.
- [x] 3.2 In a nonproduction smoke session, arrange a successful send completion while reload ingress is closed; verify old-pool retirement, new-generation notification admission, message processing, and successful acceptance of a subsequent reload. Also verify that a notification admitted before closure retains normal old-generation drain behavior and a drain-timeout waiter resumes on the old generation.
- [x] 3.3 Exercise process shutdown with a gate-waiting notification and a provider completion racing notification-admission closure; verify accepted callbacks terminate before dependency teardown, late observations are not enqueued, and provider results remain unchanged. Check repeated cleanup and an early exit after observer registration without adding unit tests.
- [x] 3.4 Record the isolated build and smoke evidence, notification failure/disabled-observer checks, and unchanged Bot restart-required boundary in an implementation note; explicitly report any unverified scenario and document that deployment requires a separately authorized core process restart.
