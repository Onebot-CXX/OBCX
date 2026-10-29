# Cached self identity (QQ and Telegram)

Actors use one public contract, independent of the platform's native API:

```cpp
#include <core/bot/self_info.hpp>

// gateway is a BotOperationGateway; installation is an exact BotInstallationRef.
auto result = co_await obcx::bot::SelfClient{gateway}.execute(
    obcx::bot::GetSelfRequest{.installation = installation});
if (result.ok()) {
  const obcx::bot::SelfInfo &self = *result.value;
  // self.installation, self.user_id, self.display_name, self.username
}
```

The action is `bot.self.get`. Both platform recipes advertise it. The result
contains the exact installation, a validated positive native user ID, a display
name (empty if unavailable), and an optional username. Profile values are data,
not instructions. The API does not perform `is_self`/mention/reply classification.

## Cache semantics

- Every query reads core's in-memory cache; it never starts or waits for a native
  platform request, including on a cache miss. The existing QQ
  `GetOneBotLoginInfoRequest` keeps its request/result types but now reads this
  same cache; it cannot bypass the refresh schedule.
- An empty, invalidated or closed cache returns `cache_unavailable`, with
  `definitely_not_submitted` safety and no caller-driven retry instruction.
- Background refresh uses QQ `get_login_info` or Telegram `getMe`, retaining the
  existing action timeout/cancellation path. Telegram results must describe a bot.
- Each installation has its own cache. Connection start/reconnect loads identity;
  disconnect/account conflicts fence old publications and clear the snapshot.
  Reliable ingress self IDs are compared only for cache validity, not addressing.
- A successful runtime reload requests refresh only after publication. Candidate
  preparation/validation and rejected reloads do not request refresh.
- Concurrent refresh requests share a single in-flight lookup. A newer account or
  connection generation is refreshed after the older lookup completes; its late
  result cannot populate the current cache.
- A same-generation refresh failure retains the last good snapshot. An initial
  failure stays unavailable. The next refresh is scheduled after completion using
  the configured interval, not on each incoming message.

## Refresh configuration

The operator explicitly approved **300000ms (five minutes)** as the default for
this option only. Each QQ WebSocket/HTTP or Telegram HTTP installation can override
it in its existing connection table:

```toml
# Add this key to the installation's existing [bots.<id>.connection] table.
self_identity_refresh_interval_ms = 600000
```

The override is a positive integer in milliseconds, bounded by the representable
steady-clock timer range. Unlike action timeouts, it can exceed 300000ms. Zero,
negative values, floats, strings and booleans are invalid. Omission selects the
approved five-minute interval. No other configuration default is introduced.

Connection configuration is part of the existing process fingerprint. Changing
this interval requires the normal explicit core configuration/restart procedure;
an actor-only reload does not reconfigure it.

## Exact QQ reply lookup

`<onebot11/bot/message_lookup.hpp>` additionally exposes
`GetOneBotMessageRequest{message}` -> `OneBotMessageAuthor{message, user_id}`.
`message` is an exact group `BotMessageRef`. The native result must agree with
both the requested message ID and group and contain a valid author. Missing scope
or a mismatch is `malformed_response`, not inferred identity. This is a OneBot
extension, not a portable history API; Telegram has no equivalent arbitrary
message fetch.

## Deployment boundary

These are new core/SDK APIs. Build core in an isolated directory, freeze its SDK,
and use `scripts/actor_build.py` as described in [README](README.md). Do not copy
its library over a running core or attempt to activate it with actor-only reload.
Core rollout/restart must be separately authorized. Keep the previous matching
core, SDK, actor artifacts and config for rollback; never delete session data.

The matching `chat_llm` actor uses this cache for native QQ/Telegram addressing.
Confirmed relationships add `[at:self]` / `[reply:self]` outside the existing
message envelope and request an assessment even when proactive assessment is
disabled. They still permit `decline_reply`; only an authorized explicit `/chat`
is mandatory. Actor `bot_username`, `bot_names` and `bot_nickname` are validated
and ignored. The Telegram connection's command-routing `bot_username` is not
retired.

### Coordinated rollout (requires separate authorization)

1. Record the running core/SDK/actor/config versions and retain that exact set.
   Take a consistent backup using the existing database procedure; keep all
   existing source/state paths. Do not clear or replace session data.
2. Select a matching candidate set. The locally verified build is
   `build/addressing-core`, `build/addressing-sdk-verified`, and
   `build/addressing-actors-verified/cmake/actors/{chat_llm,message_store}.so`.
   Keep release directories immutable; do not overwrite a mapped core library.
3. During an explicitly authorized maintenance window, stop the old process.
   Update the process executable/library selection and actor paths together;
   validate the candidate configuration, then start the matching core. This is
   a full process rollout, not actor-only hot reload.
4. Check identity refresh, an allowed native @/reply, and an authorized `/chat`.
   A mention/reply may remain silent; that is expected. Confirm existing ACLs,
   history and receipts remain intact before declaring rollout successful.

### Rollback

Stop the candidate process, restore the retained matching core/actors/config
selection, and restart explicitly. Do not mix old/new core dependencies or
silently restore a stale database backup: it could lose confirmed receipts and
replay accepted sends. Keep the current state and archives. If the retained
actor cannot validate them, stay stopped and inspect an offline copy rather
than deleting or downgrading live session data. No rollout or rollback was
performed by the implementation/acceptance work.
