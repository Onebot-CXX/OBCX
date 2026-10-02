## 1. HTTP ownership and cleanup

- [x] 1.1 Bind direct/proxy HTTP clients to their constructor executor, retain request state, close admission, and preserve isolated synchronous wrappers.
- [x] 1.2 Retain queued curl shutdown ownership, clean multi resources on the strand, and make cancellation/close idempotent.

## 2. Installation lifecycle

- [x] 2.1 Close polling HTTP clients during disconnect and prevent post-stop retry/update work.
- [x] 2.2 Drain installation cancellation before component/context destruction instead of forcing the context to stop, including suppressing queued OneBot WebSocket connects/reconnects after stop.

## 3. Regression verification

- [x] 3.1 Add cross-executor retirement, queued shutdown ownership, pending-transfer close, polling, and installation drain regression coverage.
- [x] 3.2 Run focused and sanitizer tests with at least six workers, build the application, document lifecycle contracts, and validate the OpenSpec change.

## 4. Actor caller migration moved

Original checked tasks 4.1–4.2 and their verification record moved unchanged to the Bridge archive with the same original date; see [migration.md](migration.md).

## Verification results

- Built application and focused test targets with 20 workers.
- 98 regular tests passed: HTTP/curl, installation lifecycle/assembly, dispatcher/generation, WebSocket, and action tracking.
- 61 ASan/UBSan tests passed with leak detection and halt-on-error enabled; nine shutdown/race regressions also passed ten repetitions each.
- Sanitizer CTest used an isolated inventory of rebuilt targets because unrelated stale binaries in `build-asan` fail test discovery against the newer core ABI.
- Public `HttpClient` single-pointer object layout is preserved; request ownership lives behind its pimpl.

### Caller follow-up evidence

The relocated Bridge `tasks.md` retains the complete original failed-before-migration, 108/61 regular, 68 sanitizer, repetition, module-build and constructor-audit results verbatim. These are historical results, not tests rerun by the documentation migration.

