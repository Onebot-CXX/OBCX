## 1. Core HTTP transport

- [x] 1.1 Add libcurl development/build/package dependencies and verify asynchronous DNS, TLS, and HTTP/HTTPS/SOCKS5 proxy capabilities in the supported toolchain.
- [x] 1.2 Implement a private libcurl multi/socket-to-Asio adapter with owned request state, strand-serialized operations, executor-correct completion, cancellation, timeouts, and finite wire/header/decoded response collection.
- [x] 1.3 Add local origin/proxy TLS fixtures covering direct HTTPS, HTTP and HTTPS CONNECT, SOCKS5 proxy-side DNS, peer/hostname rejection, compressed expansion, cancellation races, descriptor reuse, and concurrent transfers.
- [x] 1.4 Switch `HttpClient` and `ProxyHttpClient` internals to the adapter while preserving supported awaitable/synchronous APIs, explicit proxy behavior, and conservative request-submission state.
- [x] 1.5 Remove superseded transport/decompression paths from builds and verify existing HTTP, bot submission-safety, reload, and installed-SDK regressions.

## 2. Core generation lifecycle

- [x] 2.1 Add and export `ActorGenerationLifecycle` with preparation-time subscriptions, active-generation tokens, invalidation-before-cancellation, and work leases backed by generation drain.
- [x] 2.2 Integrate lifecycle activation, invalidation, rollback reactivation, shutdown retirement, and DSO-safe callback destruction into runtime generation/reload ownership without adding scheduling policy.
- [x] 2.3 Add and export a process-owned `ActorRestartConstraintRegistry` shared across generations for exact actor/key candidate compatibility checks.

## 3. Core database transactions

- [x] 3.1 Add typed `DbManager::run_transaction` and provider `run_transaction_task` support with writer serialization, commit, rollback, result propagation, and migration-lock reuse.
- [x] 3.2 Verify SQLite commit/rollback behavior, writer usability after failure, provider integration, and clean installed-SDK compilation.

## 4. Core acceptance

- [x] 4.1 Complete the root build and offline core network, lifecycle, database, packaging, and compatibility tests without live endpoints or credentials.
- [x] 4.2 Run `nix fmt`, strict OpenSpec validation, and staged/unstaged root and nested diff checks while preserving unrelated work.
