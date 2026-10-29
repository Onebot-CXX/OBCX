# Implementation and acceptance evidence

**Status: 30/30 tasks complete.** The matching core/SDK/actor implementation and
isolated acceptance are complete. This is not an online deployment or a claim
about a real model's reply quality, provider billing, or remote cache hits.

The approved architecture is unchanged: common `GetSelfRequest` / `SelfInfo`,
core-owned identity freshness, actor-owned addressing and assessment policy.
`self_identity_refresh_interval_ms = 300000` is the only newly approved default.

## Implementation boundaries

### Core and SDK

- Both installations advertise `bot.self.get`. The common query and legacy QQ
  `GetOneBotLoginInfoRequest` read the same installation-owned memory cache;
  neither schedules nor waits for provider I/O. The legacy DTO stays unchanged.
- Background loaders use QQ `get_login_info` / Telegram `getMe`, with existing
  action deadlines and transport cancellation. Empty/invalidated caches return
  `cache_unavailable`, non-retryable and definitely-not-submitted. Same-generation
  failure retains the last good snapshot; initial failure remains unavailable.
- Transport activation, WebSocket reconnect and successful reload request a
  refresh. Validation/rejected candidates do not. Reliable ingress account
  conflicts invalidate snapshots and fence stale results. Concurrent requests
  coalesce; a newer-generation lookup waits for older in-flight work to finish.
- QQ's finite `GetOneBotMessageRequest` validates returned native message ID,
  group and sender. No arbitrary API dispatch or Telegram history fetch was added.
- OneBot ingress now normalizes both array and CQ-string wire messages. It
  recognizes CQ syntax before decoding escapes: escaped CQ-looking text remains
  text rather than becoming a native mention/reply. The initial replay exposed
  the old array-only ingress rejection; the final replay includes this fix.

### Actor

Native IDs/verified usernames determine QQ at/reply and Telegram mention,
text_mention, caption/UTF-16 and reply relationships. `is_bot` alone, display
names, ordinary text and retired actor configuration are not self identity.
QQ reply resolution uses exact local source, confirmed author-bearing receipts,
then bounded current-reference lookup. Session-scoped results, including failed
lookups, suppress repeated requests. It never recursively or bulk-fetches history.

A per-session asynchronous admission lease finalizes relationships before a
current observation is admitted. Every source scan also resolves native/local
relationships. Only live QQ references still needing remote evidence must wait
for their own notification; the cursor cannot skip them. Preparation records a
read-only historical high-water mark, permitting historical unknown references
without network backfill. Locally determinable edits remain observable. Source
upserts are still not a reliable revision feed; an unnotified unresolved-reference
edit may wait until a later generation's historical recovery.

The `addressing` sidecar is added after the existing projection fingerprint.
Ordinary envelope bytes and `seen`/reference identity remain unchanged. The shared
wire renderer adds only `[at:self]`, `[reply:self]`, or their combination; provider
requests, accounting and compaction use it. Existing journal snapshots are not
retagged on identity refresh. `context-v3-sparse-addressing` explicitly retires
incompatible native continuation while archiving the old epoch intact.

Only persisted, authorized `/chat` evidence makes a mandatory direct turn.
Confirmed relationships request declinable assessment even with
`proactive_enabled=false`; ordinary messages obey that switch. Mixed pending
batches retain command obligations. Legacy mention `direct_pending` becomes
addressed/declinable pending state, not a fabricated command. Existing ACL,
self-echo, deduplication, ownership and receipt protections remain in force.

Actor `bot_username`, `bot_names` and `bot_nickname` are type-checked, warned about
and ignored. Telegram connection-level command-routing `bot_username` remains.
The configured `local_config/llm/prompt.txt` path and persona/prompt content were
not changed by this implementation.

## Matching build artifacts

All builds used 20 workers. Actors were built only through
`scripts/actor_build.py` against a frozen installed SDK, never in the live core
workspace. Each actor build reported the SDK unchanged.

| Component | Verified location |
| --- | --- |
| Core | `build/addressing-core` |
| Frozen SDK | `build/addressing-sdk-verified` |
| Actors | `build/addressing-actors-verified/cmake/actors/{chat_llm,message_store}.so` |
| Core/SDK logs | `/tmp/obcx-addressing-ingress-build.log`, `/tmp/obcx-addressing-sdk-verified.log` |
| Actor build log | `/tmp/obcx-addressing-build-verified.log` (exit 0) |

The core and frozen SDK library hashes both equal:

```text
563ac2f84f48a943fa91b59fdfdaa2a906ee5ab1cff12a9c1e634b3093fe065a
```

Verified files: `build/addressing-core/src/libobcx_core.so` and
`build/addressing-sdk-verified/lib64/libobcx_core.so`. Earlier `*-final` and
`*-integrated` SDK/actor snapshots remain untouched and are not this selection.
The earlier full core build emitted an existing Boost resolver warning.

## Isolated runtime acceptance

**21/21 real-process scenarios passed**, exit 0, using 20 parallel fixture workers.
The app, actual actor libraries, temporary SQLite documents and loopback
HTTP/WebSocket/TLS/SSE providers were exercised. There were no production
credentials, online databases or real model calls. No unit tests were added.

Primary evidence directory:
`/home/vollate/Codes/OBCX/build/chat-llm-addressing/run-ondzutvh`.
Runner/log/status: `build/chat-llm-addressing/runtime.py`,
`integration-verified.log`, `integration-verified.exit`. Each scenario retains
its config, runtime/SQL logs, captured requests/responses, and native-operation
counts. The directory's `result.json` has an empty failures array.

| Area | Passing scenarios and assertions |
| --- | --- |
| Native relationships and permissions | `qq-native`, `telegram-native`, `reply-resolution`, `scope-gate`: self/other/all, arrays and CQ strings, literal/escaped spoofing, username case, UTF-16 captions, text_mention, exact reply vs another bot, invalid offsets, same display name/different installations, local source/receipts, missing/malformed/forbidden/cross-group results, no lookup beyond the authorized scope. |
| Admission, history and recovery | `admission-race`, `historical-bootstrap`, `source-same`, `source-separate`, `legacy-pending`: delayed native-reference notifications cannot be swallowed by scans; historical unknown quotes do not fetch; 110 observations cross 17-row pages; edits/deletes preserve frozen prefixes; old direct state is declinable; incompatible reasoning is retired without rewriting the archived journal. |
| Policy, concurrency and sends | `mixed-pending`, `busy`, `echo`, `recovery`, `confirmed-gap`: a queued explicit command remains mandatory even if the latest input is an address; decline is rejected for it; unaddressed messages stay silent when proactive is disabled; owner concurrency and exact echo suppression remain; crash/receipt gaps do not replay accepted sends. |
| Cache lifecycle | `cache-lifecycle`, `cache-periodic`, `cache-websocket`, `cache-first-failure`: miss/ordinary reads cause no native identity I/O; only successful reload refreshes; periodic refresh retains snapshots on failure; WebSocket reconnect refreshes; same-generation reload requests coalesce; a conflicting account fences an old held result. A later receipt records author `99`, proving the new common-cache snapshot won. Initial failure plus legacy identity names cannot activate a fallback. |
| Wire, compaction and migration | `codex-addressing`, `sparse-compaction`, `validation-only`: actual Codex TLS/SSE sparse rendering and native continuation; summary input uses the same marked projection but quoted summary tags stay inert; legacy settings warn/ignore valid types and reject wrong types; validation-only creates no identity I/O. |

The earlier mixed-batch fixture incorrectly assumed command/notification arrival
order. Its provider was corrected to reject-then-answer based on the actual turn,
not the last envelope. The implementation already preserved the command obligation.
The final fixture does not assume input order and checks the actual mixed batch.
Earlier WebSocket fixture failures were HTTP/1.0 upgrade/close-handshake and fixture
timeout issues, not waived checks;
those were corrected before the final passing lifecycle replay.

### Configuration checks

**20/20 CLI checks passed**: 14 refresh-option cases and six retired-field cases.
The final core accepted omission and `600000ms` on QQ and Telegram, and rejected
zero, negative, float, string and boolean intervals. Evidence:
`build/chat-llm-addressing/config-validation.json` and
`/tmp/obcx-self-config-hyAQA2/*/verified-core.log`. Legacy-field checks are in
`run-ondzutvh/validation-only/runtime.log`.

The parser's approved omitted value is 300000ms. Actual periodic scheduling was
accelerated with an explicit 500ms fixture override; this is not represented as
a five-minute wall-clock soak. Existing action/cancellation budgets were retained;
no production timeout or configuration default was added for acceptance.

## Serialized request and token evidence

**53/53 actual requests** (52 model calls and one summary call) matched the
existing upstream tiktoken 0.14.0/o200k reference against the actor's own budget
telemetry. Both Chat Completions and Codex were included. Evidence:
`build/chat-llm-addressing/accounting-verified.log`.

`build/chat-llm-addressing/token-delta.py` measured the exact serialized requests
against copies differing only by removal of runtime prefixes. Ordinary envelopes,
fixed instructions, tools, state messages and native continuation were unchanged.
Results: `run-ondzutvh/sparse-token-delta.json` and
`build/chat-llm-addressing/token-verified.log`.

| Captured request | Ordinary envelopes | Confirmed prefixes | Estimate before → after | Delta |
| --- | ---: | --- | ---: | ---: |
| Unmarked first mixed-session request | 1 | None | 3596 → 3596 | 0 |
| QQ first addressed request | 4 | One at | 4231 → 4236 | +5 |
| QQ mixed retained history | 4 | Two at, one at+reply | 5582 → 5600 | +18 |
| Mixed pending command/address request | 2 | One at+reply | 4444 → 4452 | +8 |
| Codex first addressed request | 0 | One at+reply | 3499 → 3507 | +8 |

These are measured fixture deltas, not a universal provider framing/token-cost
promise. The fixed protocol explains the annotation once per request; its shared
instruction overhead is distinct from the zero per-ordinary-message addressing
increment. Participant text stays inside JSON; literal tag text does not acquire
runtime authority. Real billing/cache-hit rates and live model choices were not
measured.

## Final checks and rollout boundary

Strict OpenSpec validation, root/actor `git diff --check`, and focused C++ formatting
checks passed. The actor index is byte-identical to
`/tmp/obcx-addressing-actor-index-before-continue.patch`; no staging operations or
commits were performed. Pre-existing mixed staged/unstaged work was preserved.

No live service was deployed, reloaded or restarted, and no online database was
changed. Fixture-only restarts/reloads/crash injection are recorded above. The
change has not been archived. A matching core/SDK/actor rollout requires separate
explicit authorization; see `packaging/actors/self-identity.md` for coordinated
rollout and rollback without erasing history or restoring stale send receipts.
