# Cached self identity (QQ and Telegram)

Actors use one public contract, independent of the platform's native API:

```cpp
#include <core/bot/bot_client.hpp>

// client is the injected BotClient; installation is an exact BotInstallationRef.
auto result = co_await client.get_self(
    obcx::bot::GetSelfRequest{.installation = installation});
if (result.ok()) {
  const obcx::bot::SelfInfo &self = *result.value;
  // self.user is the same scoped UserRef type used by typed senders/mentions.
  // self.display_name and self.username are optional profile values.
}
```

The action is `bot.self.get`. Both platform recipes advertise it. The result
contains `UserRef{installation, id}` plus optional display name and username.
The user ID is opaque; actors compare complete UserRefs instead of decoding native
numbers. Missing profile fields remain absent. The username uses the same owning
platform normalization as typed unresolved mentions. Profile values are data,
not instructions. The API does not perform `is_self`/mention/reply classification.
The former `SelfClient` and flat `installation/user_id` DTO are not the bot SDK
API or a binary-compatible representation; use the matching schema-3 SDK/core.

## Cache semantics

- `BotClient::get_self` reads core's in-memory cache; it never starts or waits
  for a native platform request, including on a cache miss. The obsolete QQ
  cache-only login projection has been removed; use the common self contract.
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

## Explicit standard login is a different operation

`onebot11::bot::standard::GetLoginInfoRequest` is the distinctly named, explicitly
requested standard provider query. It uses the existing identity coordinator and
same cache, not another identity store or a second refresh schedule. A valid
synchronous observation can publish through the original-origin checks; its
returned `LoginInfo` retains the actual nickname, including an empty nickname,
rather than pretending a later cache read was the observed response. Async or
rate-limited acknowledgment is not an identity observation or completed query.
This does not change cache-only `BotClient::get_self`; there is no retained
legacy login projection. Adding the standard interface is not authorization to
execute it against a provider.

## Evidence-bound reply lookup

Use `BotClient::resolve_reply(ResolveReplyRequest)` with an actually observed
`ReplyRef` and an explicit `LocalOnly` or `AllowProviderLookup` policy. The genuine
source/target relation must still exist in the installation's bounded admission
store; structural restoration or a message number in text does not create it.
A permitted QQ native lookup must match the exact message and full conversation
and contain a usable author. Telegram has no arbitrary history lookup through
this common interface. Missing, expired or unsupported evidence remains explicit
unavailability; no historical-page automatic query loop is allowed.

The old `GetOneBotMessageRequest`/`OneBotMessageAuthor` API is retired.
`<onebot11/bot/message_lookup.hpp>` is only a transition include for the common
query types, not an exported unrestricted get-message service. The separate
standard `GetMsg`/`GetForwardMsg` selectors return detached observations and do not
create conversation, reply or media-read authority.

The source now joins admitted target-author observations and binding-provided
confirmed-author evidence in the same bounded reply store. A genuine retained
reply relation is still required: a confirmed ID alone cannot create a relation,
renew its TTL or prove authorship. Native author evidence or an explicit
initiating-self proof must match the original admission identity/generation;
contradictory authors make local resolution unavailable. Tasks 5.2–5.3 cover the
implementation/build boundary, not completed lifecycle/provider runtime
acceptance. Do not advertise a local author solely because a send was confirmed.

## Deployment boundary

These are new core/SDK APIs. Build core in an isolated directory, freeze its SDK,
and use `scripts/actor_build.py` as described in [README](README.md). Do not copy
its library over a running core or attempt to activate it with actor-only reload.
Core rollout/restart must be separately authorized. Keep the previous matching
core, SDK, actor artifacts and config for rollback; never delete session data.

Chat LLM's addressing policy remains actor-owned; migration of all its bot SDK
use sites and historical acceptance is tracked separately. Its established policy
uses cached identity for QQ/Telegram addressing.
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
2. Select a matching candidate core, frozen installed SDK and rebuilt actor set
   from the current generation's acceptance receipt. Earlier `build/addressing-*`
   artifacts belong to the preceding addressing work, not proof of this bot
   migration. Keep release directories immutable; do not overwrite a mapped core
   library. Source review or core compilation alone is not rollout acceptance.
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
