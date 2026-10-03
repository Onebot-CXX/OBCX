# Implementation notes

## Repository boundary

- Root baseline: `825630b39c6cf134bb544454c539fddbf52757ee`.
- Existing staged and unstaged root work was preserved, including the separate changes in `src/network/proxy_tunnel.cpp` and unrelated platform/runtime files.
- This cleaned OpenSpec records only `obcx_core` implementation. Actor-package implementation and behavior are intentionally excluded.

## Core HTTP transport

OBCX now uses a private libcurl multi/socket driver integrated with Asio strands, descriptor readiness, and timer callbacks. Request state owns curl strings, headers, bodies, response storage, cancellation state, and completion work. Completion returns through the associated executor, and cancellation removes the transfer and completes exactly once.

The backend explicitly disables ambient proxies for direct operation, forces configured proxies, uses SOCKS5 proxy-side DNS, verifies origin and HTTPS-proxy certificates and hostnames, preserves both TLS layers for HTTPS-over-HTTPS-proxy, restricts protocols, does not follow redirects, and enables no shared cookie engine. Finite header, wire-body, and decoded-body limits are enforced during collection. Diagnostics remain bounded and do not include credentials, URLs, raw bodies, or curl error buffers.

`HttpClient` and `ProxyHttpClient` retain their supported awaitable and synchronous APIs and conservative POST submission-state semantics. The old manual decompression implementation was removed, and superseded proxy transport code is no longer built. CMake, Nix, vcpkg, and installed package metadata resolve libcurl 8+.

Offline verification included the complete 14-test local TLS/proxy/cancellation matrix, direct/proxy and actor-executor HTTP tests, synchronous compatibility, timeout and response-limit tests, submission-safety tests, and installed-SDK isolation tests.

## Core generation lifecycle

`ActorGenerationLifecycle` is registered as a generation service. Preparation-time subscribers receive a fresh token only after active publication. Invalidation marks the token invalid before stop callbacks run. Valid-token work leases participate in the existing route drain, and retirement destroys DSO-owned closures before actor-library unload.

Reload invalidates background work before draining. If drain times out, the retained generation reactivates with a fresh token; successful cutover activates only the published candidate. Validation-only generations do not activate background work. Runtime reload tests cover candidate inactivity, cancellation/drain, rollback reactivation, repeated reload, and safe retirement.

`ActorRestartConstraintRegistry` is process-owned by the generation builder, shared with candidates and active generations, and exported through the installed SDK. Exact actor/key encoding prevents ambiguous key collisions while leaving policy with SDK consumers.

## Core database transactions

`IDbConnection::run_transaction_task` and typed `DbManager::run_transaction` provide provider-owned atomic transactions. The SQLite provider executes transactions on its dedicated writer queue, uses `BEGIN IMMEDIATE`, commits successful callbacks, attempts rollback on failure, and propagates callback/commit errors. Migration locking now reuses the same transaction primitive.

Database-manager tests verify successful result propagation, rollback after exceptions, preservation of committed rows, and continued writer usability. The final database selection passed **8/8**, and a clean installed-SDK consumer compiled and passed.

## Final core verification

- Complete root build: passed.
- Root tests excluding the separately invoked cross-repository conformance test: **476/476 passed**.
- Cross-repository conformance: **1/1 passed**.
- Core database-manager tests after final formatting: **8/8 passed**.
- Clean installed-SDK transaction consumer: **1/1 passed**.
- `nix fmt`: passed.
- Strict OpenSpec validation: passed before this artifact cleanup and rerun afterward.
- Root and nested staged/unstaged diff checks: passed.

No live endpoint or credential was used, and no commit was created.
