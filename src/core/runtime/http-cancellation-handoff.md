# Shared HTTP cancellation-slot completion ownership

Generic shared-network correction in `src/network/curl_asio_multi.cpp` only. No header/API, client, cache, event, registry, SDK, platform, actor, configuration or CMake changes. Source/compile evidence is not executed cancellation/shutdown or installed-SDK acceptance.

## Ownership and serialization contract

Initiation obtains the awaitable handler's associated executor and cancellation slot, creates the existing caller work guard, assigns the cancellation handler, and only then dispatches transfer start onto the curl driver strand. The installed cancellation handler still strongly owns its specific Transfer and weakly references the driver; dispatched cancellation retains both while it runs. Cancellation remains targeted, not shared-client shutdown.

The slot handle is copied by value into the completion closure and then its posted caller-executor callback. It is not captured by reference, is no longer stored in Transfer, and is never accessed by driver completion. The posted callback clears it immediately BEFORE invoking the completion handler. There is no slot clear/access afterward: invoking that handler can resume/finish the coroutine and destroy its signal. A copied Asio slot handle is non-owning; it does not extend the signal's lifetime or clear anything in its destructor.

The caller must serialize initiation/slot assignment, cancellation emission, completion cleanup and signal destruction. Using the SAME strand for the coroutine and its cancellation emitter satisfies this requirement. Merely using the same io_context with multiple runners does not. The signal and caller/driver executors must remain alive and runnable until admitted work drains. The driver does not introduce a global lock or an arbitrary replacement executor.

Completion continues to use `post`, not inline invocation/dispatch. Even if caller and driver share a serialization domain, cleanup cannot reentrantly destroy the currently emitting cancellation handler.

## Driver terminal ordering

`complete_transfer` remains driver-strand-owned:

1. Reject already-complete transfers, then mark this transfer complete.
2. Remove an added easy handle from curl multi and erase its active map entry.
3. Release this transfer's curl resources on the driver, preserving the existing header-list-before-easy cleanup order and nulling both released pointers.
4. Move its completion and owned response, then post caller completion.
5. On the caller-associated executor, clear the slot and invoke the handler with the unchanged error/response.

The first serialized terminal event still wins. Late cancellation cannot overwrite a completed success/physical effect, and cancellation winning before completion still returns operation_aborted. No send-receipt/account-origin policy is added here.

## Additional same-seam lifetime edges (explicitly approved)

Moving clear alone would leave the slot's strong Transfer owner alive until the caller callback. Clearing it there could destroy the last Transfer and move curl easy/header cleanup onto that caller. `release_curl_resources()` therefore performs idempotent cleanup on the driver BEFORE posting; the destructor retains its fallback for abandoned/teardown paths. Normal later destruction on any owner sees null curl pointers. Request/response ownership is retained; no networking work is added to the caller callback.

`start_transfer` now checks `complete` before configuration or shutdown handling. If cancellation has already terminally completed a not-yet-started transfer, the queued start cannot create/re-add a curl handle or complete it twice. The existing cancel and complete guards remain.

These two edges, not just the moved clear, must be included in independent review and propagation.

## Path trace (source proof, not executed tests)

- **Normal completion:** collect status/error/owned response as before; driver marks complete, removes/releases curl state, posts once; caller clears then resumes. The handler owns the suspended awaitable until resumption.
- **Early/configuration failure:** assignment precedes start; the existing error mapping reaches the same terminal path. Any partially created curl handles/header list are released on the driver; caller clears before error delivery.
- **Cancellation-before-start:** cancellation dispatch owns the target; if it wins, it completes operation_aborted once. The queued start retains that transfer but exits on complete without curl configuration.
- **Active cancellation:** only that transfer is removed/released. Other transfers and the shared client's connection pool/lifecycle are untouched.
- **Shutdown:** the existing requested flag/driver shutdown path, callback disabling, socket/timer cleanup and active-transfer iteration remain. Each active transfer uses the same terminal/post path; queued starts see shutdown or complete. Caller cleanup does not operate curl resources.
- **Queued cancellation after transfer completion:** until caller cleanup, emission can still dispatch a cancellation closure. It owns its target safely, but on the driver finds complete and returns. After caller cleanup, emission finds no HTTP slot handler. No second completion or result override occurs.
- **Handler destruction:** clear destroys the installed cancellation handler on the serialized caller before coroutine resumption. Any final Transfer release then has no live curl resources. The posted completion owns the work guard, awaitable handler and moved response by value; no queued closure borrows a dead Transfer/caller stack reference. Ordinary functor/slot-handle destruction after handler invocation does not clear the possibly destroyed signal.
- **Abandonment:** the existing header contract still requires draining runnable contexts. Destructor cleanup is retained as fallback; it does not promise handler invocation when queued work is discarded. Signal/context destruction must not race emission. Existing allocation/enqueue-failure behavior is not replaced by a new shutdown/retry/blocking policy.

## Telegram consumption, read-only trace

The stopped Telegram candidate constructs poll_executor_ using make_strand. poll_updates is co_spawned there with the poll's bound cancellation slot. HttpClient::post → HttpClientState::perform → CurlAsioMulti::perform are awaited directly, preserving the awaitable caller executor/slot through initiation. cancel_poll posts signal emission and retry-timer cancellation onto that same polling strand. The existing active-poll completion barrier waits for the old coroutine before launching a pending replacement.

This correction makes HTTP slot cleanup run on that polling serialization domain rather than the different curl strand. It does not change poll retirement, overlap replacement polls, close the shared HTTP client or modify cursors/credentials. Parent must import the reviewed shared-network delta into stopped consumers and independently re-review combined Telegram consumption.

## Preserved behavior and validation gate

Request/body/header bounds, TLS/proxy/credential handling, redirect rules, timeout/error mapping, response ownership, retries and generic shutdown policy are unchanged. All other source is read-only. Validation uses the six existing network production TUs, not synthetic cancellation programs or tests. Full source ordering/lifetime review, real runtime cancellation/shutdown behavior, integration and installed-SDK acceptance remain separate gates.
