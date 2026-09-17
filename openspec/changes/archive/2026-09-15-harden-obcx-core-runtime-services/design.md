## Context

OBCX's prior HTTP implementation combined Beast, custom proxy tunnelling, and post-read decompression. It lacked complete origin/proxy TLS verification and could exceed the selected body limit during decompression. Actor-runtime reload also had no generic lifecycle signal for actor-owned background work, so prepared candidates could not safely distinguish preparation from active publication or join reload drain. Finally, `DbManager` exposed reads, serialized writes, and migration locking, but no provider-owned atomic runtime transaction.

These are reusable `obcx_core` responsibilities. This design intentionally excludes actor-specific behavior.

## Goals / Non-Goals

**Goals:**
- Drive libcurl multi transfers from Asio readiness and timers without blocking actor workers or creating one thread per request.
- Preserve the supported OBCX HTTP API, executor affinity, explicit proxy behavior, finite response bounds, and conservative submission-state semantics.
- Provide minimal generation activation/invalidation and work-admission primitives without moving scheduling policy into core.
- Provide process-lifetime candidate compatibility storage for actor-declared restart constraints.
- Provide atomic, writer-serialized database transactions through `DbManager` with provider-owned transaction semantics.
- Export and verify the new core services through the installed SDK.

**Non-Goals:**
- Actor-specific timers, scheduling rules, commands, schemas, migrations, authorization, parsing, or delivery policy.
- A generic scheduler or scheduled-input DSL.
- Exposing curl handles or callbacks in public HTTP APIs.
- Exactly-once network delivery or treating missing acknowledgements as definite non-submission.
- Adding configuration defaults.

## Decisions

### 1. Private libcurl multi driver integrated with Asio

Use `curl_multi_socket_action` with libcurl socket/timer callbacks mapped onto Asio descriptor waits and timers. Serialize each driver's state through an Asio strand. Request state owns URLs, headers, upload data, response buffers, easy handles, cancellation state, and completion work until one terminal completion is posted to the associated executor.

Cancellation is serialized with readiness handling, removes the easy handle before releasing resources, and completes once. Stale descriptor callbacks are tied to owned socket state and cannot operate on a later transfer that reuses the descriptor.

Require libcurl 8+ with asynchronous DNS, TLS, HTTP/HTTPS, HTTP/HTTPS proxy, and SOCKS5-hostname support. The backend does not call `curl_easy_perform` and does not create a worker thread per request.

### 2. Explicit TLS, proxy, protocol, and response policy

Verify certificates and hostnames independently for origins and HTTPS proxies, retaining both TLS layers for HTTPS-over-HTTPS-proxy. Reject TLS failure without insecure fallback. Disable ambient proxies for direct clients; explicit proxy configuration overrides ambient proxy/no-proxy values. Resolve SOCKS5 target names through the proxy.

Restrict protocols to HTTP/HTTPS, preserve no-automatic-redirect behavior, and do not enable a shared cookie jar. Apply finite URL, header, wire-body, and decoded-body limits while collecting data. Abort before appending decoded bytes beyond the selected bound. Keep diagnostics bounded and free of URLs, credentials, raw response bodies, and curl error buffers.

Retain existing awaitable and synchronous wrappers. Pre-connect failures may be definitely-not-submitted; timeout, cancellation, send/receive ambiguity, or malformed post-response processing remains possibly-submitted.

### 3. Minimal generation lifecycle service

Register one `ActorGenerationLifecycle` service per runtime generation during construction. Actors may subscribe nonthrowing start/stop closures during preparation, but start closures run only after active-generation publication. Activation creates a fresh immutable validity token. Invalidation marks the token invalid before invoking stop closures.

`acquire_work(token)` admits only the current valid token and returns a lease backed by the generation's existing route-drain accounting. Callers retain the lease until every completion callback for that operation retires. Idle waits need no lease. Reload invalidates the old generation before drain; a drain timeout reactivates it with a fresh token. Shutdown retires the lifecycle and destroys DSO-owned closures before actor libraries unload.

Core supplies no timer registry or scheduling policy.

### 4. Process-owned restart constraints

Register one `ActorRestartConstraintRegistry` on the process-owned generation builder and share it with every generation. Exact `(actor, key)` values do not collide across actor/key boundaries. Candidate preparation can check compatibility without changing published values; active-generation code can publish the value it activated. The registry stores no actor-specific policy or credentials.

### 5. Provider-owned database transactions

Extend `IDbConnection` with a transaction task and `DbManager` with a typed `run_transaction` wrapper. The SQLite provider routes the task through its dedicated writer queue, begins an immediate transaction, commits after successful callback completion, and attempts rollback before propagating any callback or commit failure. Existing migration locking reuses the same transaction primitive.

Transaction callbacks must remain short and synchronous and must not suspend on network or actor work. The provider owns transaction syntax and serialization; callers receive only `IDbConnection` and typed result/error propagation.

### 6. Packaging and verification

Keep curl implementation files private while exporting only the existing HTTP interfaces and new core lifecycle/database declarations. Installed package configuration resolves libcurl as an implementation dependency needed by linked executables. Use local HTTP/TLS/proxy fixtures and temporary SQLite files; no acceptance test uses live services or credentials.

## Risks / Trade-offs

- [libcurl and Asio callback ownership differ] → Strand serialization, owned state, exactly-once completion, and cancellation/descriptor-reuse tests.
- [TLS verification changes deployments using private certificates] → Require trusted roots and provide no insecure fallback.
- [libcurl feature sets vary] → Validate required features in the supported build/package matrix.
- [Background work can outlive invalidation] → Invalidate first, account admitted operations in generation drain, and retain DSO closures until retirement.
- [Adding a pure virtual transaction operation changes provider source compatibility] → Rebuild providers against the matching SDK and require each provider to implement atomic semantics explicitly.
- [Long transactions block the shared writer] → Require short synchronous callbacks and prohibit suspension inside transactions.

## Migration Plan

1. Add and validate libcurl dependencies and the isolated multi/Asio adapter against local TLS, proxy, compression, timeout, cancellation, and concurrency fixtures.
2. Switch core HTTP clients to the adapter, remove superseded transport/decompression code from builds, and verify API/submission compatibility.
3. Register lifecycle and restart-constraint services in runtime generations; integrate activation, invalidation, rollback reactivation, drain, shutdown, and DSO retirement.
4. Add provider-owned database transactions, refactor migration locking to reuse them, and verify commit/rollback and installed-SDK use.
5. Rebuild core and all SDK consumers against the matching interface, run offline regression suites, format, and validate the change strictly.

## Open Questions

None.
