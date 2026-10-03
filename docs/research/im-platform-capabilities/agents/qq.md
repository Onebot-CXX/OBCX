# QQ platform capability audit

## 1. Lane metadata

- **Platform:** Tencent QQ.
- **Surfaces kept separate:** (a) consumer QQ, (b) the official QQ Open Platform / QQ Bot API, and (c) OneBot 11, an unofficial compatibility protocol implemented by third-party adapters.
- **Research date:** 2026-08-17 UTC.
- **API generation/freshness:** QQ Bot API v2 and current official documentation/SDK material. The official documentation repository contains both current C2C/group material and older QQ Channel (Guild) material. WebSocket documentation itself says that transport was to be phased out by the end of 2024; current integrations should prefer Webhook. Some pages and SDKs still describe active-message code paths after the documentation says active push was withdrawn on 2025-04-21. These conflicts are called out rather than silently resolved.
- **Status vocabulary:** `NATIVE`, `API_LIMITED`, `EMULATED`, `EXTENSION`, `UNSUPPORTED`, `UNKNOWN` as defined in the research brief.
- **Important boundary:** Nothing in OneBot 11 is evidence that Tencent's official QQ Bot API supports the same operation.

## 2. Executive findings

1. Consumer QQ is a broad social/communications product with friend and group chat, voice/video calling, and file transfer. Those product features do **not** imply corresponding Bot APIs. [S1]
2. Official bots are applications created on QQ Open Platform and authenticate server-to-server with `AppID` + `ClientSecret`; the resulting access token is normally valid for up to 7,200 seconds and is sent as `Authorization: QQBot ACCESS_TOKEN`. [S2]
3. The current official message surface spans **C2C**, **QQ group**, **Guild text channel**, and **Guild DM**, but the event semantics differ: C2C receives user messages, groups receive messages that @ the bot, public Guilds receive @ messages, and full Guild message delivery is a restricted private-bot intent. [S3][S4]
4. C2C/group IDs are opaque `openid` / `group_openid` / `member_openid`, not consumer QQ numbers. OBCX must not model official QQ identity as a numeric QQ account. [S4]
5. Passive replies are tied to an inbound `msg_id`/`event_id`; current official SDK guidance requires the triggering C2C message to be within five minutes for streaming. The documentation repository says arbitrary active push was withdrawn on 2025-04-21, despite newer SDK code retaining proactive-send helpers. Treat context-free active sends as **unavailable unless a bot's live capability grant proves otherwise**. [S4][S5]
6. C2C and QQ groups officially support text plus uploaded image, voice, video, and file media; current SDK guidance gives 30/20/100/100 MB maxima respectively and a two-step upload-then-send model. C2C alone supports replace-mode streaming messages. [S4]
7. Markdown and inline keyboards/buttons are permission/review-sensitive. Button clicks arrive as `INTERACTION_CREATE` and must be acknowledged; the current SDK says ACK within five seconds. Cards/Ark/Embed exist in low-level message payloads, but current cross-scenario availability and grants are insufficiently documented, so they remain namespaced/conditional. [S3][S4]
8. Webhook is the supported forward-looking event transport: HTTPS callback, Ed25519-style signature validation, explicit event selection, and callback ports 80/443/8080/8443. The official page says WebSocket was to be phased out and is no longer maintained. [S3]
9. Intents are permission-gated. Only Guild lifecycle, public Guild @ messages, and Guild member events are documented as basic; other intents require application. Subscribing to an unauthorized intent can close a WebSocket connection. [S3]
10. Official Guild APIs include member/role/mute operations subject to bot administrator status and API permission. This does **not** establish QQ-group member enumeration or moderation APIs; current group-bot documentation only clearly establishes messaging and robot lifecycle/notification-toggle events. [S3][S6]
11. The sandbox is a distinct data/environment boundary. Only configured test channels, users and groups can trigger events or be operated on. Public release requires review and subsequent manual publication; current documentation also describes production IP allowlisting. [S7]
12. Quotas are dynamic rather than one stable QPS constant: token acquisition can be rate-limited; Gateway returns remaining session starts and concurrency; media prepare has daily quotas; messaging has scenario/permission limits. OBCX should surface observed limit errors and retry metadata, not hard-code a universal QQ rate. [S2][S3][S4]
13. OneBot 11 is a community specification derived from CQHTTP compatibility. It exposes private/group sends, message lookup/deletion, friend/group lists, extensive moderation, CQ/segment media, and HTTP/WebSocket transports—but every such feature is **adapter-boundary capability only**, never proof of official QQ support. [S8][S9][S10]

## 3. Research action log

- Read the lane brief in full before research.
- Searched official QQ Bot documentation for authentication, C2C/group/Guild message routes, events/intents, active/passive messages, media, Markdown/buttons, sandbox, review, IP allowlists, and limits.
- Checked the official documentation site and `tencent-connect/bot-docs` source repository. The VuePress site is partly client-rendered and search indexing is uneven; raw official repository pages were used where possible.
- Read the current official `tencent-connect/qqbot-nodejs` repository, including `README.zh-CN.md`, `USAGE.md`, protocol routes/types, message API implementation, and Gateway constants.
- Checked official Guild role/mute endpoint pages in the documentation repository.
- Checked Tencent's official QQ product page for consumer/product scope.
- Read the OneBot 11 specification repository: introduction, communications, public actions, message segments, and event families.
- Rejected third-party adapter documentation as proof of Tencent API support. It was used only to locate primary pages or identify conflicts.
- **Inaccessible/weak points:** several current management-console rules (exact review criteria, per-bot quotas, geographic eligibility, and live scenario grants) require an authenticated QQ Open Platform account and are not fully represented in public pages. Exact active-message withdrawal text is discoverable in the official documentation repository/mirror but conflicts with newer official SDK helpers. These points remain explicit risks/unknowns.

## 4. Source register

Access date for all sources: **2026-08-17**.

| ID | Authority | Title / relevant section | URL | What it proves |
|---|---|---|---|---|
| S1 | OFFICIAL | Tencent, **QQ** product page | https://www.tencent.com/products/qq/ | Consumer QQ is an IM/social product supporting chat, voice/video calls, and point-to-point file transfer. Product evidence only. |
| S2 | OFFICIAL | QQ Bot Docs, **获取访问凭证** / token lifetime and header | https://bot.q.qq.com/wiki/develop/api-v2/dev-prepare/access-token.html | `AppID` + `clientSecret`, token endpoint, ≤7200-second lifetime, `QQBot` authorization scheme, token rate-limit errors, server-side secrecy. |
| S3 | OFFICIAL | QQ Bot Docs source, **事件订阅与通知** / Webhook, WebSocket, Intents | https://raw.githubusercontent.com/tencent-connect/bot-docs/master/docs/develop/api-v2/dev-prepare/interface-framework/event-emit.md | Payload, delivery modes, Webhook validation, WS deprecation notice, all intent/event mappings, base vs special intent grants, Gateway session-start limits. |
| S4 | OFFICIAL | Tencent QQ Open Platform Node.js SDK, **README/USAGE/protocol** | https://github.com/tencent-connect/qqbot-nodejs | Current routes and typed protocol behavior: C2C/group/Guild/DM messaging, opaque IDs, media, streaming, keyboards/interactions, reply targeting, errors and SDK-stated limits. Relevant headings: “消息发送”, “媒体上传与发送”, “C2C 流式消息”, “平台限制”. |
| S5 | OFFICIAL | Tencent bot documentation repository, current **send-message lifecycle note** | https://github.com/tencent-connect/bot-docs | Repository documentation states active push ceased on 2025-04-21. Also contains older material that conflicts with current lifecycle. Exact public page indexing is unstable; therefore confidence is medium and live-console verification is required. |
| S6 | OFFICIAL | QQ Bot Docs source, **Guild role/mute endpoints** | https://raw.githubusercontent.com/tencent-connect/bot-docs/master/docs/develop/api-v2/server-inter/channel/role-group/put_guild_member_role.md | Add member role requires permission and bot Guild administrator; success 204. Companion pages: [remove role](https://raw.githubusercontent.com/tencent-connect/bot-docs/master/docs/develop/api-v2/server-inter/channel/role-group/delete_guild_member_role.md), [mute member](https://raw.githubusercontent.com/tencent-connect/bot-docs/master/docs/develop/api-v2/server-inter/channel/speak/patch_guild_member_mute.md), [mute Guild](https://raw.githubusercontent.com/tencent-connect/bot-docs/master/docs/develop/api-v2/server-inter/channel/speak/patch_guild_mute.md). |
| S7 | OFFICIAL | QQ Bot documentation repository, **sandbox / publish / scope management** | https://raw.githubusercontent.com/tencent-connect/bot-docs/master/docs/README.md | Sandbox host/data isolation and configured users/groups/channels; publication flow and historical developer-type restrictions. Repository images/pages also document review and IP allowlisting. Because this landing document aggregates multiple generations, current console verification is required. |
| S8 | SPEC | OneBot 11, **Introduction** | https://github.com/botuniverse/onebot-11 | OneBot's community/CQHTTP-compatibility origin and portability goal; not a Tencent API. |
| S9 | SPEC | OneBot 11, **Public API** | https://github.com/botuniverse/onebot-11/blob/master/api/public.md | Adapter actions for private/group messaging, lookup, moderation, contacts, media retrieval and status. |
| S10 | SPEC | OneBot 11, **Message segments and events** | https://github.com/botuniverse/onebot-11/blob/master/message/segment.md | Segment set; companion [event overview](https://github.com/botuniverse/onebot-11/blob/master/event/README.md) and [communications](https://github.com/botuniverse/onebot-11/blob/master/communication/README.md). |
| S11 | SECONDARY | Apple App Store, **QQ app listing** | https://apps.apple.com/cn/app/qq/id444934666 | Corroborates current consumer friend/group messaging, calling and cross-device file transfer. Not used to infer Bot API support. |

**Dropped sources:** SEO tutorials, adapter docs, unofficial API wrappers, archived QQ Channel tutorials, and reverse-engineered client projects were excluded as evidence of official support. GitExtract/search summaries were used only to find claims in the official repository and are not treated as independent authority.

## 5. Product vs official API boundary

| Surface | Consumer QQ product | Official QQ Bot API | OneBot 11 boundary |
|---|---|---|---|
| Identity | Human QQ account, profile, friends/social usage. | App/bot identity with AppID and opaque OpenIDs; not a logged-in human QQ client. | Usually presents a logged-in `self_id` and numeric QQ IDs through an adapter; unofficial relative to Tencent Bot API. |
| Private chat | Friend/private messaging is core product functionality. | C2C user↔bot messaging; Guild DM is a separate Guild-scoped concept. | `send_private_msg` and private message events if the implementation supports them. |
| Group chat | Full human QQ-group conversation and administration. | Bot receives `GROUP_AT_MESSAGE_CREATE`; sends/replies through group OpenID route. No public evidence of full group history/member/admin parity. | Rich group messaging and moderation actions in spec; adapter-dependent and not proof of official APIs. |
| QQ Channel / Guild | Community space with channels where enabled in the consumer client. Availability/product emphasis has changed over time. | Separate Guild/channel API generation: public @ events, restricted full-message intent, Guild DM, member/role/moderation endpoints. | OneBot 11 has only private/group core semantics; Guild/channel requires implementation extensions. |
| Calls/files/social | Voice/video calls, file transfer and social functions exist in product. | Media messages exist; no official bot voice/video-call control or general friend/social graph API established. | Some message media/contact actions exist, but no official-platform implication. |

## 6. Capability evidence table

`Product` describes consumer QQ. `API` describes only Tencent's official Bot API.

| Capability | Product support | Official API status | Restrictions / exact boundary | Evidence | Confidence |
|---|---|---|---|---|---|
| Bot/app identity | Product: `EXTENSION` (bots appear as special identities) | `NATIVE` | Create Open Platform app; AppID/bot identity, not human client login. | S2, S4 | HIGH |
| Authentication | N/A | `NATIVE` | Server-side AppID + ClientSecret → token; ≤7200 s; `Authorization: QQBot …`; never expose in frontend. | S2 | HIGH |
| Multi-account | Product: multiple consumer accounts are a client concern | `API_LIMITED` | Multiple independent apps/SDK instances are possible; no tenant-wide multi-bot account API is documented. | S4 | MEDIUM |
| Bot presence/status | Product: human online status exists | `UNSUPPORTED` / `UNKNOWN` | No official set-presence/status API found. Gateway readiness is transport state, not QQ presence. | S3, S4 | MEDIUM |
| User/profile lookup | Product: profiles exist | `API_LIMITED` | `/users/@me` is used for bot identity; event authors provide scoped opaque IDs. Arbitrary consumer profile lookup not established. | S2, S4 | MEDIUM |
| Contacts/friends/follow graph | Product: friend/social graph is core | `API_LIMITED` | `FRIEND_ADD`/`FRIEND_DEL` lifecycle events exist; no official friend-list or follow/unfollow API found. | S3 | HIGH |
| C2C DM | Product: private chat `NATIVE` | `API_LIMITED` | User-to-bot C2C messages; opaque `user_openid`; passive reply context and grants apply. | S3, S4 | HIGH |
| QQ group | Product: group chat `NATIVE` | `API_LIMITED` | Official receive event is group **@ bot** message; target is `group_openid`, sender `member_openid`. Not full group message firehose. | S3, S4 | HIGH |
| Guild/server/space | Product: QQ Channel/Guild exists but current availability varies | `API_LIMITED` | Guild lifecycle and management APIs remain documented; public/private bot and API permission distinctions apply. Lifecycle risk is high. | S3, S6 | MEDIUM |
| Text channel | Product: Guild text channels | `API_LIMITED` | Public bots receive @ messages (`AT_MESSAGE_CREATE`); `MESSAGE_CREATE` for all messages is private-bot-only. | S3, S4 | HIGH |
| Guild DM | Product: channel-associated private message | `API_LIMITED` | Separate `DIRECT_MESSAGE_CREATE` intent and `/dms/{guild_id}/messages`; special intent permission. | S3, S4 | HIGH |
| Thread/topic/forum | Product: historically available in Guild forums | `API_LIMITED` | Thread/post/reply create/update/delete events under private-bot-only `FORUMS_EVENT`; current outbound forum-operation inventory was not confidently verified. | S3 | MEDIUM |
| Message create/send | Product: `NATIVE` | `API_LIMITED` | POST routes for C2C, group, channel, Guild DM. Scenario grant, reply context and content review apply. | S4 | HIGH |
| Message get/history | Product: history exists | `UNSUPPORTED` / `UNKNOWN` | No current general C2C/group history or arbitrary-message-get API established. Do not infer from OneBot. | S4, S9 | MEDIUM |
| Message edit | Product: limited product semantics vary | `UNSUPPORTED` except streaming `EXTENSION` | No general edit. C2C streaming repeatedly replaces one in-progress Markdown message. | S4 | HIGH |
| Message delete/recall | Product: recall exists | `API_LIMITED` | Current official SDK exposes recall for C2C/group. Guild deletion events exist; exact sender/time/permission constraints are not fully public. | S3, S4 | MEDIUM |
| Reply/quote | Product: reply/quote exists | `API_LIMITED` | Passive reply uses inbound `msg_id`; message references are available in some send payloads. Preserve trigger and reference separately. | S4 | HIGH |
| Forward/multi-forward | Product: forwards exist | `UNSUPPORTED` / `UNKNOWN` | No current official bot forward/history operation verified. OneBot `forward`/`node` is adapter-only. | S9, S10 | HIGH |
| Rich text/mentions | Product: styled content and @ mentions | `API_LIMITED` | Markdown needs a bot permission; public/group receive semantics are mention-gated. Exact mention-send grammar is scenario-specific. | S3, S4 | HIGH |
| Reactions | Product: reactions available in Guild contexts | `API_LIMITED` | Guild reaction add/remove events use special `GUILD_MESSAGE_REACTIONS` intent; not established for C2C/QQ group. | S3 | HIGH |
| Stickers/faces | Product: `NATIVE` | `UNKNOWN` | Incoming text may contain platform face tags, but no stable cross-scenario sticker API verified. OneBot `face` is not proof. | S4, S10 | MEDIUM |
| Polls/forms | Product: may exist in specific clients/Guild features | `UNSUPPORTED` / `UNKNOWN` | No official bot poll/form API verified. | — | LOW |
| Markdown | Product: rendered bot content | `API_LIMITED` | Explicit permission/review; ungranted use can fail (`40034090` in SDK guidance). | S4 | HIGH |
| Cards / Ark / Embed | Product: rich bot cards can render | `EXTENSION` / `UNKNOWN` | Low-level official SDK passes `ark`, `embed`, and message type fields, but current grants and scenario matrix are not sufficiently documented. Keep `qq.*`. | S4 | MEDIUM |
| Buttons/keyboards | Product: interactive bot buttons | `API_LIMITED` | Inline keyboard + `INTERACTION_CREATE`; interaction ACK required within 5 seconds per SDK guidance; permissions/templates may be reviewed. | S3, S4 | HIGH |
| Commands/interactions | Product: configured services/commands | `API_LIMITED` | Interactions are intent-gated; management-console command/service configuration and review apply. No universal slash-command protocol should be inferred. | S3, S7 | MEDIUM |
| Image | Product: `NATIVE` | `API_LIMITED` | C2C/group upload+send; receive attachments. SDK maximum 30 MB. Guild outbound current parity is not assumed. | S4 | HIGH |
| Audio/voice message | Product: `NATIVE` | `API_LIMITED` | C2C/group upload+send; SDK maximum 20 MB. This is a media message, not a call. | S4 | HIGH |
| Video message | Product: `NATIVE` | `API_LIMITED` | C2C/group upload+send; SDK maximum 100 MB. | S4 | HIGH |
| File | Product: `NATIVE` | `API_LIMITED` | C2C/group upload+send; SDK maximum 100 MB; upload prepare/daily limits and TTL apply. | S4 | HIGH |
| Media group/album | Product: possible client grouping | `UNSUPPORTED` / `UNKNOWN` | No atomic album/media-group operation verified. Can only emulate sequential sends, losing atomicity. | S4 | MEDIUM |
| Upload/download | Product: `NATIVE` | `API_LIMITED` | Upload can return `file_info` with TTL; URL/file-data and chunked paths vary. Inbound attachments have URLs; auth/TTL rules must be preserved. | S4 | HIGH |
| Guild members | Product: member lists | `API_LIMITED` | Guild member events and management endpoints exist. This does not apply to QQ groups. | S3, S6 | HIGH |
| QQ-group members | Product: member list/admin UI | `UNSUPPORTED` / `UNKNOWN` | No official QQ group member enumeration API verified for the bot surface. | S3, S4 | HIGH |
| Guild roles/permissions | Product: Guild roles | `API_LIMITED` | Add/remove member role requires API permission and bot Guild-administrator status. | S6 | HIGH |
| QQ-group roles/permissions | Product: owner/admin/member | `UNSUPPORTED` / `UNKNOWN` | No public official API parity established. Ignore OneBot moderation actions as evidence. | S9 | HIGH |
| Moderation | Product: group/Guild administration | `API_LIMITED` | Guild mute/member-role operations documented; no supported QQ-group moderation parity established. | S6 | HIGH |
| Audit | Product: platform moderation | `API_LIMITED` | `MESSAGE_AUDIT_PASS/REJECT` special intent; application/content review also applies. | S3, S7 | HIGH |
| Webhooks/subscriptions | N/A | `NATIVE` | HTTPS callback, signature validation, explicit event selection; ports limited to 80/443/8080/8443. | S3 | HIGH |
| WebSocket Gateway | N/A | `API_LIMITED` / lifecycle risk | Official page says phase-out by end-2024 and no further maintenance. Do not choose it for a new durable integration. | S3 | HIGH |
| Presence/typing/read receipt | Product: presence/read UI varies | Typing `API_LIMITED`; presence/read `UNSUPPORTED` | C2C typing input notification only. No general presence or read-receipt API verified. | S4 | HIGH |
| Voice/video call/live | Product: calls `NATIVE` | `UNSUPPORTED` | Guild audio-action events concern playback/mic state and require intent; no general bot call control. | S1, S3 | HIGH |
| Feed/post/repost/follow/notification | Product: broader QQ social ecosystem exists | `UNSUPPORTED` / `UNKNOWN` | Outside verified Bot API. Notification-toggle lifecycle events do not equal arbitrary notification delivery. | S3 | MEDIUM |
| Encryption/federation | Product transport is Tencent-operated | `UNSUPPORTED` | No bot-controlled E2EE or federation API. Webhook TLS/signatures are transport integrity, not end-to-end encryption. | S3 | HIGH |
| Tenant/compliance | Product/account review ecosystem | `EXTENSION` / `UNKNOWN` | App review, scopes and developer qualification exist; no portable tenant model or complete public regional/compliance matrix. | S7 | MEDIUM |
| Idempotency | N/A | `API_LIMITED` | `msg_id`/`event_id` + `msg_seq` participate in reply correlation/deduplication; inbound callbacks may duplicate, so business idempotency is still required. | S3, S4 | HIGH |
| Pagination | Product UI abstracts it | `API_LIMITED` | Guild list/member operations historically paginate; current C2C/group message history is not available. Model per-operation cursors, not one global paginator. | S6, S7 | MEDIUM |
| Rate limits/quotas | N/A | `API_LIMITED` | Token `Too many requests`; Gateway session-start counters; upload daily quota; no stable universal QPS. | S2, S3, S4 | HIGH |
| Passive reply window | N/A | `API_LIMITED` | Five-minute constraint is explicit for C2C streaming and historically for ordinary passive replies. Exact current per-scenario reply count/window requires live validation. | S4 | MEDIUM |
| Payload/message limits | Product varies | `API_LIMITED` / `UNKNOWN` | Media maxima documented by current SDK; exact text/Markdown/button row/count and per-route limits were not publicly consolidated. | S4 | MEDIUM |

## 7. Inbound event inventory

Delivery is by **Webhook** (recommended) or legacy/deprecated **WebSocket Gateway**. Webhook payloads share `{id, op, d, s, t}` and require signature validation. Event selection is configured in the console; WebSocket uses intent bits. [S3]

| Intent / permission | Official events | Boundary |
|---|---|---|
| `GUILDS (1<<0)` — basic | `GUILD_CREATE`, `GUILD_UPDATE`, `GUILD_DELETE`, `CHANNEL_CREATE`, `CHANNEL_UPDATE`, `CHANNEL_DELETE` | Bot/Guild lifecycle. |
| `GUILD_MEMBERS (1<<1)` — basic | `GUILD_MEMBER_ADD`, `GUILD_MEMBER_UPDATE`, `GUILD_MEMBER_REMOVE` | Guild only, not QQ groups. |
| `PUBLIC_GUILD_MESSAGES (1<<30)` — basic | `AT_MESSAGE_CREATE`, `PUBLIC_MESSAGE_DELETE` | Public Guild receives @bot messages, not all channel traffic. |
| `GUILD_MESSAGES (1<<9)` — special/private bot | `MESSAGE_CREATE`, `MESSAGE_DELETE` | All Guild channel messages; private-domain bots only. |
| `GUILD_MESSAGE_REACTIONS (1<<10)` — special | `MESSAGE_REACTION_ADD`, `MESSAGE_REACTION_REMOVE` | Guild reactions. |
| `DIRECT_MESSAGE (1<<12)` — special | `DIRECT_MESSAGE_CREATE`, `DIRECT_MESSAGE_DELETE` | Guild-scoped DM. |
| `GROUP_AND_C2C_EVENT (1<<25)` — special | `C2C_MESSAGE_CREATE`, `GROUP_AT_MESSAGE_CREATE`, `FRIEND_ADD`, `FRIEND_DEL`, `C2C_MSG_REJECT`, `C2C_MSG_RECEIVE`, `GROUP_ADD_ROBOT`, `GROUP_DEL_ROBOT`, `GROUP_MSG_REJECT`, `GROUP_MSG_RECEIVE` | C2C and QQ-group bot lifecycle/messages. Reject/receive are user/admin notification-switch events, not read receipts. |
| `INTERACTION (1<<26)` — special | `INTERACTION_CREATE` | Button/config interactions; acknowledge via interaction endpoint. |
| `MESSAGE_AUDIT (1<<27)` — special | `MESSAGE_AUDIT_PASS`, `MESSAGE_AUDIT_REJECT` | Asynchronous message moderation outcome. |
| `FORUMS_EVENT (1<<28)` — special/private bot | thread create/update/delete; post create/delete; reply create/delete; publish audit result | Guild forum only. |
| `AUDIO_ACTION (1<<29)` — special | `AUDIO_START`, `AUDIO_FINISH`, `AUDIO_ON_MIC`, `AUDIO_OFF_MIC` | Guild audio state, not general QQ voice calls. |
| Transport meta | WebSocket `READY`, `RESUMED`, reconnect/heartbeat opcodes; Webhook validation/ACK opcodes | Transport lifecycle should not be delivered as user business events unless explicitly requested. |

**Delivery semantics:** WebSocket resume can replay events after the last stored sequence; Webhook/event retries can likewise require deduplication. Use event `id` plus platform/app scope as the primary ingress dedupe key; retain raw `t`, `s`, and payload. [S3]

## 8. Outbound operation inventory

| Operation | Official route/shape | Result/error semantics |
|---|---|---|
| Acquire token | `POST https://api.bot.qq.com/app/getAppAccessToken` | Returns token and `expires_in`; repeated valid-period fetch may return same token; 100001 is too frequent. [S2] |
| Send C2C message | `POST /v2/users/{openid}/messages` | Returns message `id`, timestamp and possible reference index; structured HTTP/business errors. Passive context uses `msg_id`/`event_id`. [S4] |
| Send QQ-group message | `POST /v2/groups/{group_openid}/messages` | Same broad result; group/scenario permission and reply rules apply. [S4] |
| Send Guild channel message | `POST /channels/{channel_id}/messages` | Guild-era endpoint; bot/API permissions and public/private scope apply. [S4] |
| Send Guild DM | `POST /dms/{guild_id}/messages` | Guild-DM context, not C2C. [S4] |
| Upload C2C/group media | `/v2/users/{openid}/files` or `/v2/groups/{group_openid}/files`; large file prepare/parts/complete variants | Returns `file_uuid`, `file_info`, TTL. Upload and send are separable; prepare can hit daily quota. [S4] |
| Send media | Message route with media/file info (`msg_type=7` in SDK) | Image/voice/video/file. Upload success does not imply message-send success; return both stages separately in adapter diagnostics. [S4] |
| C2C stream | `POST /v2/users/{openid}/stream_messages` | Replace-mode full Markdown content, generating/done state, same sequence and increasing index; C2C only, inbound message ≤5 min, recommended ≥300 ms interval. [S4] |
| Typing indicator | C2C message route with input-notify type | C2C only; duration field. Not a read receipt. [S4] |
| Acknowledge interaction | `PUT /interactions/{interaction_id}` | Result code 0–5; SDK says ACK within five seconds. [S4] |
| Recall C2C/group message | `DELETE .../messages/{message_id}` | No response body in SDK abstraction; time/ownership constraints are not consolidated publicly. [S4] |
| Guild roles/mute | `PUT/DELETE /guilds/.../roles/...`; `PATCH /guilds/.../mute` | Often HTTP 204; requires relevant API permission and bot Guild-admin status. [S6] |

**Active-send rule:** do not expose a generic `push()` as universally available. The official documentation repository says active push was withdrawn on 2025-04-21, while the 2026 SDK still contains proactive helpers and older notification-toggle events. Model `qq.active_message` as a runtime-discovered, normally-disabled capability and return a typed unsupported/policy error when absent. [S3][S4][S5]

## 9. Normalized common-capability candidates

These can safely enter a cross-platform contract, with capability discovery and target-kind constraints:

1. `BotIdentity { platform, appId, botId?, displayName? }` — never assume a human account or numeric QQ ID. [S2][S4]
2. `ConversationRef` variants: `direct`, `group`, `guild_channel`, `guild_dm`; keep QQ C2C and Guild DM distinct. [S3][S4]
3. `MessageCreated`, `MessageDeleted`, `MemberAdded/Updated/Removed`, and `InteractionCreated` events with platform raw payload and opaque IDs. [S3]
4. `SendText`, `Reply`, `SendMedia`, `DeleteOwnMessage`, `AcknowledgeInteraction`; each gated by conversation-scoped capabilities. [S4]
5. `MediaRef { kind, source, size?, filename?, expiresAt? }`; represent upload and send as separate serializable requests/results. [S4]
6. `Button`/`Interaction` as an optional feature with platform-specific permission and deadline metadata. [S4]
7. `Pagination { cursor?, limit? }` only on operations that advertise it; no implied message-history support.
8. `RateLimitInfo` and typed platform error (`httpStatus`, business code/message, retryAfter if observed), rather than static QPS constants. [S2][S4]
9. `IngressEnvelope { eventId, eventType, sequence?, appId, receivedAt, payload }` supporting deduplication and raw replay. [S3]

## 10. Required namespaced extensions

- `qq.c2c_stream`: replace-mode, full-content Markdown updates, `stream_msg_id`, `index`, `input_state`; not a general message edit API. [S4]
- `qq.guild_public_domain` / `qq.guild_private_domain`: determines @-only versus full channel event access. [S3]
- `qq.intent_bits` and intent grant metadata: retain exact bit and permission failure because the set is platform-specific. [S3]
- `qq.openid`, `qq.group_openid`, `qq.member_openid`: preserve scoped identity type and never coerce to consumer QQ number. [S4]
- `qq.message_audit_result`: platform moderation decision event. [S3]
- `qq.notification_switch_changed`: C2C/group `MSG_REJECT`/`MSG_RECEIVE`; not a generic notification/read model. [S3]
- `qq.markdown`, `qq.keyboard`, `qq.ark`, `qq.embed`: keep raw typed variants until a verified scenario/permission matrix supports normalization. [S4]
- `qq.guild_audio_action` and `qq.forum.*`: non-portable Guild semantics. [S3]
- `onebot11.*`: separate adapter namespace for CQ/segment encoding, numeric IDs, implementation status, action response (`status`, `retcode`, `data`, `echo`), and adapter-specific extensions. [S8][S9][S10]

## 11. Limits, policy, review, regional and lifecycle risks

- **Token:** nominal 7,200-second lifecycle; refresh server-side; token acquisition can return 100001 when too frequent. [S2]
- **Intents:** unauthorized special intents can fail authentication/close the WS connection. Grants may be removed. Subscribe minimally. [S3]
- **Gateway:** `/gateway/bot` returns `session_start_limit {total, remaining, reset_after, max_concurrency}`. This is dynamic. [S3]
- **Webhook:** HTTPS required; only ports 80, 443, 8080, 8443 accepted; validate signatures before enqueueing. [S3]
- **WebSocket lifecycle:** documented as phased out by end-2024/no longer maintained. A current SDK still implements it, but new OBCX work should prefer Webhook and keep WS behind a deprecated transport option. [S3][S4]
- **Passive replies:** five-minute evidence is strong for C2C streaming, but exact current ordinary-message windows and number of replies per trigger are not fully consolidated. Treat trigger expiry as server policy and expose expiry/rejection. [S4]
- **Active messages:** withdrawal note versus retained SDK helpers is a material conflict. Default to unsupported and allow only a live platform capability/grant to enable. [S5]
- **Media:** current SDK states image 30 MB, voice 20 MB, video/file 100 MB; large uploads use chunking and prepare has daily quotas. These are operational limits, not permanent common-contract constants. [S4]
- **Markdown/buttons:** Markdown is permissioned/reviewed; button/template acceptance and per-scenario behavior can change. A missing grant must produce `API_LIMITED`, not silent plaintext unless the caller explicitly allows degradation. [S4]
- **Sandbox:** sandbox API host and objects are isolated; only configured users/groups/Guilds trigger events and can be acted upon. Historic documentation says sandbox groups are limited to 20 members. [S7]
- **Review/publication:** complete configuration/development, submit review, then manually publish after approval. Production IP allowlisting is documented for new apps; absent a valid public egress IP, review/publication or API access can fail. [S7]
- **Developer qualification:** repository landing material historically distinguishes personal developers (Guild default) and enterprise access to group/C2C. Because the platform has evolved, current eligibility must be read from the authenticated console; do not encode this historical rule as timeless. [S7]
- **Region:** QQ and its Open Platform are primarily a mainland-China service, but no complete public country/residency eligibility matrix was located. Exact overseas availability, real-name/enterprise qualification and data residency are `UNKNOWN`; obtain contractual/platform-console confirmation before deployment.
- **Guild lifecycle:** QQ Channel/Guild APIs remain in docs and events, but consumer visibility and platform emphasis have changed. Keep Guild capabilities separately discoverable and test them against a real approved app.

## 12. Conflicts and unknowns

1. **Active push conflict:** repository documentation says withdrawn 2025-04-21; current official Node SDK retains `sendProactiveMessage` and describes active quota. Code-path existence is not entitlement. Resolution: live approved-app test plus console confirmation; default OBCX capability false. [S4][S5]
2. **WebSocket conflict:** official event page says phase-out/no maintenance; current official SDK defaults to WS while also supporting Webhook. Resolution: prefer Webhook and mark WS deprecated. [S3][S4]
3. **Current Guild availability:** official docs retain Guild APIs/intents, but some pages are older-generation documentation. Exact availability to newly created apps is `UNKNOWN` until console/app-grant verification.
4. **Exact text/Markdown/button payload limits and rate limits:** no one current official public matrix was found. Store adapter-configurable limits and use server responses.
5. **Ordinary passive reply count/window:** five minutes is explicit in current stream SDK guidance and older send docs; exact current C2C/group/channel counts are not confidently public.
6. **QQ-group administration:** no official current member list, kick, ban, role, or history API was verified. OneBot actions must not fill this evidence gap.
7. **Message get/history/edit/general forwarding:** no current official C2C/group support verified.
8. **Cards:** Ark/Embed/raw payload fields are present in official SDK code, but public permission and scenario matrices are incomplete.
9. **Regional/compliance/price:** exact eligibility, data-location commitments, review SLAs and commercial terms are not public enough for a stable contract.
10. **Product-only details:** read receipts, reactions outside Guilds, stickers, polls, feeds and calling behavior vary by consumer client/version and were not exhaustively audited because they do not establish Bot APIs.

## 13. OBCX design implications

1. **No giant `IBot`.** Publish capability descriptors per app and conversation kind: `canReply`, `canSendActive`, media kinds/max observed size, `canStreamReplace`, `canUseMarkdown`, `canUseKeyboard`, `canRecall`, granted intents, and moderation scope. This follows the scenario and grant fragmentation above. [S3][S4][S5]
2. **Typed opaque identities.** Use distinct DTOs for `QqUserOpenId`, `QqGroupOpenId`, `QqMemberOpenId`, `QqGuildId`, and `QqChannelId`; never accept a raw `long` as a universal QQ identity. [S3][S4]
3. **Conversation algebra, not strings:** `QqC2C`, `QqGroup`, `QqGuildChannel`, `QqGuildDm`. This prevents accidental use of `/v2/users` for Guild DM or channel APIs for QQ groups. [S4]
4. **Reply context DTO:** `{ triggerMessageId?, triggerEventId?, receivedAt, expiresAt?, msgSeq? }`. The egress actor validates known expiry and lets platform errors remain typed. Do not overload quote/reference with passive-trigger correlation. [S4]
5. **Process-owned transport.** A webhook ingress process validates signature, deduplicates `event.id`, persists raw envelope, and sends serializable typed events to business actors. Business actors never own HTTP servers, token refresh, heartbeats or Gateway resume state. [S2][S3]
6. **Outbound request/result messages:** e.g. `SendQqMessageRequest` → `SendQqMessageResult | QqPolicyDenied | QqGrantMissing | QqRateLimited | QqApiError`. Include platform message ID, timestamp, reference index, audit/pending state, and raw response. [S3][S4]
7. **Two-stage media workflow:** `UploadQqMedia` returns expiring `file_info`; `SendQqUploadedMedia` consumes it. A convenience orchestration can compose them but must report which stage failed. [S4]
8. **Streaming is a namespaced session actor:** serialize updates per C2C target, throttle ≥300 ms, send full accumulated text, enforce five-minute trigger context, and finish with DONE. Do not implement as common `EditMessage`. [S4]
9. **Permission-aware ingress:** configure only granted intents and expose grant discovery/configuration. Unauthorized intents are a startup failure, not an empty event stream. [S3]
10. **Interaction deadline:** acknowledge button events independently of slow business work when safe, within the documented five-second budget; then process asynchronously. [S4]
11. **Explicit degradation policy:** Markdown→plain text and keyboard removal may be `EMULATED` only when the caller opted in and no required interaction semantics are lost. Otherwise return missing-capability. [S4]
12. **Guild moderation as optional sub-capability:** expose Guild member/role/mute operations only when the bot is Guild admin and permission is granted. Never map those methods onto QQ groups. [S6]
13. **OneBot adapter quarantine:** implement `OneBot11Adapter` behind its own capability discovery. Translate only semantically safe text/image/reply primitives; retain CQ/raw segments and unsupported moderation as `onebot11.*`. Never reuse OneBot numeric IDs in the official adapter. [S8][S9][S10]
14. **Dynamic limits:** maintain observed retry/reset metadata and configurable platform hints. Do not compile 30/20/100 MB or session-start counts into common interfaces. [S2][S3][S4]
15. **Webhook-first lifecycle:** make Webhook the production default; legacy WS can be a separately enabled connector with session persistence and explicit deprecation telemetry. [S3][S4]

## 14. Claim-to-source checklist

| Claim / conclusion | Sources |
|---|---|
| Consumer QQ features do not imply API features | S1, S11 |
| Official app identity and token auth | S2, S4 |
| C2C/group/Guild/channel/DM distinctions | S3, S4 |
| Opaque OpenID identity model | S4 |
| Passive context, streaming window and active-message conflict | S4, S5 |
| Text/media/streaming capabilities and limits | S4 |
| Markdown/button permissions and interaction events | S3, S4 |
| Webhook-first; WS lifecycle warning | S3, S4 |
| Intent grants and event inventory | S3 |
| Guild moderation but no proved QQ-group parity | S3, S6, contrasted with S9 |
| Sandbox/review/IP allowlist risk | S7 |
| Dynamic quota handling | S2, S3, S4 |
| OneBot 11 is unofficial/adapter-only | S8, S9, S10 |
| Typed IDs/conversations and capability discovery | S2–S6 |
| Process-owned ingress/egress and idempotency | S2, S3, S4 |
| Namespaced streaming/Guild/OneBot extensions | S3, S4, S8–S10 |

---

### OneBot 11 adapter-boundary appendix (not official QQ API evidence)

OneBot 11 specifies HTTP API, HTTP event POST, forward WebSocket, and reverse WebSocket transports. Actions return an envelope such as `status`, `retcode`, `data`, and optional correlation `echo`. Its public action set includes private/group send, delete/get message, merged-forward lookup, friend/group/member lookup, request handling, group kick/ban/admin/card/name/title operations, media retrieval, and implementation status. Events are divided into `message`, `notice`, `request`, and `meta_event`; messages may be CQ-code strings or arrays of typed segments such as text, face, image, record, video, @, reply, forward/node, XML and JSON. [S8][S9][S10]

For OBCX, this is useful as a **compatibility adapter contract only**. Every OneBot implementation may implement only a subset or add extensions, and many implementations automate a consumer account rather than use Tencent's official Bot API. Therefore `get_friend_list`, group moderation, arbitrary private sends, numeric QQ IDs, forwards, XML/JSON, status, cookies/CSRF, and similar OneBot capabilities must remain implementation-discovered `onebot11.*` features and must never be advertised by the official QQ adapter without independent Tencent documentation.
