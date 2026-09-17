## Why

OBCX core had three reusable runtime gaps: its hand-written HTTP transport did not provide the required TLS/proxy and decoded-body guarantees, generation-owned background work had no activation/invalidation bridge into reload drain, and `DbManager` could serialize writes but could not expose an atomic transaction operation. These concerns belong to `obcx_core` rather than to any individual actor package.

## What Changes

- Replace the internal OBCX HTTP/proxy transport with libcurl's multi/socket API driven by Asio while preserving the supported awaitable and synchronous client boundaries.
- Require origin and HTTPS-proxy certificate/hostname verification, explicit proxy routing, asynchronous DNS capability, cancellation-safe transfer retirement, conservative POST submission classification, and finite wire/header/decoded response collection.
- Add a generation lifecycle service that starts background work only after active publication, invalidates it before reload drain or shutdown, accounts admitted work in generation drain, and destroys generation-owned callbacks before actor-library unload.
- Add a process-owned restart-constraint registry so candidates can compare actor-declared immutable values without publishing candidate state.
- Add a provider-owned `DbManager` transaction operation that executes on the serialized writer, commits atomically, rolls back on failure, propagates results/errors, and remains available through the installed SDK.
- Update core build dependencies, installed SDK packaging, and offline core tests.

Actor package rewrites, actor-specific configuration, commands, persistence schemas, polling policy, parsing, and delivery behavior are outside this OpenSpec change.

## Capabilities

### New Capabilities

- `http-curl-asio-transport`: Shared libcurl multi/Asio backend, executor integration, proxy/TLS behavior, cancellation, and compatibility at the OBCX HTTP boundary.
- `database-manager-transactions`: Writer-serialized atomic transactions through the process `DbManager` and provider connection interface.

### Modified Capabilities

- `actor-runtime-reload`: Generation activation, invalidation, drain ownership, rollback reactivation, callback retirement, and process-owned restart constraints for actor-owned background work.
- `http-response-body-limits`: Finite decoded-body enforcement and failure propagation for compressed responses while preserving existing limit selection.

## Impact

- Core runtime: `ActorGenerationLifecycle`, `ActorRestartConstraintRegistry`, runtime generation construction, reload cutover, shutdown, and installed headers.
- Core database infrastructure: `DbManager`, `IDbConnection`, the SQLite provider, and database tests.
- Core network infrastructure: `HttpClient`, `ProxyHttpClient`, the private curl/Asio adapter, dependency declarations, SDK package configuration, and local TLS/proxy fixtures.
- Compatibility: existing supported HTTP constructors, awaitable/synchronous methods, response values, and request-submission safety remain the public boundary; curl handles remain implementation-private.
- Deployment: libcurl 8+ with asynchronous DNS and required TLS/proxy support becomes a build/runtime dependency. No live endpoint or credential is required for acceptance tests.
