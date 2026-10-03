# WeCom / WeChat Work (企业微信) capability audit

## 1. Lane metadata

- **Platform:** WeCom / WeChat Work (企业微信), not consumer WeChat, WeChat Official Accounts, or the consumer WeChat Bot ecosystem.
- **Surfaces examined:** (A) tenant-owned internal/self-built applications and their application-chat API; (B) classic group-robot/message-push Webhooks; (C) newer intelligent robot API mode; (D) Customer Contact (客户联系), including customer groups; (E) WeChat Customer Service (微信客服); and (F) Conversation Content Archive (会话内容存档).
- **Research date / access date:** 2026-08-17 UTC.
- **Freshness caveat:** WeCom documentation is a dynamic Chinese-language site. Several developer URLs resolve by current navigation state and are poorly indexed. Direct official pages were fetched where possible; mirrored official text is explicitly marked `SECONDARY`. Limits and entitlement rules can change by tenant certification, size, edition, administrator configuration, and client version. Revalidate before implementation.
- **Status vocabulary:** `NATIVE`, `API_LIMITED`, `EMULATED`, `EXTENSION`, `UNSUPPORTED`, and `UNKNOWN` have the meanings in the lane brief.

## 2. Executive findings

1. **There is no single WeCom “bot API.”** Internal apps, application-created internal chats, classic group Webhooks, intelligent robots, Customer Contact, Customer Service, and archive access have different identities, credentials, address spaces, message schemas, ingress modes, and limits. They must be separate adapter capabilities. [S1][S3][S8][S10][S12]
2. **An internal app is tenant- and application-scoped.** Calls use a server-side token derived from the enterprise `CorpID` and the relevant application/system `Secret`; messages carry `agentid`, and recipient/contact visibility is administrator-controlled. A token or user ID from one app/tenant must not be treated as global. [S1][S2][S6]
3. **Classic group robots are outbound-only fixed Webhooks, not chat listeners.** A secret URL posts into one internal group. Official documentation defines send/upload operations but no inbound member-message callback. It supports text, Markdown variants, image, news, file, voice, and two display-card forms, with 20 sends/minute per Webhook. [S3]
4. **The newer intelligent-robot API is a separate interaction surface.** It receives user interactions by encrypted callback and may return/use a one-use, one-hour `response_url`; its reply repertoire and callback semantics are not those of classic group Webhooks or internal apps. [S4][S5]
5. **Ordinary internal applications can push rich application messages but cannot read general chat history.** Direct recipients are members/departments/tags in app visibility. An application can also create/manage its own special internal `appchat` only if self-built and root-department-visible; it cannot target arbitrary existing user-created chats. [S2][S7]
6. **Customer Contact is CRM/contact-management, not a general external-chat bot.** APIs expose external-contact/customer-group metadata, follow-user relationships, welcome messages, and member-mediated mass-send tasks/events. Creating a mass-send task does not mean the application directly sent into a customer chat. [S8][S9]
7. **WeChat Customer Service is the official live customer-message surface.** It has its own Secret/account IDs and cursor-based message sync plus send API; API sends are constrained by service state and, after a customer message, a 48-hour/5-message opportunity. It must not be conflated with Customer Contact customer groups. [S10]
8. **General history/audit is a separately enabled compliance product.** Conversation Content Archive uses a native SDK, archive-specific Secret and RSA keys, consent/notice rules, incremental `seq`, encryption, a five-day retrieval horizon, up to 1,000 records/pull, and up to 4,000 pulls/minute. It is not ordinary message-get. [S11][S12]
9. **Callbacks are encrypted, retried, at-least-once ingress.** The callback endpoint is configured with URL, Token and `EncodingAESKey`; normal callbacks should be acknowledged within five seconds and may be retried three times. Deduplicate message callbacks by `MsgId` and event callbacks by a stable composite/raw identity. [S13]
10. **Message mutation is narrow.** Internal application messages can be recalled within 24 hours using returned `msgid`; selected interactive template cards can be updated using a short-lived/single-use response code. There is no general edit/delete API, and customer/group-robot messages do not inherit these semantics. [S14][S15]
11. **Contact administration is permission-model-specific.** Ordinary app contact reads are visibility-scoped; directory synchronization/edit authorization is a privileged, distinct mode. We found no general API for arbitrary custom tenant roles or presence/status. [S6]
12. **Rate limiting cannot be one global constant.** Limits differ by operation and surface, and some delivery protection silently drops messages even after a successful call. Adapters need per-credential/per-tenant buckets, retry classification, and delivery status distinct from request acceptance. [S3][S7][S10][S12][S16]

## 3. Research action log

### Searches and entry points

1. Searched official developer-domain results for internal-app identity/token, application-message send, callback encryption, contact visibility, application chat, and active-call limits.
2. Searched classic group robot/Webhook message types, upload limits, cards, mentions, and rate limits; fetched the live official “消息推送配置说明” page.
3. Searched Customer Contact and customer-group list/detail, events, welcome-message and mass-send semantics separately from WeChat Customer Service receive/send APIs.
4. Searched Conversation Content Archive setup, consent, SDK pull/decrypt/media operations, paging, horizon, and rates; fetched the live official SDK page.
5. Searched the newer intelligent-robot API and fetched the official active-response page; checked Tencent’s official `WecomTeam/aibot-node-sdk` repository for identity and callback corroboration.
6. Searched application-message/card update and recall behavior and contact/directory privilege changes.

### Pages checked but difficult/inaccessible

- The official developer center is heavily client-rendered and search indexing is incomplete. Some direct `/document/path/...` URLs exposed readable content, while others changed title/navigation or were inaccessible through search. Where the live page could not be reliably extracted, this report cites the direct official entry point plus a clearly marked mirror of the official text.
- No current official page was found that exposes a stable, comprehensive machine-readable OpenAPI schema or a single authoritative capability/limit matrix.
- No evidence was accepted from unofficial consumer-WeChat automation, reverse-engineered desktop clients, or personal-account bots.

## 4. Source register

All sources accessed 2026-08-17.

| ID | Authority | Title / relevant section | URL | Evidence / precise paraphrase |
|---|---|---|---|---|
| S1 | OFFICIAL | 获取 access_token / server API credential entry | https://developer.work.weixin.qq.com/document/path/91039 | Token is obtained for a CorpID plus the relevant Secret and is server-side, expiring/cached credential material. |
| S2 | SECONDARY | Apifox mirror, “发送应用消息” and “消息类型” | https://s.apifox.cn/apidoc/docs-site/406014/api-10061693 ; https://s.apifox.cn/apidoc/docs-site/406014/doc-1776833 | `/cgi-bin/message/send`, `agentid`, visible-scope recipients; supported app message families, recipient counts, duplicate-check fields. Mirror used because official page extraction was unstable. |
| S3 | OFFICIAL | 消息推送配置说明 — message types, rate, upload | https://developer.work.weixin.qq.com/document/path/91770 | Fixed Webhook POST; eight listed types; text 2,048 bytes, Markdown 4,096; image 2 MiB; file 20 MiB; media ID three days; 20 messages/minute. The page describes outbound send/upload only. |
| S4 | OFFICIAL | 智能机器人：主动回复消息 | https://developer.work.weixin.qq.com/document/path/101138 | Interaction callback supplies `response_url`; one call, one-hour validity; Markdown up to 20,480 bytes and template-card reply. |
| S5 | OFFICIAL | WecomTeam `aibot-node-sdk` | https://github.com/WecomTeam/aibot-node-sdk | Vendor-published SDK demonstrates intelligent-robot callback verification/decryption and API-mode integration as a distinct surface. |
| S6 | SECONDARY | Mirror, 通讯录概述 / 权限体系 | https://s.apifox.cn/apidoc/docs-site/406014/doc-417869 ; https://wdk-docs.github.io/wework-docs/server/basic/application-authorization/address-book-authority-system/ | Ordinary app reads are visibility-scoped; synchronization/edit authorization is privileged and separate; post-2022 privacy/field restrictions apply. |
| S7 | OFFICIAL | 应用群聊 `appchat/send` — interface, permission, limits, types | https://developer.work.weixin.qq.com/document/path/90248 | Only self-built apps whose visibility is root department; chat must have been created by that app; enterprise and per-member throughput protections; listed appchat message types. |
| S8 | SECONDARY | Mirror, 客户联系概述 | https://www.apifox.cn/apidoc/docs-site/406014/doc-417781 | Customer Contact authorization, external contacts, follow users, customer groups, strategies and mass-send API families. |
| S9 | SECONDARY | Mirror, 创建企业群发 | https://apifox.com/apidoc/docs-site/406014/api-10061297 | API creates a mass-send task and notifies an employee; employee confirmation is required; app visible-scope and customer frequency restrictions apply. |
| S10 | SECONDARY | Mirror, 微信客服“发送消息” | https://apifox.com/apidoc/docs-site/406014/api-10061328 | Customer Service uses `/cgi-bin/kf/send_msg`, `open_kfid` and external customer ID; allowed message types and service-state plus 48-hour/5-send restriction. |
| S11 | SECONDARY | Mirror, 会话内容存档“使用前帮助” | https://qiyeweixin.apifox.cn/doc-417831 | Archive must be enabled/configured; covered members and external-party consent/notice govern availability; not a normal app-history API. |
| S12 | OFFICIAL | 会话内容存档 SDK — overall flow and `GetChatData` | https://developer.work.weixin.qq.com/document/path/91774 | Archive-specific CorpID/Secret, SDK pull/decrypt/media, RSA envelope, `seq`/`msgid`, max 1,000/pull, 4,000/minute and five-day retrieval horizon. |
| S13 | SECONDARY | Mirror, 接收消息与事件概述 / 回调配置 | https://wdk-docs.github.io/wework-docs/server/basic/message-push/receive-messages-and-events/ ; https://wdk-docs.github.io/wework-docs/server/dev-guide/callback-setting/ | URL/Token/AES validation, encrypted callback, five-second acknowledgement, three retries, and dedupe guidance. |
| S14 | SECONDARY | Mirror, 撤回应用消息 | https://s.apifox.cn/apidoc/docs-site/406014/api-10061695 | `/cgi-bin/message/recall` accepts app-send `msgid`; only app messages sent within 24 hours and supported WeCom clients. |
| S15 | SECONDARY | Mirror, 更新模板卡片消息 | https://s.apifox.cn/apidoc/docs-site/406014/api-10061694 | Selected application cards update with `agentid` plus one-use `response_code`; documentation has a 24h/72h conflict. |
| S16 | SECONDARY | Mirror, 访问频率限制 | https://wdk-docs.github.io/wework-docs/appendix/access-frequency-restriction/ | Active APIs have family/endpoint limits; rate error commonly `45009`; cache tokens and apply endpoint-aware throttling. |
| S17 | SECONDARY | Mirror, 客户联系事件格式 | https://qiyeweixin.apifox.cn/doc-417790 | Add/delete external contact, transfer, customer-group and related lifecycle events; API-originated changes do not necessarily generate callbacks. |
| S18 | SECONDARY | Mirror, 微信客服读取消息/events | https://s.apifox.cn/apidoc/docs-site/406014/api-10061327 | Cursor/token-based sync obtains Customer Service messages and events such as entering session; this is not a push of arbitrary customer-group history. |

## 5. Product vs official API boundary

| Surface | Product behavior | Official automation boundary |
|---|---|---|
| Internal conversations | Members can DM and create ordinary internal groups in clients. | Internal apps can push to visible members/departments/tags and receive messages sent to the app. They cannot enumerate/read arbitrary ordinary chats. Special `appchat` is app-created only and requires root-department visibility. [S2][S7] |
| Classic group robot/message push | An administrator/member adds a message-push robot to an internal group. | Secret Webhook supports outbound send and media upload only. No official group-message ingress, history, membership or moderation API is defined for this surface. [S3] |
| Intelligent robot | Users interact with an API-mode robot in supported chats. | Encrypted callbacks plus immediate/active reply; active `response_url` is single-use for one hour. Do not apply classic Webhook semantics. [S4][S5] |
| Customer Contact | Employees add/follow external customers and operate customer groups in the client. | Metadata, lifecycle, tags/strategies, welcome messages and member-mediated mass-send tasks. It is not an unrestricted send/read bot for external chats. [S8][S9][S17] |
| WeChat Customer Service | Customers initiate support sessions from official entry points; agents/assistant serve them. | Separate Customer Service account/Secret, cursor-based receive/sync and constrained send operation. [S10][S18] |
| Conversation Content Archive | Tenant compliance feature records covered conversations, subject to notice/consent. | Separate SDK/Secret/key pipeline; encrypted incremental retrieval only for entitled/covered data. [S11][S12] |
| Contacts/admin | Admin console manages org, app visibility, admins and feature scopes. | Contact APIs reflect the calling credential’s visibility and special directory authorization. Product admin roles are not a generic API role graph. [S6] |
| Meetings/voice/video | Native product has meetings and calls. | No evidence that the messaging surfaces provide general call control/live-media bot APIs; archive can expose archived call artifacts only when covered. [S12] |

## 6. Capability evidence table

Abbreviations: **IA** internal app; **AC** application-created `appchat`; **GR** classic group robot; **IR** intelligent robot; **CC** Customer Contact; **KF** WeChat Customer Service; **AR** archive.

| Capability | Product support | Official API status | Restrictions / surface boundary | Evidence | Confidence |
|---|---|---|---|---|---|
| Bot/app identity | Yes | **NATIVE (IA/IR); EXTENSION (GR/CC/KF)** | IA: CorpID + AgentId/application Secret. GR identity is possession of group-bound URL key. IR has robot identity. CC/KF use feature-specific IDs/secrets. | S1–S5, S8, S10 | HIGH |
| Authentication | Yes | **API_LIMITED** | Server tokens must be cached and never exposed client-side; callback Token/AES key differs from API token; GR/IR response URLs are bearer secrets. Trusted-IP/admin setup may apply. | S1, S3, S4, S13, S16 | HIGH |
| Tenant model / multi-account | Multi-enterprise client supported | **NATIVE** | Every credential and identifier must be keyed by tenant and app/surface. One runtime may hold many tenant installations, but IDs/tokens are not portable. | S1, S6, S8 | HIGH |
| App/bot status/presence | Product shows app availability | **UNSUPPORTED** | No general official set/get presence or bot online-status API found. | S1–S6 | MEDIUM |
| Member profile/get/list | Yes | **API_LIMITED** | Visible-scope and privacy/authorization restrictions; directory synchronization has distinct privilege. Pagination/department scoping applies. | S6 | HIGH |
| Departments | Yes | **API_LIMITED** | List/read in authorized scope; create/update/delete only with suitable directory-sync/edit authorization, not ordinary app authority. | S6 | HIGH |
| Tags | Yes | **API_LIMITED** | Tenant contact tags exist; access and modification depend on calling app, creator and visible scope. Customer tags are a different CC namespace. | S6, S8 | MEDIUM |
| Friends/follow/social graph | CC has employee-customer follow relation | **EXTENSION** | No consumer-WeChat friend graph. CC exposes follow users/external contacts; model as `wecom.customer_contact.follow`. | S8 | HIGH |
| Internal DM / push to user | Yes | **NATIVE** | IA push is application message, not impersonated member chat. Recipient must be in app scope. Incoming is user-to-app callback, not arbitrary DM snooping. | S2, S13 | HIGH |
| Ordinary internal group | Yes | **UNSUPPORTED** for general automation | IA/GR cannot enumerate or read arbitrary ordinary groups; GR only posts to its configured group. | S3, S7 | HIGH |
| Application-created group (`appchat`) | Specialized product surface | **API_LIMITED** | Self-built app only, root-department visibility, and only chats created by that app. Create/get/update/send available. | S7 | HIGH |
| Customer group | Yes | **API_LIMITED / EXTENSION** | CC list/detail/member metadata and lifecycle; no arbitrary live message read/send. Mass-send is employee-mediated. | S8, S9, S17 | HIGH |
| Threads/topics/channels/server/guild | No aligned primitive found | **UNSUPPORTED** | WeCom group chats are not channel/thread hierarchies. Do not synthesize thread IDs. | S2, S3, S7 | HIGH |
| Create message | Yes | **NATIVE** IA/GR; **API_LIMITED** AC/IR/KF; **EXTENSION** CC mass-send | Each surface has different recipient and payload semantics; KF has opportunity window; CC task requires member action. | S2–S4, S7, S9, S10 | HIGH |
| Get one message | Client can view | **UNSUPPORTED** ordinary; **API_LIMITED** AR/KF sync | No ordinary app `get message`. AR retrieves covered records; KF sync retrieves service messages. | S12, S18 | HIGH |
| History | Client has history | **API_LIMITED** only AR/KF | AR is separately enabled compliance SDK; KF is service-session sync. Neither grants general app history. | S11, S12, S18 | HIGH |
| Edit message | Some client behavior/cards | **API_LIMITED** | No general edit. Selected IA interactive cards update through response code. | S15 | HIGH |
| Delete/recall | Product recall exists | **API_LIMITED** IA | IA can recall eligible API-sent messages within 24h; not a general delete and not inherited by GR/CC/KF. AR records recall action. | S12, S14 | HIGH |
| Reply/quote/forward | Product supports them | **API_LIMITED / EMULATED** | IR reply is interaction-bound; IA can passive/active respond to app messages but no universal quote/forward operation. AR may represent quoted/forwarded records as archive data. | S4, S12, S13 | MEDIUM |
| Text/rich text/Markdown | Yes | **NATIVE with dialect limits** | Byte limits and supported Markdown differ: GR text 2,048, Markdown 4,096; IR active Markdown 20,480; IA/appchat use their own subset/limits. | S2–S4, S7 | HIGH |
| Mentions | Yes | **API_LIMITED** | GR text/Markdown and AC text support user IDs/`@all`; Markdown v2 does not mention. Mention does not imply contact discovery permission. | S3, S7 | HIGH |
| Reactions | Yes in clients | **UNSUPPORTED** | No ordinary messaging API evidence for create/list reaction. IR feedback event is not a general emoji reaction. | S4 | MEDIUM |
| Stickers/emojis | Yes | **UNSUPPORTED** send; **API_LIMITED** archive observation | AR has emotion records when covered. No general IA/GR sticker-send contract found. | S12 | HIGH |
| Polls/forms | Product supports | **API_LIMITED / EXTENSION** | IA interactive template cards include vote/multi-select; AR can observe native vote/collect types. No portable “poll object” CRUD. | S2, S12, S15 | HIGH |
| Cards/buttons | Yes | **API_LIMITED** | IA has several interactive/display templates and callback updates; GR only text/news display cards; IR template-card interaction/reply. Schemas are surface-specific. | S2–S4, S15 | HIGH |
| Images | Yes | **NATIVE / API_LIMITED** | IA/AC/KF use uploaded media IDs; GR allows base64+MD5 up to 2 MiB; AR media is SDK-file retrieval. | S2, S3, S7, S10, S12 | HIGH |
| Audio/voice | Yes | **API_LIMITED** | GR voice upload: AMR, ≤2 MiB, ≤60s; other surfaces use their own media upload/type rules. | S2, S3, S7, S10 | HIGH |
| Video | Yes | **API_LIMITED** | IA/AC/KF support video; classic GR’s current listed set has no video message type. | S2, S3, S7, S10 | HIGH |
| Files | Yes | **NATIVE / API_LIMITED** | GR upload >5 bytes, ≤20 MiB, media ID valid three days and bound to uploader Webhook; IA/KF media credentials differ. | S2, S3, S7, S10 | HIGH |
| Media groups/albums | Product may group media | **UNSUPPORTED / UNKNOWN** | No common multi-media-group send primitive established. Use multiple sends only as explicit emulation, losing atomicity. | S2, S3, S10 | MEDIUM |
| Upload/download | Yes | **API_LIMITED** | Upload APIs and media IDs are credential/surface scoped and temporary. Download exists for received callback media, KF sync and AR SDK, not arbitrary product media. | S2, S3, S10, S12, S18 | HIGH |
| Group members | Yes | **API_LIMITED** AC/CC/AR | AC get/update owns its appchat; CC gives customer-group detail; AR can expose covered room data. GR has no member-list API. | S7, S8, S12 | HIGH |
| Roles/permissions | Yes in admin UI | **API_LIMITED / UNSUPPORTED generic** | App visible scope, secrets, special directory authorization, CC/KF scopes and archive entitlement are native. No arbitrary role CRUD/general permission graph found. | S6, S8, S10–S12 | HIGH |
| Moderation | Product admins moderate | **UNSUPPORTED generic** | No ordinary app/GR cross-chat moderation API found. AC owner/member update is not tenant-wide moderation. | S7 | MEDIUM |
| Audit/compliance | Yes, separately enabled | **API_LIMITED** | AR entitlement, covered users, consent, IP/key setup and SDK required; data must be pulled within five days. | S11, S12 | HIGH |
| Commands/interactions | Menus/cards/IR interactions | **API_LIMITED** | Not slash commands. IA menu/card events and IR callbacks are typed platform interactions. | S4, S13, S15 | HIGH |
| Incoming Webhooks/events | Yes | **NATIVE / API_LIMITED** | IA/CC callbacks encrypted; IR callbacks distinct; GR has no inbound callback. KF uses token/cursor sync after notification. | S3–S5, S13, S17, S18 | HIGH |
| Outgoing Webhooks | GR | **NATIVE** | Fixed group-bound secret URL, outbound into WeCom from external system; terminology is opposite some platforms’ “incoming webhook.” | S3 | HIGH |
| Presence/typing | Product UI may show states | **UNSUPPORTED** | No official bot/API capability found. | — | MEDIUM |
| Read receipts | Product has message read state | **API_LIMITED** | Application-message read-member query exists only in specific app-message scenarios/versions; no universal per-message receipt. Exact current entitlement/limits are `UNKNOWN`; do not normalize yet. | S2 | LOW |
| Feed/post/repost/follow/notification | Product has Workbench/Moments-related functions | **EXTENSION / UNSUPPORTED generic** | IA application notification is message push; CC Moments APIs, where authorized, are campaign/CRM features, not a social feed contract. | S2, S8 | MEDIUM |
| Voice/video/live/space control | Product supports meetings/calls/live | **UNSUPPORTED generic** | Messaging APIs do not establish portable call/live control. AR may archive call artifacts; observation is not control. | S12 | MEDIUM |
| Encryption | Yes | **NATIVE** | HTTPS for active calls; callback signature plus AES; AR RSA envelope plus SDK decryption. No claim of end-to-end encryption to the integrating application. | S12, S13 | HIGH |
| Federation/external identity | Inter-enterprise and WeChat interop exist | **EXTENSION** | Internal `userid`, external `external_userid`, customer IDs and robot IDs have distinct namespaces; consumer-WeChat interoperability does not make this consumer-WeChat API. | S8, S12 | HIGH |
| Idempotency | Partial | **API_LIMITED** | Callback `MsgId`/AR `msgid` permit dedupe; IA can enable duplicate checking; KF accepts optional client `msgid`. Not every send has an idempotency key. | S2, S10, S12, S13 | HIGH |
| Pagination | Yes | **NATIVE / API_LIMITED** | Contact/CC use cursor/list paging; KF uses sync token/cursor; AR uses monotonic `seq`. Preserve mechanism-specific continuation type. | S8, S12, S18 | HIGH |
| Rate limits | N/A | **API_LIMITED** | GR 20/min; AC has enterprise/person-time protections including silent drops; AR 4,000 pulls/min; KF and other APIs have endpoint-specific limits/error codes. | S3, S7, S10, S12, S16 | HIGH |
| Passive-response window | N/A | **API_LIMITED** | Normal callbacks: respond within 5s or acknowledge and handle async; up to three retries. IR `response_url`: one use/1h. KF: 48h and five sends per customer opportunity. These are not interchangeable. | S4, S10, S13 | HIGH |
| Payload/message limits | N/A | **API_LIMITED** | Enforce per type/surface, in bytes where documented. Do not reuse GR limits for IA/IR/KF. | S2–S4, S7, S10 | HIGH |

## 7. Inbound event inventory

### 7.1 Internal app callback — encrypted HTTP POST

Official callback families evidenced by the callback/message documents include:

- **Messages sent to the application:** text, image, voice, video, location, link and event payloads; payload availability varies by application configuration. This is not arbitrary employee-chat capture. [S13]
- **Application/UI events:** enter application, menu click/view/scan/location-related events, location reporting, and interactive template-card submissions/menu actions. [S13][S15]
- **Contact-change events:** member create/update/delete, department create/update/delete, tag changes where the correct contact callback privilege is configured. Directory field visibility follows authorization. [S6][S13]
- **Application lifecycle/authorization events:** suite/provider/authorized-installation events exist for third-party-provider models, but this lane does not normalize them as internal-app chat events. Treat as `wecom.installation.*` extension.

**Delivery:** URL verification by GET; encrypted XML POST signed with configured Token and encrypted with `EncodingAESKey`. Normal callback delivery should receive HTTP 200 within five seconds; otherwise WeCom may retry three times. Message `MsgId` and a stable event composite/raw hash are required for dedupe. [S13]

### 7.2 Classic group robot

- **None.** The classic group Webhook documents external-system-to-group send and upload only; it does not deliver member messages, joins, reactions, or card clicks to the caller. [S3]

### 7.3 Intelligent robot API mode

- User message/interactions with robot; template-card button/menu/input events; optional user-feedback events when a feedback ID is attached. Callback includes an interaction-bound response mechanism. [S4][S5]
- Delivery uses the robot callback verification/decryption mechanism. `response_url` is a bearer capability: one successful use within one hour. Preserve raw event type and robot/chat IDs. [S4]

### 7.4 Customer Contact

Events include external-contact add/change/delete, customer deleting employee, follow-user handoff/transfer outcomes, customer-tag/contact changes, customer-group create/update/dismiss and member join/leave-type lifecycle events, subject to feature authorization and configured callback receiver. [S17]

These are **relationship/group lifecycle events**, not the text/media stream of a customer conversation. API-caused changes do not universally echo as callbacks, so adapters must not rely on write-followed-by-event consistency. [S17]

### 7.5 WeChat Customer Service

The service message-sync API returns customer messages plus service events, including entering a session and service-state transitions; notification/callback tells the integration to sync, while cursor/token-based reads obtain records. Preserve `open_kfid`, customer external ID, event/message token and cursor. [S18]

### 7.6 Conversation Content Archive

Not a Webhook. An entitled tenant periodically pulls encrypted records by `seq` through the SDK. Records can represent sends, recalls, consent/disagreement, messages, room IDs, call artifacts and many product-native types. `msgid` is the dedupe key; media requires separate SDK retrieval. [S12]

## 8. Outbound operation inventory

| Surface | Operations | Result / error / async semantics |
|---|---|---|
| IA application message | Upload temporary media; send to users/departments/tags; recall eligible message; update selected card | JSON `errcode/errmsg`; send may return `msgid`, invalid-recipient lists and card response code. Acceptance is not universal read/delivery. Recall is ≤24h; card response code is one-use/short-lived. [S2][S14][S15] |
| AC `appchat` | Create, get, update members/name/owner, send | Only app-created chat; root-visible self-built app. `appchat/send` success may still be followed by silent per-member protection drops; model accepted vs delivered separately. [S7] |
| GR | POST Webhook message; upload file/voice | `errcode/errmsg`; fixed group destination. 20 sends/min/Webhook. Uploaded media ID is Webhook-bound and valid three days. No message ID/history/recall guarantee documented. [S3] |
| IR | Immediate callback reply and/or POST to one-use `response_url`; reply Markdown/template card | Interaction-bound. URL expires in one hour and can be used once. Store as secret, never as durable channel credential. [S4] |
| CC | Manage external-contact/customer-group metadata as authorized; send welcome message using event code; create/remind/query member mass-send task | Welcome code is event/window scoped. Mass-send creation is asynchronous/member-mediated; status query reports employee/customer outcomes. It is not direct bot send. [S8][S9] |
| KF | Sync messages/events; send text/image/voice/video/file/news/miniprogram/menu/location as currently documented; manage service state/account | `errcode/errmsg`; optional caller `msgid` aids idempotency. Sends require allowed service state and available customer opportunity; after customer message, 48h and max five sends. [S10][S18] |
| AR | Initialize SDK, pull encrypted records, decrypt, retrieve media | Pull returns cursor-like `seq` and encrypted envelopes. Up to 1,000 records/call; pull within five days; 4,000 calls/min. This is read/audit, never send. [S12] |

## 9. Normalized common-capability candidates

The following can safely generalize to other enterprise IM adapters, provided capability discovery includes restrictions:

1. **Tenant-scoped installation identity:** `{platform, tenant_id, installation_id, app_id?, bot_id?, credential_ref}`. Never expose Secret/token in DTOs. WeCom’s CorpID/AgentId and feature-specific identities prove the need for both tenant and installation. [S1][S8][S10]
2. **Targeted outbound message:** target union `{user, owned_room, webhook_room, customer_session}` plus content union `{text, image, audio, video, file, link, card}`. Availability is per surface. [S2][S3][S7][S10]
3. **Media handle:** `{platform, tenant, credential_scope, media_id, media_type, expires_at?}`. WeCom’s Webhook-bound three-day media ID demonstrates that IDs cannot be globally reused. [S3]
4. **Encrypted at-least-once callback envelope:** `{installation, event_id?, event_type, occurred_at, raw, decryption_context_ref}` with immediate process-owned verification/decryption and dedupe. [S13]
5. **Paged collection result:** opaque continuation union `{cursor, sync_token, sequence}` rather than forcing one string semantic. CC/KF/AR differ. [S8][S12][S18]
6. **Send result:** `{accepted, platform_message_id?, invalid_targets[], warnings[], raw}`. Do not equate `errcode=0` to display/read; AC protection may silently drop. [S2][S7]
7. **Rate-limit descriptor:** scope (`credential`, `webhook`, `tenant`, `recipient`), window, unit (`requests`, `messages`, `recipient-deliveries`), and failure behavior (`error`, `silent_drop`). [S3][S7][S12]
8. **Constrained interaction reply:** `{interaction_id, reply_deadline, max_uses, supported_content}` generalizes to Slack/Discord-like interaction callbacks while retaining WeCom values. [S4]
9. **Compliance record stream:** optional separate capability yielding immutable/audit-oriented records with consent/coverage metadata. It must not implement ordinary `message.history`. [S11][S12]

## 10. Required namespaced extensions

- `wecom.app.visible_scope`, `agent_id`, `to_party`, `to_tag`, and duplicate-check controls: recipient semantics are directory/app-specific. [S2][S6]
- `wecom.appchat.*`: root-department visibility, app ownership of chat, person-time throughput, and confidentiality (`safe`) are non-portable. [S7]
- `wecom.group_webhook.*`: group-bound key, Markdown dialect, mobile-number mentions, Webhook-bound media and fixed 20/min limit. [S3]
- `wecom.aibot.response_url`: single-use one-hour bearer URL, feedback ID and WeCom card schemas. [S4]
- `wecom.template_card.*`: card types, task/response codes, update operations and callback field layouts. A generic card may cover display-only fallback, not update semantics. [S2][S15]
- `wecom.external_contact.*`: external user/follow-user relationship, customer tags, transfer state, customer group and employee-mediated mass-send task. [S8][S9][S17]
- `wecom.kf.*`: `open_kfid`, service-state machine, sync token, send opportunity/window and customer-service menu/location types. [S10][S18]
- `wecom.msg_audit.*`: archive `seq`, covered-user/consent state, RSA public-key version, encrypted random key, SDK file ID and native archive message types. [S11][S12]
- `wecom.callback.*`: XML signature/AES metadata and exact raw event. Do not leak these fields into business actors unless a platform-specific handler asks for them. [S13]

## 11. Limits, policy, review, regional and lifecycle risks

1. **Credential/security:** Corp/app/feature Secrets and Webhook/response URLs are bearer secrets. Keep in process-owned secret storage, rotate on leak, redact query strings, and never serialize into actor messages/logs. [S1][S3][S4]
2. **Visibility/admin control:** App recipient and contact capabilities can change when an administrator changes visible scope, disables an app, changes trusted IP, or revokes feature authorization. Capability discovery must be live/cached with invalidation. [S2][S6]
3. **Group robot:** 20 messages/min per Webhook; text 2,048 bytes; Markdown 4,096; image ≤2 MiB JPG/PNG; uploaded ordinary file ≤20 MiB; voice ≤2 MiB/60s AMR; uploaded media ID valid three days and bound to that Webhook. Markdown v2 lacks mentions and needs newer clients for rich rendering. [S3]
4. **Application chat:** Only self-built/root-visible apps and app-owned chats. Enterprise throughput is in recipient-deliveries (“person-times”), and a member can be silently protected after high per-app volume. Exact certification/enterprise-size bands should be configuration, not hard-coded contract. [S7]
5. **Internal application sends:** Type-specific bytes/recipient counts and app-visible scope apply; active-call limits and `45009` vary by endpoint. Token retrieval must be cached. [S2][S16]
6. **Callback liveness:** Normal callbacks require quick acknowledgement (five seconds) and may retry three times. Decrypt, validate, dedupe, durably enqueue, then acknowledge; do not execute business logic inline. [S13]
7. **IR reply:** one use and one hour. An expired/consumed response URL is terminal; never retry blindly after ambiguous transport failure without idempotency evidence. [S4]
8. **Customer Contact:** feature authorization and employee visibility apply. Mass sends are constrained campaigns and require employee action; customer/customer-group frequency policy is not a general send quota. [S8][S9]
9. **Customer Service:** send only in allowed service state and customer opportunity. Current cited rule is five messages within 48 hours after the customer message; treat server errors/state as authoritative and do not auto-bypass with other surfaces. [S10]
10. **Archive:** separate entitlement/configuration, covered-member rules, external-party notice/consent, IP allowlist, archive Secret, RSA keys and local secure storage are required. Pull at least within five days, persist `seq`, and protect decrypted content as highly sensitive. [S11][S12]
11. **Region/lifecycle:** WeCom’s mainland-China documentation, qyapi endpoints, WeChat interoperation, certification and feature editions can differ from international availability. We found no authoritative parity guarantee across regions. Mark region as `UNKNOWN` until tenant provisioning confirms it.
12. **Pricing/review:** Archive and some advanced customer/security capabilities can be paid, reviewed, certified or administrator-gated. No exact price is part of the proposed contract.

## 12. Conflicts and unknowns

- **Card response-code lifetime conflict:** the mirrored card-update document contains 24-hour and 72-hour language. Design to the stricter 24 hours and treat server response as authoritative until the live official page resolves the contradiction. [S15]
- **Path instability:** official developer path IDs/navigation yielded changing page titles/content in extraction. Source titles and quoted headings, not numeric path IDs alone, should be recorded in implementation tests.
- **Application-message read details:** a read-member query appears in current mirrored API navigation, but exact supported message types, retention and entitlement were not established confidently. Status remains `UNKNOWN/API_LIMITED`; do not advertise universal read receipts.
- **General active-call ceilings:** the official system has endpoint-specific limits, but a complete current matrix was not reliably extractable. Implement error-aware throttling and retrieve current docs/admin configuration rather than hard-code a universal rate. [S16]
- **Intelligent-robot availability:** official docs and official SDK establish API mode, but rollout/tenant/client requirements were not found in one stable entitlement page. Capability-probe/configure it; do not assume every tenant has it. [S4][S5]
- **Customer-group live messages:** no official evidence supports reading all messages or directly posting arbitrary messages through Customer Contact. Only metadata/lifecycle/campaign semantics are claimed. [S8][S9][S17]
- **Presence, typing, reactions, threads, arbitrary role CRUD, general moderation and ordinary chat history:** no supported official APIs found. Absence is based on checked API families; recheck release notes before permanently declaring unsupported.
- **International/regional equivalence:** `UNKNOWN`; provisioning tests and regional official terms are needed.

## 13. OBCX design implications

1. **Expose small discovered capabilities, not a giant `IBot`.** Suggested independent capabilities: `OutboundApplicationMessaging`, `OwnedAppChat`, `GroupWebhookSend`, `InteractiveRobot`, `DirectoryRead`, `DirectoryAdmin`, `ExternalContactCRM`, `CustomerServiceSession`, and `ComplianceArchive`. This separation follows the credential/semantic boundaries in findings 1–8. [S1–S12]
2. **Transport ownership belongs to the adapter process.** The process validates signatures, decrypts callback XML, refreshes tokens, pulls KF/AR cursors, downloads media, rate-limits and acknowledges within five seconds. Business actors receive only typed serializable events/results plus a redacted raw escape hatch. [S1][S12][S13]
3. **Use a target discriminated union, not a free-form chat ID.** At minimum distinguish `InternalUser`, `Department`, `ContactTag`, `OwnedAppChat`, `GroupWebhook`, `ExternalContact`, `CustomerGroupCampaign`, and `CustomerServiceSession`. This prevents sending a CC `chat_id` to `appchat/send` or treating a Webhook as a room token. [S2][S3][S7–S10]
4. **Namespace all identifiers by tenant, installation and surface.** A safe ID key is `(platform=wecom, corp_id, surface, credential_id, native_id)`. External IDs and archive robot IDs are not internal member IDs. [S1][S8][S10][S12]
5. **Model messages as content + platform rendering.** Normalize text/media/file/link and a conservative display card; retain `wecom.template_card` raw/typed extension for buttons, vote/multi-select and updates. Capability discovery must provide type/size limits for the selected surface. [S2–S4][S15]
6. **Separate acceptance, delivery and read.** Send results should not claim delivery merely from `errcode=0`, especially AC silent recipient protection and employee-mediated CC campaigns. Read receipt is optional/unknown. [S7][S9]
7. **Make interaction tokens ephemeral typed capabilities.** IR response URLs, CC welcome codes and card response codes need expiry/max-use metadata, encrypted storage and no automatic cross-surface fallback. [S4][S9][S15]
8. **Ingress needs durable idempotency.** Use native `MsgId`/`msgid` where available; otherwise a documented composite plus ciphertext/raw hash. Persist before acknowledgement. For AR, commit `seq` only after durable processing; for KF, advance cursor/token transactionally. [S12][S13][S18]
9. **Rate limiting is multidimensional.** Key buckets by tenant + credential + endpoint + destination/recipient where required. Represent limits in requests, messages or recipient-deliveries, and support silent-drop warnings. [S3][S7][S12][S16]
10. **Archive is a compliance stream, never fallback history.** Its DTO should include consent/coverage, action (`send/recall/...`), sender/recipient/room namespaces, native type, encrypted/raw provenance and media reference. Access should be separately authorized and audited. [S11][S12]
11. **Unsupported behavior must fail capability discovery.** Do not expose empty methods for threads, presence, typing, reactions, arbitrary ordinary-group access, generic roles/moderation, or consumer-WeChat features. A caller may explicitly degrade a thread to an ordinary message or a rich card to text, recording semantic loss.
12. **Keep a typed/raw escape hatch.** Preserve encrypted callback metadata only inside the ingress boundary; business events may include redacted `raw_payload` and a versioned `wecom.*` extension for newly introduced event/message types. This is justified by the breadth and evolving archive/card schemas. [S12][S13]

## 14. Claim-to-source checklist

| Claim / recommendation | Sources |
|---|---|
| Separate IA, AC, GR, IR, CC, KF and AR adapters | S1–S5, S7–S12 |
| Tenant/app-specific identity and credentials | S1, S2, S6, S8, S10 |
| Classic GR is outbound-only, with listed media/rate limits | S3 |
| IR callback and one-use/one-hour active reply | S4, S5 |
| IA messaging is scoped; AC is self-built/root-visible/app-owned | S2, S7 |
| CC is metadata/lifecycle/member-mediated campaign, not live-chat bot | S8, S9, S17 |
| KF is separate live customer-service sync/send with 48h/5-send rule | S10, S18 |
| Ordinary history unavailable; AR is separate compliance SDK | S11, S12 |
| Callback encryption, fast ACK, retry and dedupe | S13 |
| Recall/card update are narrow and time/token limited | S14, S15 |
| Contact data/administration follows visibility and special authorization | S6 |
| Per-surface rates and silent-drop-aware delivery model | S3, S7, S12, S16 |
| Typed target/ID namespaces and ephemeral interaction-token DTOs | S2–S4, S7–S10, S12, S15 |
| Compliance stream must remain distinct from message history | S11, S12 |
| Capability discovery rather than giant interface/empty methods | All preceding evidence, especially S2–S12 |

## Sources kept and dropped

- **Kept:** official WeCom developer pages for group message push, application chat, intelligent-robot reply and archive SDK because they expose direct protocol/limit text. [S3][S4][S7][S12]
- **Kept with lower confidence:** Apifox and WDK mirrors where the official client-rendered page was not reliably extractable. They reproduce endpoint names and field tables, but are marked `SECONDARY`. [S2][S6][S8–S11][S13–S18]
- **Dropped:** generic CRM vendor blogs, unofficial SDK wrappers, reverse-engineered desktop automation, consumer-WeChat bot projects, and SEO summaries because they either conflate surfaces, are stale, or do not establish official support.
