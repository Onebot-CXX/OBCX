# Discord capability audit

## 1. Lane metadata

- **Platform:** Discord
- **Scope:** End-user/server product plus the official Developer Platform: applications and bot users, HTTP API v10, Gateway, interactions/application commands, incoming and event webhooks, OAuth2, guilds/channels/threads/forums, messages/components/polls/reactions/media, membership/roles/moderation/audit, voice/Stage/presence, limits/intents/verification.
- **Research date / access date:** 2026-08-17 UTC.
- **Freshness caveat:** Discord documentation is continuously published rather than released as one fixed specification. HTTP API v10 is current; Gateway and Voice have separately versioned protocols. Limits, review thresholds, privileged scopes, Components V2, Social SDK, and voice encryption are especially changeable. Exact commercial tier entitlements are intentionally not treated as stable contracts.
- **Assessment vocabulary:** `NATIVE`, `API_LIMITED`, `EMULATED`, `EXTENSION`, `UNSUPPORTED`, `UNKNOWN` as defined by the lane brief. “API” below means a supported official Discord surface, not automation of a normal user client.

## 2. Executive findings

1. Discord has a broad native server model—guilds, typed channels, categories, public/private threads, forum/media posts, roles and permission overwrites—and most administrative primitives are exposed by the HTTP API, subject to permissions and hierarchy [S5][S8][S17].
2. The supported automation identity is the application’s dedicated **bot user**. Bot tokens authenticate most HTTP routes and the Gateway; OAuth2 bearer tokens expose only consented scopes. Automating an ordinary user account (“self-bot”) is explicitly forbidden and may terminate the account [S1][S12][S21].
3. Gateway is a stateful WebSocket event stream; HTTP is request/response; interactions arrive through either `INTERACTION_CREATE` or an HTTP endpoint (mutually exclusive), but interaction responses are HTTP/webhook operations [S2][S10].
4. Messages have unusually rich official support: create/get/history/edit/delete, replies, forwards, announcement crossposts, embeds, attachments, stickers, reactions, polls, and interactive components. Message-content fields are redacted without the privileged `MESSAGE_CONTENT` intent except documented exceptions [S6][S2].
5. Application commands (chat-input/slash, user, message, and entry-point), autocomplete, buttons/selects, and modals are first-class. Initial interaction acknowledgement has a short three-second deadline; follow-up tokens last 15 minutes and can produce ephemeral messages [S9][S10].
6. Incoming webhooks are outbound-to-Discord message endpoints, while Event Webhooks are a distinct inbound event transport with only a narrow event catalog. Neither substitutes for Gateway guild/message subscriptions [S11][S13].
7. Friends, blocked users, and pending relationships are not a general bot capability. `relationships.read` is restricted to approved Social SDK access; DM-channel listing is partner-only. Linked third-party connections are not the Discord social graph [S12][S7].
8. Bot voice audio is supported through a separate Voice Gateway plus RTP/UDP media stack; Stage instances and voice state are modeled. Video transport appears in the protocol, but high-level bot camera/Go Live parity is not clearly guaranteed and should remain `UNKNOWN`/namespaced [S14][S15].
9. Presence and typing are supported but constrained: member and presence subscriptions require privileged intents, bot presence updates are rate-limited, and Discord exposes no general sender-visible message read-receipt API [S2][S3].
10. Moderation is extensive—roles, overwrites, timeout, kick/ban, AutoMod rules/actions, and audit-log retrieval—but all is permission/hierarchy limited. Audit entries are retained 45 days [S8][S16][S18].
11. HTTP clients must dynamically obey bucket/global rate-limit headers. The documented baseline bot global limit is 50 requests/s and the invalid-request threshold is 10,000 per 10 minutes; interaction endpoints are outside the bot global bucket [S4].
12. Gateway scale is gated by intents, verification/review, session-start limits, and sharding. Current docs simultaneously mention app verification at 100+ guilds and a newer privileged-intent review threshold of 10,000 visible users; these are related but distinct controls and should not be collapsed [S2][S22].
13. Discord’s product contains user-only experiences—friends, account switching, client notification/read state, video/screenshare, custom statuses, and user profile controls—that must not be inferred as bot API capabilities [S12][S21].

## 3. Research action log

- Read the lane brief in full, then used four official-domain searches covering (a) Gateway/OAuth/interactions/limits, (b) resources and moderation, (c) end-user/server features, and (d) self-bot policy.
- Follow-up searches covered webhooks, voice/Stage, AutoMod, message and upload limits, friends/DM scopes, bot presence, verification thresholds, and interaction acknowledgement timing.
- Primary entry points checked: Developer Platform overview; Gateway and Gateway Events; API Reference and Rate Limits; OAuth2; User, Guild, Channel, Message, Audit Log, Webhook, Auto Moderation, and Stage resources; application commands; interactions; Components; Voice; permissions; product Help Center pages.
- Full official developer pages were fetched and searched for endpoint/section text. Help Center pages returned HTTP 403 to direct fetch in this environment; their official search-index results were retained and marked as access-limited [S19–S21]. No secondary source was needed.
- Conflict checked: historical 100-guild app/bot verification versus the newer 10,000-visible-user privileged-intent review. Reported separately rather than choosing one [S2][S22].
- Reproducible query examples: `site:docs.discord.com/developers Discord Gateway intents verification`; `site:docs.discord.com/developers resources message polls components`; `site:docs.discord.com/developers relationships.read dm_channels.read`; `site:discord.com self-bot unsupported`.

## 4. Source register

| ID | Authority | Title / relevant section | URL | Evidence (accessed 2026-08-17) |
|---|---|---|---|---|
| S1 | OFFICIAL | Overview of Discord Apps — app/bot identity | https://docs.discord.com/developers/quick-start/overview-of-apps | Apps can have a bot user dedicated to automation. |
| S2 | OFFICIAL | Gateway — connecting, intents, privileged intents, sharding, Get Gateway Bot | https://docs.discord.com/developers/events/gateway | Stateful WebSocket, intent mapping, privileged `GUILD_MEMBERS`, `GUILD_PRESENCES`, `MESSAGE_CONTENT`, close codes, review and session-start metadata. |
| S3 | OFFICIAL | Gateway Events — send/receive events, presence and typing | https://docs.discord.com/developers/events/gateway-events | Complete event catalog; Update Presence/Voice State; bot activity fields and presence update constraints. |
| S4 | OFFICIAL | Rate Limits — buckets, global and invalid-request limits | https://docs.discord.com/developers/topics/rate-limits | Read headers; 429/`Retry-After`; 50 requests/s baseline; 10,000 invalid requests/10 minutes. |
| S5 | OFFICIAL | Channels Resource — channel types, threads, forums/media, typing, group DM recipients | https://docs.discord.com/developers/resources/channel | Typed channel model and CRUD/thread/forum operations, permissions and group-DM OAuth restriction. |
| S6 | OFFICIAL | Message Resource — message object and endpoints | https://docs.discord.com/developers/resources/message | History/create/edit/delete/reply/forward/crosspost/reactions/polls; 2,000-char content; 25 MiB request; nonce dedupe; `MESSAGE_CONTENT` redaction. |
| S7 | OFFICIAL | User Resource — current user, connections, guilds, DMs | https://docs.discord.com/developers/resources/user | User lookup, scoped current-user data, linked connections, bot DM creation, legacy group-DM caveats. |
| S8 | OFFICIAL | Guild Resource — members, roles, bans, channels, moderation | https://docs.discord.com/developers/resources/guild | Guild/member/role CRUD, joining, timeout/kick/ban, pruning and pagination. |
| S9 | OFFICIAL | Application Commands — command types, registration, permissions | https://docs.discord.com/developers/interactions/application-commands | Chat-input/user/message/entry-point commands, autocomplete, global/guild registration and command-create limit. |
| S10 | OFFICIAL | Receiving and Responding to Interactions — delivery, callback, follow-ups | https://docs.discord.com/developers/interactions/receiving-and-responding | Gateway-or-HTTP exclusivity, response types, modals, ephemeral messages, 3-second acknowledgement, 15-minute token. |
| S11 | OFFICIAL | Webhook Resource — management and execution | https://docs.discord.com/developers/resources/webhook | Token-authenticated incoming webhook execution; message/edit/delete; `wait` result semantics. |
| S12 | OFFICIAL | OAuth2 — grants, scopes, bot users and account differences | https://docs.discord.com/developers/topics/oauth2 | Consent/scopes; restricted partner/Social SDK scopes; bots cannot friend or join group DMs; bot flow. |
| S13 | OFFICIAL | Webhook Events — subscriptions, signing, delivery and event types | https://docs.discord.com/developers/events/webhook-events | Ed25519 verification, 3-second 204, retries to 10 minutes; narrow event list. |
| S14 | OFFICIAL | Voice — Voice Gateway, transport and DAVE | https://docs.discord.com/developers/topics/voice-connections | Voice connection handshake, UDP/RTP/Opus, protocol video fields, E2EE/DAVE lifecycle. |
| S15 | OFFICIAL | Stage Instance Resource — Stage CRUD | https://docs.discord.com/developers/resources/stage-instance | Create/get/modify/delete Stage instances with permissions. |
| S16 | OFFICIAL | Auto Moderation — rule triggers/actions/endpoints | https://docs.discord.com/developers/resources/auto-moderation | Rule CRUD, block/log/timeout/block-interaction actions and permissions. |
| S17 | OFFICIAL | Permissions — calculation, overwrites and hierarchy | https://docs.discord.com/developers/topics/permissions | Guild/channel permission bitsets, owner/admin, overwrites and hierarchy rules. |
| S18 | OFFICIAL | Audit Log Resource — retrieval, entries, reasons | https://docs.discord.com/developers/resources/audit-log | `VIEW_AUDIT_LOG`, 45-day retention, action taxonomy, `X-Audit-Log-Reason`. |
| S19 | OFFICIAL | Forum Channels FAQ | https://support.discord.com/hc/en-us/articles/6208479917079-Forum-Channels-FAQ | Product forums organize topic posts and require Community; direct fetch was 403. |
| S20 | OFFICIAL | Polls FAQ | https://support.discord.com/hc/en-us/articles/22163184112407-Polls-FAQ | Product polls support up to 10 answers and permission control; direct fetch was 403. |
| S21 | OFFICIAL | Automated User Accounts (Self-Bots) | https://support.discord.com/hc/en-us/articles/115002192352-Automated-User-Accounts-Self-Bots | Normal-user automation is forbidden and may cause termination; direct fetch was 403. |
| S22 | OFFICIAL | Updated Requirements to How Apps Access Data in Servers | https://discord.com/blog/updated-requirements-to-how-apps-access-data-in-servers | New 10,000-visible-user privileged-intent review and annual re-review framing. |
| S23 | OFFICIAL | API Reference — versioning, snowflakes, uploads, errors | https://docs.discord.com/developers/reference | API v10, serializable error model, snowflake cursors, multipart uploads and per-file limits. |
| S24 | OFFICIAL | Component Reference — Components V2 | https://docs.discord.com/developers/components/reference | Buttons/selects/text inputs/layout/media/file components and `IS_COMPONENTS_V2` restrictions. |

## 5. Product vs official API boundary

| Surface | End-user product | Supported official app API | Boundary |
|---|---|---|---|
| Accounts/identity | Human accounts, profiles, custom status, account switching | Dedicated bot user per app; scoped OAuth2 current-user data | A bot token is not a human account credential [S12]. |
| Friends/social graph | Friends, requests, blocks | `relationships.read` only with Social SDK approval; bots cannot friend | General bot adapters: `UNSUPPORTED`; never scrape client state [S12]. |
| DMs/group DMs | Human DMs and group calls/chats | Bots can open one-to-one DMs; DM list is approved-partner-only; group DM add requires each user’s `gdm.join` token; bots cannot join group DMs | Product presence does not imply ordinary bot access [S5][S7][S12]. |
| Servers/channels | Full server UI, text/voice/Stage/forum/media/announcement channels | Broad guild/channel HTTP API plus Gateway events | Permission and role-hierarchy constrained [S5][S8][S17]. |
| Messages | Client composition, replies, forwards, polls, reactions, media | Broad message REST/events/components APIs | Content may be redacted without `MESSAGE_CONTENT` [S6]. |
| Voice/video/screenshare | Human voice, camera, calls, Go Live/screen share, Stage | Bot voice protocol/audio and Stage management; video protocol details exist but high-level parity unclear | Do not advertise generic camera/screenshare capability [S14][S15]. |
| Read/notifications | Client unread state, notification preferences | No general read-receipt or notification-management bot API | `UNSUPPORTED`; typing is separate [S3]. |
| Automation | Apps installed to users or guilds | Bot/API/OAuth/webhook routes only | **Self-bots are unsupported policy**, not an adapter option [S21]. |

## 6. Capability evidence table

| Capability | Product support | Official API support/status | Restrictions / semantic notes | Evidence | Confidence |
|---|---|---|---|---|---|
| Bot/app identity | NATIVE | NATIVE | One bot user is attached to an app; app may also be user-installed | S1,S12 | HIGH |
| Authentication | NATIVE | NATIVE | Bot token; OAuth2 authorization-code/implicit/client-credentials/special bot/webhook flows; protect secrets | S12 | HIGH |
| Multi-account | NATIVE | API_LIMITED | Multiple app credentials can be configured independently; no single token multiplexes identities; human-account automation prohibited | S12,S21 | HIGH |
| Bot status/activity | NATIVE | API_LIMITED | Gateway Update Presence; supported bot activity fields limited; updates rate-limited; human custom-status parity not guaranteed | S3 | HIGH |
| User/profile get | NATIVE | API_LIMITED | Public user objects; `/users/@me` requires scopes for user data; bot can modify only documented current-bot fields | S7,S12 | HIGH |
| Contacts/friends/follow graph | NATIVE | API_LIMITED / UNSUPPORTED generally | `relationships.read` requires Social SDK approval; linked `connections` are third-party accounts, not friends; bots cannot friend | S7,S12 | HIGH |
| DM | NATIVE | API_LIMITED | Create DM and message bot DM; avoid unsolicited/bulk DMs; access is relationship/context constrained | S7,S6 | HIGH |
| Group DM | NATIVE | API_LIMITED | Bots cannot join; recipient add/remove requires user OAuth `gdm.join`; creation is legacy/deprecated-context limited | S5,S7,S12 | HIGH |
| Guild/server/space | NATIVE | NATIVE | Guild CRUD/read and events subject to install, permissions and ownership | S8,S3 | HIGH |
| Rooms/channels/categories | NATIVE | NATIVE | Text, voice, category, announcement, Stage, forum/media, DM types; CRUD permission-gated | S5,S8 | HIGH |
| Threads/topics/forums | NATIVE | NATIVE | Public/private/announcement threads; forum/media posts are threads; inherit parent permissions; archived-thread pagination | S5,S19 | HIGH |
| Message create/get/history | NATIVE | NATIVE | `VIEW_CHANNEL`, `READ_MESSAGE_HISTORY`, `SEND_MESSAGES`; cursor pagination; message content intent can redact fields | S6,S2 | HIGH |
| Message edit/delete | NATIVE | API_LIMITED | Author can edit own fields; moderation permissions for others; bulk delete 2–100 and rejects messages older than 14 days | S6 | HIGH |
| Reply | NATIVE | NATIVE | Message reference with reply semantics | S6 | HIGH |
| Quote | EMULATED | EMULATED | No distinct quote object/operation established; represent as attributed text/embed without claiming native quote semantics | S6 | MEDIUM |
| Forward | NATIVE | NATIVE | Message-reference forward creates immutable snapshot; only documented source message types, currently one snapshot | S6 | HIGH |
| Announcement repost/crosspost/follow | NATIVE | API_LIMITED | Crosspost announcement messages and follow announcement channel; permission/channel-type constrained | S5,S6 | HIGH |
| Rich text/mentions | NATIVE | NATIVE | Markdown-like formatting and structured mention arrays; use `allowed_mentions` to prevent accidental pings | S6,S23 | HIGH |
| Embeds/cards | NATIVE | NATIVE | Up to 10 rich embeds/6,000 embed characters on create; not a portable card schema | S6 | HIGH |
| Reactions/emoji | NATIVE | NATIVE | Add/remove/list reactions; custom emoji encoding and permissions; reaction Gateway events | S6,S3 | HIGH |
| Stickers | NATIVE | API_LIMITED | Send sticker IDs and guild-sticker management; availability/permissions/tier constraints | S6,S8 | HIGH |
| Polls | NATIVE | NATIVE | Create polls, poll object/results, answer-voter pagination and vote events; product has up to 10 answers | S6,S20,S3 | HIGH |
| Buttons/selects | NATIVE | NATIVE | Components trigger interactions; custom IDs and context constraints | S24,S10 | HIGH |
| Forms/modals | NATIVE | NATIVE | Modal interaction response and modal submit; modal cannot be opened in response to modal submit/PING | S10,S24 | HIGH |
| Components V2/media layouts | NATIVE | EXTENSION | `IS_COMPONENTS_V2` changes message rules; component-only create excludes traditional content/embeds/poll fields | S24,S6 | HIGH |
| Images/audio/video/files | NATIVE | NATIVE | Multipart attachment upload and CDN URLs; per-file entitlement limit, overall create-message request 25 MiB; voice-message flag/metadata are Discord-specific | S6,S23 | HIGH |
| Media groups/albums | NATIVE | EMULATED / EXTENSION | Multiple attachments and Components V2 media gallery exist, but portable “album” semantics do not | S24,S6 | MEDIUM |
| Members | NATIVE | API_LIMITED | Get/list/search/add/modify/remove; list and full events depend on `GUILD_MEMBERS`; add uses `guilds.join` OAuth | S8,S2,S12 | HIGH |
| Roles/permissions | NATIVE | API_LIMITED | Role CRUD/assignment and channel overwrites; hierarchy, owner/admin and bot’s highest role constrain operations | S8,S17 | HIGH |
| Moderation | NATIVE | API_LIMITED | Timeout, kick, ban, prune and AutoMod rule/actions require granular permissions; 2FA can gate elevated permissions | S8,S16,S12 | HIGH |
| Audit | NATIVE | API_LIMITED | `VIEW_AUDIT_LOG`; 45-day retention; pagination/filtering; optional audit reason on supported write endpoints | S18 | HIGH |
| Commands | NATIVE | NATIVE | Global/guild chat-input, user, message and entry-point commands; install/context/permissions; 200 creates/day/guild | S9 | HIGH |
| Interactions | NATIVE | API_LIMITED | Gateway or HTTP ingress, mutually exclusive; ACK/defer quickly; 15-minute token; ephemeral response supported | S10 | HIGH |
| Incoming webhooks | NATIVE | API_LIMITED | Token grants channel posting; can wait for Message result or receive 204; no event subscription | S11 | HIGH |
| Event subscriptions | NATIVE | API_LIMITED | Gateway is broad; Event Webhooks have a narrow catalog, signatures, retries, no realtime/order guarantee | S2,S3,S13 | HIGH |
| Presence receive | NATIVE | API_LIMITED | `GUILD_PRESENCES` is privileged; visibility/cache behavior depends on intents and guild scale | S2,S3 | HIGH |
| Typing | NATIVE | API_LIMITED | `TYPING_START` event and 10-second trigger endpoint; docs discourage routine bot use | S3,S5 | HIGH |
| Read receipt/unread state | API_LIMITED | UNSUPPORTED | Client tracks unread state, but no supported general sender-visible read-receipt or bot mark-read contract found | S3,S12 | MEDIUM |
| Feed/post/repost | API_LIMITED | EXTENSION | Forum posts and announcement crossposts exist; no general social feed/repost contract | S5,S6 | HIGH |
| Notifications | NATIVE | UNSUPPORTED | User client settings/notifications are not exposed as general bot management operations | S12 | MEDIUM |
| Voice audio | NATIVE | API_LIMITED | Separate Voice Gateway, UDP/RTP, Opus and encryption implementation; permissions and voice state required | S14,S3 | HIGH |
| Video/Go Live/screenshare | NATIVE | UNKNOWN / EXTENSION | Protocol includes video-era fields/codecs, but documented bot parity for camera/Go Live/screenshare lifecycle is insufficient | S14 | MEDIUM |
| Stage | NATIVE | API_LIMITED | Stage Instance CRUD plus voice suppression/request-to-speak semantics; Community/permissions matter | S15,S3 | HIGH |
| Text encryption | API_LIMITED | UNSUPPORTED for E2EE | HTTPS/WSS transport, but no app-facing end-to-end encrypted text-message model documented | S23 | MEDIUM |
| Voice encryption | NATIVE | API_LIMITED | DAVE E2EE is protocol work; docs state E2EE-only voice/video conversations from 2026-03-01 | S14 | HIGH |
| Federation | UNSUPPORTED | UNSUPPORTED | Discord guilds are hosted Discord resources; no federation protocol exposed | S8,S23 | HIGH |
| Tenant/compliance | EXTENSION | API_LIMITED | Guild ownership/admin/audit exist; not a generic enterprise tenant/compliance API | S8,S18 | MEDIUM |
| Idempotency | API_LIMITED | API_LIMITED | Message `nonce` ≤25 chars with `enforce_nonce` deduplicates same author over only the past few minutes; not universal | S6 | HIGH |
| Pagination | NATIVE | NATIVE | Snowflake cursor (`before`/`after`/`around`) and endpoint-specific `limit`; no universal page token | S6,S8,S23 | HIGH |
| Rate limits | NATIVE | API_LIMITED | Per-route/shared/global buckets; 429 + retry; baseline 50 r/s; never hard-code route buckets | S4 | HIGH |
| Passive reply window | N/A | API_LIMITED | No general passive-reply window; interaction initial response is 3 seconds and token continuation 15 minutes | S10 | HIGH |
| Payload/message limits | NATIVE | API_LIMITED | 2,000 content chars, 10 embeds/6,000 embed chars, 25 MiB create request; upload limit varies by entitlement/guild | S6,S23 | HIGH |

## 7. Inbound event inventory

### Gateway (WebSocket)

The official receive catalog [S3] is grouped below; delivery depends on intents, install context, permissions, visibility and session state.

- **Connection/session:** `HELLO`, `READY`, `RESUMED`, `RECONNECT`, `RATE_LIMITED`, `INVALID_SESSION`.
- **Commands/moderation:** `APPLICATION_COMMAND_PERMISSIONS_UPDATE`; `AUTO_MODERATION_RULE_CREATE/UPDATE/DELETE`; `AUTO_MODERATION_ACTION_EXECUTION`.
- **Channels/threads:** `CHANNEL_CREATE/UPDATE/DELETE/INFO`, `CHANNEL_PINS_UPDATE`; `THREAD_CREATE/UPDATE/DELETE`, `THREAD_LIST_SYNC`, `THREAD_MEMBER_UPDATE`, `THREAD_MEMBERS_UPDATE`.
- **Guilds/admin:** `GUILD_CREATE/UPDATE/DELETE`, `GUILD_AUDIT_LOG_ENTRY_CREATE`, `GUILD_BAN_ADD/REMOVE`, `GUILD_EMOJIS_UPDATE`, `GUILD_STICKERS_UPDATE`, `GUILD_INTEGRATIONS_UPDATE`, `GUILD_MEMBER_ADD/REMOVE/UPDATE`, `GUILD_MEMBERS_CHUNK`, `GUILD_ROLE_CREATE/UPDATE/DELETE`.
- **Events/media/integrations:** scheduled-event create/update/delete/user-add/user-remove; soundboard sound create/update/delete/bulk-update and requested sounds; integration create/update/delete; invite create/delete.
- **Interactions/messages:** `INTERACTION_CREATE`; `MESSAGE_CREATE/UPDATE/DELETE/DELETE_BULK`; reaction add/remove/remove-all/remove-emoji; poll vote add/remove.
- **Presence/user/voice:** `PRESENCE_UPDATE`, `TYPING_START`, `USER_UPDATE`; Stage instance create/update/delete; voice channel effect/start-time/status updates; `VOICE_STATE_UPDATE`, `VOICE_SERVER_UPDATE`.
- **App commerce:** entitlement and subscription create/update/delete.
- **Webhook metadata:** `WEBHOOKS_UPDATE` says a channel webhook changed; it does not deliver webhook message content.

Gateway dispatches are sequenced and sessions can resume from a saved sequence, but adapters must handle reconnect, duplicate effects around recovery, unavailable guilds and partial update objects [S2][S3].

### HTTP interaction ingress

`PING`, application command, message component, command autocomplete and modal submit interactions arrive either as `INTERACTION_CREATE` or signed HTTP requests, never both for the same configured app. HTTP requests require Ed25519 signature verification. Responses/callbacks are HTTP even when ingress is Gateway [S10].

### Event Webhooks

Signed HTTP Event Webhooks currently list: application authorized/deauthorized; entitlement create/update/delete; Quest enrollment (**currently unavailable**); lobby message create/update/delete; and game direct-message create/update/delete during active Social SDK sessions. Acknowledge with empty `204` within three seconds; failures retry exponentially for up to ten minutes. Delivery is not guaranteed realtime or ordered [S13]. This is not a general message/guild event transport.

## 8. Outbound operation inventory

- **Identity/OAuth:** exchange/refresh/revoke tokens; inspect authorization/current user; install bot/commands to guild or user; retrieve current bot/application; get scoped guilds/connections/member data [S12][S7]. HTTP returns typed JSON on success and structured status/error-code bodies on failure [S23].
- **Guild/admin:** get/modify/create/delete guilds where allowed; channels; roles; member add/modify/remove; timeout; kick/ban/unban; prune; invites; scheduled events; integrations; templates and widgets where documented [S8]. Permission and hierarchy failures are synchronous 403/400 results.
- **Channels/threads/forums:** channel CRUD and overwrites; follow announcements; start/join/leave/list threads; create forum/media posts; archive/lock/tag threads; set voice-channel status; typing; constrained group-DM recipient operations [S5]. Successful creates return channel/thread objects; many deletes/typing operations return 204 and emit Gateway events.
- **Messages:** list/get/search where endpoint access allows; create/edit/delete/bulk-delete; reply/forward/crosspost; pin/unpin; add/remove/list reactions; poll voters/end poll [S6]. Create returns a Message and emits `MESSAGE_CREATE`; `enforce_nonce` offers narrow dedupe. Bulk delete is all-or-error if any message is too old.
- **Media:** multipart upload with attachment metadata; CDN URLs provide retrieval. Preserve filename, content type, size, dimensions/duration/waveform where present and treat URLs as platform resources rather than permanent storage contracts [S6][S23].
- **Commands/interactions/components:** register/list/edit/delete global or guild commands; send immediate/deferred callbacks, autocomplete, modal, activity launch; get/edit/delete original response; create/get/edit/delete follow-ups [S9][S10][S24]. Initial callbacks may return 204 or 200 with callback resource depending on `with_response`; follow-up create waits and returns a message.
- **Webhooks:** create/list/get/modify/delete; execute into channels or threads; edit/delete webhook messages [S11]. `wait=false` may return 204, while `wait=true` returns the created Message. Tokenized webhook routes use the URL secret instead of bot authorization.
- **Moderation/audit:** AutoMod rule CRUD; audit-log read; write operations that support it may include URL-encoded `X-Audit-Log-Reason` [S16][S18]. Audit appearance is eventual platform side effect, not a transaction result.
- **Gateway client commands:** identify/resume/heartbeat; request guild members/soundboard/channel info; update presence; update voice state [S2][S3]. They are protocol messages, not actor-owned sockets or ordinary HTTP results.
- **Voice/Stage:** establish Voice Gateway session after paired voice-state/server events, negotiate encryption and send/receive RTP media; Stage CRUD over HTTP [S14][S15]. Voice connection establishment is asynchronous and event-correlated.

## 9. Normalized common-capability candidates

These are safe as small discoverable capabilities, not one giant `IBot`:

1. **Identity and installation:** `AppIdentity`, `BotIdentity`, OAuth authorization and scoped installation. Keep Discord install context (`guild` versus `user`) optional [S1][S12].
2. **Conversation locator:** `ConversationRef(platform, kind, id, parentId?)` supporting DM, room/channel and thread. Forum posts normalize as threads only while preserving forum tags in an extension [S5].
3. **Message CRUD/history:** create/get/page/edit-own/delete, with author/channel/timestamp/content and typed attachments. Permissions and content-redaction capability must be discoverable [S6][S2].
4. **Reply and reaction:** message reference/reply and emoji reaction operations/events generalize well. Forward snapshots must not be reduced to replies [S6].
5. **Rich outbound content:** text, mentions, attachments, embeds/cards and simple buttons can normalize as optional blocks, while raw Discord components remain available [S6][S24].
6. **Membership/RBAC:** member, role, assignment and permission checks generalize, but expose hierarchy/overwrite calculations explicitly rather than a Boolean `isAdmin` [S8][S17].
7. **Commands/interactions:** command invocation with typed options, component invocation, defer/ack and follow-up are reusable patterns. Carry response deadline and ephemeral support as capability metadata [S9][S10].
8. **Event subscription:** process-owned ingress yielding serializable typed events with event ID/sequence/source/raw payload. Delivery mode (`gateway`, `interaction_http`, `event_webhook`) is metadata [S2][S10][S13].
9. **Rate-aware request/result:** typed success, platform error, retry-after and rate-limit scope; idempotency support is operation-specific [S4][S6].
10. **Moderation basics:** timeout/kick/ban and audit reference can normalize as optional capabilities; AutoMod rule language stays Discord-specific [S16][S18].

## 10. Required namespaced extensions

- `discord.guild`, role hierarchy, permission bitset and per-channel overwrite evaluation [S8][S17].
- `discord.channel_type`, announcement following/crossposting, Stage, forum/media post tags/layout/default reaction and thread archive durations [S5].
- `discord.message.flags`, forwarded immutable snapshots, stickers, polls, voice-message metadata and allowed-mention controls [S6].
- `discord.components.v2` including layouts, media galleries, file display/upload and the component-only message rule [S24][S6].
- `discord.interaction` install/context/authorizing-owner fields, deferred callback types, ephemeral flag and 3-second/15-minute lifecycle [S10].
- `discord.gateway` intents, shard ID/count, sequence/session resume, unavailable guilds and partial events [S2][S3].
- `discord.webhook` token ownership, `wait`, thread targeting and webhook-event signing/retry semantics [S11][S13].
- `discord.automod` trigger/action schemas and execution-event content redaction [S16][S2].
- `discord.voice` Voice Gateway, RTP/Opus, DAVE, SSRC and Stage suppression/request-to-speak semantics [S14][S15].
- A typed `discord.raw` escape hatch is justified for evolving message/component/event fields, but must retain API version and never enable undocumented user-client calls.

## 11. Limits, policy, review, regional and lifecycle risks

- **Self-bots:** categorically unsupported. Do not accept user tokens, replay client traffic or offer unofficial automation fallback [S21][S12].
- **Privileged data:** `GUILD_MEMBERS`, `GUILD_PRESENCES`, `MESSAGE_CONTENT` require portal enablement and, at scale, review. Missing `MESSAGE_CONTENT` empties content/embeds/attachments/components and omits poll on affected messages [S2][S6].
- **Scale/review:** docs mention verification for apps in 100+ guilds and privileged-intent review after 10,000 users can see the app, with annual reapplication. Large-bot sharding and session-start limits are returned by Get Gateway Bot; do not hard-code shard/session quotas [S2][S22].
- **HTTP limits:** dynamic per-route/shared buckets and baseline 50 requests/s global per bot; 10,000 invalid 401/403/429 requests per 10 minutes can trigger temporary restriction. Interaction endpoints are outside the bot global limit [S4].
- **Gateway limits:** Identify concurrency and daily remaining starts come from `session_start_limit`; presence and other opcodes have their own limits. Resume rather than re-identify when possible [S2][S3].
- **Content/payload:** 2,000 message-content characters, up to 10 embeds/6,000 embed characters, 25 MiB create-message request; upload size per file can vary with user/guild entitlement. Components V2 rejects mixed legacy fields [S6][S23][S24].
- **Deletion/audit:** bulk deletion cannot include messages over 14 days; audit retention is 45 days [S6][S18].
- **Interactions/webhooks:** initial interaction response in three seconds, token 15 minutes. Event Webhooks require signed requests, 204 in three seconds and may be disabled after repeated failures [S10][S13].
- **Restricted scopes:** `relationships.read` (Social SDK approval), `dm_channels.read` (approved partners), `voice` on a user’s behalf (approved partners), and multiple RPC scopes are not general bot capabilities [S12].
- **Voice lifecycle:** DAVE/E2EE became mandatory for documented calls on 2026-03-01. Voice clients require protocol upgrades independent of HTTP API v10 [S14].
- **Paid/admin/region:** upload sizes, stickers/emoji and some server features vary with Nitro/guild boost/Community configuration or admin permissions. No stable region-specific API difference was established; commercial entitlement should be runtime metadata, not a constant [S5][S19][S23].
- **Deprecation:** group-DM creation context is legacy; `PREMIUM_REQUIRED` interaction response is deprecated; Quest webhook event is currently unavailable [S7][S10][S13].

## 12. Conflicts and unknowns

1. **Verification versus privileged-intent review:** Gateway text still says verification is required for apps in 100+ guilds, while current review material describes 10,000 visible users for ongoing privileged-intent access. These appear to be separate app-verification and sensitive-data-review gates, but portal behavior is authoritative at runtime [S2][S22].
2. **Video for bots:** Voice documentation contains video protocol/version material and E2EE requirements, but a clear supported end-to-end bot camera/Go Live/screenshare product contract was not found. Status: `UNKNOWN`; test only under official docs/support and keep namespaced [S14].
3. **Read state:** no official general bot endpoint/event for read receipts or marking messages read was found. Product unread state should not be mapped to delivery/read receipts. Status: `UNSUPPORTED` for common API [S3][S12].
4. **Product Help Center access:** forum, polls and self-bot pages were discoverable through official search but direct retrieval returned 403 in this environment [S19–S21]. Developer-resource evidence independently confirms forum/poll API facts; self-bot policy is also restated in OAuth bot-account distinctions [S5][S6][S12].
5. **Exact attachment size:** reference docs describe a default per-file limit that can vary with Nitro/guild boost, while Create Message independently caps total request size at 25 MiB. The adapter must use interaction `attachment_size_limit` or runtime/API errors rather than one constant [S10][S23][S6].
6. **Text E2EE/compliance:** no official end-to-end text encryption, federation or comprehensive enterprise compliance API was established. Transport security should not be labeled E2EE. Further legal/compliance review is outside this technical lane.

## 13. OBCX design implications

1. **Split capabilities, not `IBot`:** define small providers such as `MessageRead`, `MessageWrite`, `Reaction`, `Thread`, `GuildAdmin`, `InteractionResponder`, `WebhookPublisher`, `Presence`, `VoiceAudio` and `AuditRead`. Capability discovery must report intent, permission, install-context and review requirements [S2][S5][S10][S17].
2. **Serializable transport messages:** the process owns HTTP/WebSocket/UDP. Business actors receive `DiscordGatewayEvent`, `DiscordInteraction`, `DiscordWebhookEvent`, `DiscordVoiceStateChanged` DTOs and issue typed request messages. Keep shard/session/sequence and acknowledgement state in transport actors [S2][S3][S13][S14].
3. **Message DTO:** include snowflake ID, conversation/parent/thread refs, author/member/webhook identity, timestamps, content availability state (`present`, `redacted_by_intent`, `absent`), embeds, attachments, components, stickers, poll, reactions, flags, reference and forwarded snapshots [S6]. Never turn redacted content into an empty user message.
4. **Ingress envelope:** `{platform, transport, eventType, receivedAt, sequence?, shard?, interactionId?, guildId?, channelId?, rawVersion, raw}`. Gateway sequence is resumable; HTTP webhook events need signature result and dedupe storage; interactions need absolute response deadline [S2][S10][S13].
5. **Egress result:** return typed `Accepted`, `Created<T>`, `NoContent`, `Deferred`, `RateLimited(retryAfter, scope, bucket)`, `PermissionDenied`, `IntentUnavailable`, `InvalidRequest(code, errors)` and `UnknownPlatformError(raw)` [S4][S10][S23]. Do not infer completion from an eventual Gateway echo.
6. **Permission planner:** preflight calculated guild/channel permissions and role hierarchy where cached, but still treat HTTP 403 as authoritative due to races. Surface missing Discord permission separately from missing OAuth scope/privileged intent [S12][S17].
7. **Idempotency:** allow caller nonce only on operations that document it. For message create, generate stable ≤25-character nonce and set `enforce_nonce` when desired, while documenting the short dedupe window. For other writes, use OBCX dedupe/outbox state rather than claiming Discord idempotency [S6].
8. **Interaction scheduler:** ACK/defer before business work, then follow up through the token. Model ephemeral state as immutable after initial defer and expire continuation at 15 minutes [S10].
9. **Rate controller:** centralize per-token global budget and per-major-resource bucket tracking from headers; honor `Retry-After`; separately monitor invalid-request counts. Webhook-token and interaction traffic require correct separate scopes [S4][S11].
10. **Content safety:** default `allowed_mentions` to no broad mention expansion; validate 2,000/6,000/request/upload limits; treat Components V2 as a mutually constrained rendering mode [S6][S24].
11. **Voice isolation:** keep Voice Gateway/RTP/DAVE in a dedicated adapter capability with codec/encryption negotiation and asynchronous state correlation. Do not expose generic video/screenshare until the unknown is resolved [S14].
12. **Policy enforcement:** configuration schema should accept bot token/client credentials/webhook tokens, never ordinary user tokens. Reject a “self-bot” mode explicitly with a policy diagnostic [S12][S21].

## 14. Claim-to-source checklist

| Claim/conclusion | Sources |
|---|---|
| Broad guild/channel/thread/forum administrative model | S5,S8,S17,S19 |
| Dedicated bot identity; self-bots forbidden | S1,S12,S21 |
| Gateway/HTTP/interactions transport split | S2,S3,S10 |
| Rich message operations and content-intent redaction | S6,S2,S24 |
| Commands/components/modals, deadline and ephemeral follow-ups | S9,S10,S24 |
| Incoming versus Event Webhooks | S11,S13 |
| Friends/DM social data is restricted | S7,S12 |
| Voice audio supported; video parity unknown; Stage supported | S14,S15 |
| Presence/typing constrained; no read-receipt API | S2,S3,S12 |
| Roles/moderation/audit and retention | S8,S16,S17,S18 |
| HTTP rate/invalid-request limits | S4 |
| Verification, intent review, sharding risks | S2,S22 |
| Product/API boundary and no user-client inference | S12,S21 |
| DTO/capability split recommendation | S2,S5,S6,S10,S17 |
| Process-owned transport and ingress envelope | S2,S3,S10,S13,S14 |
| Typed results/rate controller | S4,S10,S11,S23 |
| Message DTO/content availability | S6,S2 |
| Interaction-first defer scheduler | S10 |
| Policy-safe credential model | S12,S21 |
