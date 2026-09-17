# DingTalk (钉钉) platform capability audit

## 1. Lane metadata

- **Platform:** DingTalk / 钉钉.
- **Scope:** organization/tenant model; internal and multi-tenant apps; application robots, group custom/Webhook robots, and distinct specialized robot surfaces; HTTP callbacks and Stream mode; identity/auth; IM, cards, media, contacts, departments, roles, administration, audit, quotas, review, and regional risks.
- **Research date:** 2026-08-17 UTC.
- **Evidence rule:** official Open Platform documentation, DingTalk Developerpedia, and official SDK repositories were preferred. DingTalk's main documentation is JavaScript-rendered and was frequently not extractable; official Developerpedia and SDK surfaces were used to corroborate it. Numeric quotas found only in an API mirror are labeled secondary and must not be hard-coded without tenant-console/API verification.
- **Status vocabulary:** `NATIVE`, `API_LIMITED`, `EMULATED`, `EXTENSION`, `UNSUPPORTED`, `UNKNOWN` as defined by the lane brief.

## 2. Executive findings

1. DingTalk is organization-centric. An internally distributed app is single-tenant; marketplace/upstream/downstream distribution makes an app multi-tenant, with state and access tokens scoped by `corpId`. [S1][S3]
2. DingTalk separates delegated **User + App** access from administrator-consented **App-Only** access. Organization-wide automation and sensitive application permissions require an organization administrator. [S2][S16]
3. **Application robots** are the strategic full surface: receive and send, group and one-to-one conversation support, OpenAPI access, media upload, and interactive cards. They require application/developer administration and are not equivalent to simple incoming webhooks. [S4][S6]
4. **Group custom robots** are group-scoped outbound Webhooks, not general bot identities: no DM and no inbound-message capability. Developerpedia warns this surface is “即将下线” (coming offline), while existing robots were stated unaffected at the page's publication time. [S13]
5. Application robots receive a direct human-to-bot message without an @ mention, but in a group they receive only messages that @ the robot. They do not receive arbitrary group history or all conversation traffic. [S5][S8]
6. Stream mode is an authenticated outbound WebSocket ingress for event subscriptions, robot messages, and advanced-card callbacks. It does **not** carry outbound IM replies; reply through the temporary session Webhook or OpenAPI. [S6][S7][S8]
7. Stream delivery semantics vary: event subscriptions are retried and may duplicate even after ACK, while robot-message callbacks are currently fire-and-forget. Event handlers therefore need durable idempotency; bot-message handlers need loss-aware design. [S8]
8. Advanced interactive cards support create/deliver/update and HTTP or Stream action callbacks. Ordinary interactive cards currently do not support Stream callbacks. Card behavior is a platform extension rather than a portable button/form abstraction. [S9][S17]
9. Bot outbound operations include group, one-to-one/private, query/read-status, recall, and download of inbound files in the official generated SDK, but these are robot-owned workflows—not a general API for reading, editing, deleting, or moderating arbitrary user messages. [S10]
10. Contact, department, and role APIs exist, but are permission-, organization-, and visibility-scope controlled. Do not interpret a product contact picker or employee directory as unrestricted API access. [S2][S11]
11. DingTalk exposes contact and group-change events, including user/department lifecycle and group membership/owner/title/disband changes; that is not equivalent to an audit-log or moderation-event stream. [S12][S19]
12. Public evidence did not establish a general API for message history, reactions, typing/presence, arbitrary-message moderation, organization-wide IM audit, voice/video/live control, or end-to-end encryption/federation. These must remain unavailable/unknown capabilities, not empty adapter methods.
13. Published numeric limits are fragmented and appear to vary by app type and tenant. A secondary mirror reports custom-bot 20 messages/minute and multiple global/app/IP limits; runtime throttling and official console documentation must be authoritative. [S14]
14. Marketplace distribution requires administrator authorization and incremental consent for newly requested permissions; international account support does not prove API/data-region parity. Cross-border processing is subject to DingTalk policy and applicable law. [S15][S16]

## 3. Research action log

- Searched the official Open Platform domains for: app/tenant distribution, internal vs third-party apps, bot types, Webhook security, group/DM constraints, Stream protocol, card callbacks, message/media operations, contacts/departments/roles, group events, audit/moderation, quotas, review, and overseas processing.
- Entered official documentation through `open.dingtalk.com/document`, official Developerpedia at `open-dingtalk.github.io/developerpedia`, and official `open-dingtalk` GitHub repositories.
- Checked official generated SDK surfaces for robot methods and result fields to distinguish supported operations from product-only functionality.
- Main `open.dingtalk.com` pages were often JavaScript-only and could not be text-extracted. Their stable URLs are retained; corresponding Developerpedia pages link back to them.
- Checked a DingTalk API mirror only for numeric quota evidence. It is not treated as primary authority.
- Search did not locate public official evidence for general IM history, arbitrary message deletion/edit, reactions, typing, presence, general audit export, or moderation enforcement APIs.

## 4. Source register

| ID | Authority | Document / relevant section | URL | Accessed | What it proves |
|---|---|---|---|---|---|
| S1 | OFFICIAL | *介绍* — “概念 / 应用的开发和分发” | https://open-dingtalk.github.io/developerpedia/docs/develop/permission/single_to_multi/intro/ | 2026-08-17 | Internal distribution is single-tenant; marketplace/upstream/downstream forms are multi-tenant. |
| S2 | OFFICIAL | *权限概述* — delegated vs application access | https://open-dingtalk.github.io/developerpedia/docs/learn/permission/intro/overview/ | 2026-08-17 | User-delegated vs app-only permissions; admin requirement and user-access boundary. |
| S3 | OFFICIAL | *获取应用的 Access Token* — `POST /v1.0/oauth2/{corpId}/token` | https://open-dingtalk.github.io/developerpedia/docs/develop/permission/single_to_multi/new_get_app_token/ | 2026-08-17 | Per-organization client-credentials token; `expires_in: 7200`; unauthorized-app error. |
| S4 | OFFICIAL | *聊天机器人概述* — application-robot benefits/requirements | https://open-dingtalk.github.io/developerpedia/docs/learn/bot/overview/ | 2026-08-17 | App robots support inbound/outbound, DM/group, OpenAPI/media, Stream, cards; creation/admin caveat. |
| S5 | OFFICIAL | *机器人接收消息* — Stream/HTTP and mention rule | https://open-dingtalk.github.io/developerpedia/docs/learn/bot/appbot/receive/ | 2026-08-17 | Direct messages arrive; group messages require @robot; inbound files use `downloadCode`. |
| S6 | OFFICIAL | *机器人回复/发送消息* — Webhook vs OpenAPI | https://open-dingtalk.github.io/developerpedia/docs/learn/bot/appbot/reply/ | 2026-08-17 | Session Webhook expiry; OpenAPI/card sending; Stream cannot send replies. |
| S7 | OFFICIAL | *Stream Mode 概述* — directions, functions, network model | https://open-dingtalk.github.io/developerpedia/docs/learn/stream/overview/ | 2026-08-17 | Stream is inbound only; events, bot receives, card callbacks; outbound TLS/no public listener. |
| S8 | SPEC | *Stream 协议描述* — connection, topics, ACK/retry | https://open-dingtalk.github.io/developerpedia/docs/learn/stream/protocol/ | 2026-08-17 | Connection registration, 90-second one-use ticket, topics, ACK schema, duplicate/fire-and-forget semantics. |
| S9 | OFFICIAL | *互动卡片概述* — ordinary vs advanced, callback mode | https://open-dingtalk.github.io/developerpedia/docs/learn/card/intro/ | 2026-08-17 | Card create/deliver/update; advanced HTTP/Stream callbacks; ordinary cards no Stream callback. |
| S10 | OFFICIAL | Alibaba Cloud DingTalk Go SDK `robot_1_0` generated API | https://pkg.go.dev/github.com/alibabacloud-go/dingtalk/v2/robot_1_0 | 2026-08-17 | `BatchSendOTO`, group/private send/query/recall, read-query result, message-file download, partial failure lists. |
| S11 | OFFICIAL | *创建部门* and *添加角色组* | https://open.dingtalk.com/document/development/address-book-creation-department-established-department ; https://open.dingtalk.com/document/development/add-a-role-group | 2026-08-17 | Official department and role-group management surfaces exist. |
| S12 | OFFICIAL | *主数据回调事件* | https://open.dingtalk.com/document/orgapp-server/primary-data-callback | 2026-08-17 | Contact lifecycle event family; user add/modify/leave and department create/modify/remove. |
| S13 | OFFICIAL | *群自定义机器人概述* and official access page | https://open-dingtalk.github.io/developerpedia/docs/learn/bot/webhook/overview/ ; https://open.dingtalk.com/document/orgapp/custom-robot-access | 2026-08-17 | Custom bot is outbound group-only/no inbound/no DM; lifecycle warning; official security configuration entry. |
| S14 | SECONDARY | DingTalk API mirror — *调用频率限制* | https://dingtalk.apifox.cn/doc-392370 | 2026-08-17 | Candidate numeric global/IP/app/custom-bot limits; requires primary verification. |
| S15 | OFFICIAL | *Privacy Policy of DingTalk* — transfer/storage provisions | https://www.dingtalk.com/en/privacy_policy | 2026-08-17 | Cross-border handling is conditional on applicable legal requirements/notice/consent. |
| S16 | OFFICIAL | *应用市场开通授权* | https://open-dingtalk.github.io/developerpedia/docs/learn/permission/manage/app-store-consent/ | 2026-08-17 | Tenant administrator grants requested app permissions; later scopes need incremental consent. |
| S17 | OFFICIAL | *进阶：卡片回调* — callback parameters and update example | https://open-dingtalk.github.io/developerpedia/docs/explore/tutorials/stream/bot/go/card-callback/ | 2026-08-17 | Button return-request parameters, Stream callback processing, card-variable update, group/robot spaces. |
| S18 | OFFICIAL | *获取用户通讯录个人信息* | https://open.dingtalk.com/document/isvapp/dingtalk-retrieve-user-information | 2026-08-17 | User profile fields are scope-controlled (`Contact.User.Read`; mobile has a separate sensitive scope). |
| S19 | OFFICIAL | *感知群变化（事件订阅）* | https://open.dingtalk.com/document/org/group-change-awareness-event-subscription | 2026-08-17 | Group member add/remove, disband, owner change, title change subscription family. |

## 5. Product vs official API boundary

| Surface | End-user/product reality | Official automation boundary |
|---|---|---|
| Organization | Employees, departments, administrators, roles, workbench, chats | App operates in a `corpId`; permissions, visible range, and administrator consent bound access. [S1-S3][S16] |
| Normal IM | Users can DM, group-chat, use rich media, mentions, reactions, calls, and administration features | A robot sees its own DM and @-directed group traffic, not arbitrary IM or history. [S5][S8] |
| Application robot | Installed application identity in DM/group | Full official robot ingress/egress plus OpenAPI and cards, subject to app permissions. [S4-S10] |
| Group custom robot | Group add-on used for notifications | Incoming Webhook to one configured group; outbound only, no DM/inbound. Distinct security and lifecycle. [S13] |
| Intelligent-customer-service/AI assistants/scene-group robots | Specialized product surfaces | Separate APIs and eligibility rules; do not alias to generic application robot without capability probing. |
| Interactive card | User can click buttons and interact with card components | Card template/instance/open-space semantics, HTTP or advanced-card Stream callback, explicit updates. [S9][S17] |
| Contacts/departments/roles | Rich employee directory and admin UI | Scoped OpenAPIs/events; sensitive fields and writes require permissions/admin. [S2][S11][S12][S18] |
| Moderation/audit | Group owners/admins and enterprise controls exist in product | No public evidence of a general bot moderation or organization-wide IM audit API. Robot-owned recall is narrower. [S10] |

## 6. Capability evidence table

| Capability | Product | Official API/status | Restrictions / semantics | Evidence | Confidence |
|---|---|---|---|---|---|
| Bot/app identity | NATIVE | NATIVE (app robot); API_LIMITED (custom bot) | App `ClientId`, `robotCode`, tenant context; custom bot is one group Webhook. | S3-S6,S13 | HIGH |
| Authentication | NATIVE | NATIVE | App-only client credentials per `corpId`; delegated User+App OAuth for user resources; secret must remain server-side. | S2,S3 | HIGH |
| Multi-tenant / multi-account | NATIVE | API_LIMITED | Multi-tenant only after marketplace/upstream distribution and tenant consent. Store credentials/tokens by tenant; separate app instances/robots. | S1,S3,S16 | HIGH |
| Bot status/enablement | NATIVE | API_LIMITED | Official SDK exposes single-chat robot status management; tenant installation/authorization can be revoked. | S10,S16 | MEDIUM |
| User/profile | NATIVE | API_LIMITED | Scope-controlled; sensitive mobile requires separate permission; delegated access cannot exceed user access. | S2,S18 | HIGH |
| Contacts/social graph/follow | Product: contacts NATIVE; public social graph UNKNOWN | Contacts API_LIMITED; follow graph UNSUPPORTED | Enterprise directory is not a public follow/social graph. | S2,S11,S18 | HIGH |
| Departments | NATIVE | API_LIMITED | Read/write and lifecycle events exist; authorization/visible range applies. | S11,S12,S16 | HIGH |
| Organization roles | NATIVE | API_LIMITED | Role-group APIs exist; do not equate organization roles with per-chat moderation roles. | S11 | MEDIUM |
| DM / human-bot private chat | NATIVE | NATIVE for app robot; UNSUPPORTED custom bot | Direct messages need no @. Official SDK includes OTO/private send/query/recall. | S4,S5,S10,S13 | HIGH |
| Group chat | NATIVE | API_LIMITED | App robot receives only @robot group messages. Custom Webhook sends only to installed group. | S5,S8,S13 | HIGH |
| Guild/server/space/room/channel | Product: scene groups/spaces exist | EXTENSION | DingTalk `openConversationId` and card `openSpaceId` are not Discord-like guild/channel hierarchy. | S9,S10,S17 | MEDIUM |
| Threads/topics | Product UNKNOWN | UNKNOWN | No official general thread/reply-tree evidence found. | — | LOW |
| Message create/send | NATIVE | NATIVE app robot; API_LIMITED custom bot | App OpenAPI/session Webhook; Stream is not egress. Custom types include text/Markdown/link/actionCard/feedCard on the Webhook surface. | S6,S7,S10,S13 | HIGH |
| Message get/history | NATIVE product | UNSUPPORTED general history; API_LIMITED send-status query | SDK query methods concern bot sends/read status, not arbitrary history. | S10 | HIGH |
| Edit/update | NATIVE product | UNSUPPORTED ordinary message; NATIVE card update | Model mutable cards separately from message edit. | S9,S17 | HIGH |
| Delete/recall | NATIVE product | API_LIMITED | Robot APIs expose recall for robot-sent group/OTO/private/DING operations; no arbitrary-message delete. | S10 | HIGH |
| Reply | NATIVE | EMULATED/API_LIMITED | Bot “reply” is another send through session Webhook/OpenAPI; no evidence of portable reply linkage. Session Webhook expires. | S6 | HIGH |
| Quote/forward | NATIVE product | UNKNOWN / card forward EXTENSION | Card model has `supportForward`; no general bot quote/forward evidence. | S17 | MEDIUM |
| Rich text/Markdown/mentions | NATIVE | API_LIMITED | Custom bot supports text/Markdown and mention parameters; app robot has message-template keys. Exact cross-surface schema differs. | S6,S10,S13 | MEDIUM |
| Reactions/stickers/polls | NATIVE product (some client features) | UNSUPPORTED/UNKNOWN | No official robot APIs found; must not infer from client. | — | MEDIUM |
| Cards/buttons | NATIVE | NATIVE/EXTENSION | Ordinary and advanced cards; advanced create/deliver/update and Stream/HTTP return-request actions. | S9,S17 | HIGH |
| Forms/card inputs | NATIVE card UI | API_LIMITED/EXTENSION | Card interactions can return action/form data, but supported component schemas and validation are template/version specific; preserve raw typed action payload. | S8,S9,S17 | MEDIUM |
| Images/media upload | NATIVE | API_LIMITED | Application identity can upload image/file and use MediaID; custom Webhook is URL/card oriented. | S4,S6 | HIGH |
| Inbound image/audio/video/file | NATIVE | API_LIMITED | Inbound payload can carry `downloadCode`; download through robot file API. Group-vs-DM type parity was not conclusively established in primary docs. | S5,S10 | MEDIUM |
| Media download | NATIVE | NATIVE for bot-received files | `RobotMessageFileDownload`/downloadCode path; URLs should be treated as temporary. | S5,S10 | HIGH |
| Media groups/albums | Product UNKNOWN | UNKNOWN | No official grouped-media abstraction found. | — | LOW |
| Members | NATIVE | API_LIMITED | Group member changes are subscribable; complete generic membership-management parity not established. | S19 | MEDIUM |
| Group roles/permissions | NATIVE product | UNKNOWN | Organization roles and group owner events exist, but public generic group-role mutation APIs were not established. | S11,S19 | MEDIUM |
| Moderation | NATIVE product | UNSUPPORTED/UNKNOWN | No public general mute/ban/kick/arbitrary-delete bot API found. | — | MEDIUM |
| Audit | NATIVE admin/compliance product | UNKNOWN | Contact/group events are not a complete audit log. No current general IM audit export API was verified. | S12,S19 | MEDIUM |
| Commands/interactions | NATIVE | EMULATED + EXTENSION | Parse @robot/DM text for commands; native interaction is a card callback, not slash-command protocol. | S5,S8,S17 | HIGH |
| Incoming Webhooks | NATIVE | API_LIMITED | Custom group Webhook outbound-to-DingTalk; temporary `sessionWebhook` after app-bot ingress. Security settings and expiry differ. | S6,S13 | HIGH |
| Event subscriptions | NATIVE | NATIVE | HTTP or Stream selected in developer console; selected event types delivered via `*` Stream topic. | S7,S8 | HIGH |
| Presence/typing | NATIVE product | UNSUPPORTED/UNKNOWN | No official automation evidence. | — | MEDIUM |
| Read receipts | NATIVE | API_LIMITED | Batch robot-send query returns per-user read status/timestamp; no general conversation receipt API. | S10 | HIGH |
| Feed/post/repost/follow/notification | Product has workbench/notifications | EXTENSION/UNKNOWN | Robot notifications exist; no portable social feed/follow model established. | S6,S10 | MEDIUM |
| Voice/video/live/space | NATIVE product | UNSUPPORTED/UNKNOWN for bot control | No official bot call/live management evidence found. | — | MEDIUM |
| Encryption | TLS transport NATIVE | API_LIMITED | Stream uses TLS and app authentication; no claim of bot-visible end-to-end encryption. | S7,S8 | HIGH |
| Federation | UNKNOWN | UNSUPPORTED | Multi-organization distribution is tenancy, not open federation. | S1 | HIGH |
| Compliance/region | NATIVE product controls | API_LIMITED/UNKNOWN | Admin authorization and privacy obligations apply; residency/API region parity not publicly established. | S15,S16 | MEDIUM |
| Idempotency | Product N/A | API_LIMITED | Stream events can duplicate even after ACK; dedupe by event identifiers. Bot callbacks are fire-and-forget. | S8 | HIGH |
| Pagination | Product N/A | API_LIMITED | Contact/list APIs are expected to paginate, but a universal cursor contract is not evidenced; expose per-operation page tokens. | S11 | LOW |
| Rate/message/payload limits | Product N/A | API_LIMITED | Limits vary by endpoint/app type. Secondary mirror numbers are provisional only. Session Webhook has explicit expiry timestamp. | S6,S14 | MEDIUM |

## 7. Inbound event inventory

| Family / topic | Delivery | Representative data/events | Semantics |
|---|---|---|---|
| Robot message `/v1.0/im/bot/messages/get` | HTTP callback or Stream `CALLBACK` | sender/corp/staff IDs, conversation ID/type, `msgId`, type-specific content, temporary `sessionWebhook`, expiry, `robotCode` | DM received directly; group only when @robot. Stream bot callback is currently fire-and-forget; ACK mainly diagnostic. [S5][S8] |
| Interactive card `/v1.0/card/instances/callback` | HTTP or Stream `CALLBACK` for advanced cards | card instance/track context, operator, returned action/form parameters | Callback mode chosen on card create, not inherited from general app callback mode. Ordinary cards lack Stream callback. [S8][S9][S17] |
| Event subscriptions `*` | HTTP or Stream `EVENT` | Event metadata includes `eventId`, `eventType`, `eventCorpId`, born time, app ID | Event types are selected in developer-console UI; code subscribes to `*`. ACK `SUCCESS`/`LATER`; timeout retries and duplicates possible. [S8] |
| Contact lifecycle | event subscription | `user_add_org`, `user_modify_org`, `user_leave_org`, user activation; `org_dept_create/modify/remove` | Organization permission and tenant scope apply. [S12] |
| Group change | event subscription | member add/remove, group disband, owner change, title change | Change awareness, not message-content firehose or full audit. [S19] |
| Other business events | event subscription | approval, attendance, schedule and many other selected families | Platform documents nearly 100 events; OBCX should register only explicitly modeled families and retain a raw extension. [S8] |
| Stream system | Stream `SYSTEM` | `ping`, `disconnect` | Echo ping opaque value; reconnect after disconnect. Ticket is valid 90 seconds and for one connection only. [S8] |

## 8. Outbound operation inventory

| Surface | Operations | Result/error/async semantics |
|---|---|---|
| Custom group robot | POST text, Markdown, link, actionCard, feedCard to configured group Webhook | Webhook authentication/security settings are surface-specific; no inbound or DM. Treat HTTP success as acceptance, not universal delivery/read proof. Lifecycle risk. [S13] |
| App robot session reply | POST using inbound `sessionWebhook` | URL is temporary; `sessionWebhookExpiredTime` must be checked. Not a Stream response. [S6][S8] |
| App robot OpenAPI | send group, OTO, private-chat messages; batch OTO; query; recall; robot status; list/query bot-in-group; file download | Access token required. Batch OTO returns `processQueryKey`, invalid recipients, and flow-controlled recipients; later query can report send/read status. Recall returns success/failure partitions. [S3][S10] |
| Cards | create, deliver, create-and-deliver, update; ordinary robot card send/update | Advanced cards use template ID, `outTrackId`, card variables, open space/delivery model, callback type. Update is card-instance mutation, not generic message edit. [S9][S17] |
| Contacts/departments/roles | scoped read/write/list operations; contact selection JSAPIs; lifecycle subscriptions | Authorization and visibility range govern accessible users/departments. Writes require explicit permission/admin policy. [S2][S11][S16][S18] |
| Media | app uploads to obtain MediaID; download inbound robot attachment by `downloadCode` | Preserve media identifier, MIME/type, and temporary URL expiry. Do not assume custom-bot media parity. [S4][S5][S10] |

## 9. Normalized common-capability candidates

1. **Tenant installation:** `tenant_id`, installation ID/status, granted scopes, visibility scope, distribution class. DingTalk `corpId` maps to tenant, but upstream/downstream distribution stays extended. [S1][S3][S16]
2. **Principal:** user, application, bot; identifiers typed by namespace (`userId`, union/global identifier, bot code). Never store them as one unqualified string. [S3][S8]
3. **Conversation:** `DIRECT` or `GROUP`, opaque platform conversation ID, optional title. Do not force DingTalk scene/card spaces into guild/channel hierarchy. [S8][S17]
4. **InboundMessage:** event ID/message ID, tenant, bot, sender, conversation, timestamp, mentions, content union, raw payload. Include `bot_was_mentioned`; ingress capability states the group @ gate. [S5][S8]
5. **SendMessage:** destination, content union, mentions, idempotency key if supported, result containing platform tracking/process key and partial failures. Message-type availability is per bot surface. [S6][S10]
6. **MediaReference:** type, media/download token, temporary URL, expiry, filename/size if supplied. Resolve through an adapter operation. [S5][S10]
7. **EventEnvelope:** delivery ID, tenant, event type, occurred time, payload, raw extension, ACK disposition. At-least-once vs fire-and-forget must be declared per family. [S8]
8. **Directory capabilities:** get/list user, department tree, memberships, role assignments, permission/visibility boundary and page token. [S2][S11][S18]
9. **Mutable interactive artifact:** send card, receive action, update card. It may share a high-level concept with Slack/Teams cards, but template/open-space details remain namespaced. [S9][S17]

## 10. Required namespaced extensions

- `dingtalk.corpId`, distribution type (internal/marketplace/upstream/downstream), and application visible range. [S1][S16]
- `dingtalk.robotCode`, custom-WebHook vs app-robot discriminator, and specialized robot class.
- `dingtalk.sessionWebhook` plus `sessionWebhookExpiredTime`; this is a temporary response route, not a generic webhook credential. [S6][S8]
- `dingtalk.openConversationId` and card `openSpaceId`/space-delivery models. [S10][S17]
- `dingtalk.cardTemplateId`, `outTrackId`, card parameter map, callback type, card instance/action raw payload. [S9][S17]
- `dingtalk.processQueryKey`, invalid-recipient list, and flow-controlled-recipient list for asynchronous/batch robot sends. [S10]
- `dingtalk.downloadCode` for bot-received media. [S5][S10]
- DingTalk DING operations and scene-group/customer-service/AI-assistant semantics; these should not silently become generic chat messages. [S10]
- Stream subscription `topic="*"` with console-selected events, `SUCCESS/LATER` ACK, ping opaque value, and disconnect reason. [S8]

## 11. Limits, policy, review, regional and lifecycle risks

- **Custom robot lifecycle:** official Developerpedia warns that group custom robots are coming offline, while saying existing instances were then unaffected. Treat new creation as high-risk and prefer app robots. The warning has no dated shutdown schedule in captured evidence. [S13]
- **Throttling:** secondary documentation reports custom robots at 20 messages/minute (with possible 10-minute throttling), IP-wide 6,000 calls/20 seconds, and different per-app/per-tenant limits. These figures are **not sufficiently authoritative for a stable contract**; observe 429/flow-control fields, retry with jitter, and consult the current console/endpoint page. [S10][S14]
- **Recipient ceilings/payload limits:** the secondary mirror reports differing batch-recipient ceilings for internal and third-party apps. Exact per-message text/card/media sizes and card-instance ceilings were not verified. Mark discoverable/configurable, not constants. [S14]
- **Passive reply window:** session Webhook expiration is supplied per inbound message. Never encode a fixed duration; use `sessionWebhookExpiredTime`. [S6][S8]
- **Stream:** connection ticket lasts 90 seconds and can open only one connection; multiple clients are randomly load-balanced. Events may duplicate; bot messages may be lost because they are fire-and-forget. [S8]
- **Permissions/review:** marketplace installation shows requested application permissions to a tenant administrator; newly added scopes require incremental consent. Marketplace publication/review details and commercial terms were not sufficiently captured to make a stable claim. [S16]
- **Admin control:** an admin can withhold/revoke access; application permissions have a higher threshold. Design re-consent and disabled-installation states. [S2][S3][S16]
- **Regions:** DingTalk supports international-facing product/account experiences, but no official evidence here establishes equivalent OpenAPI availability, endpoints, data residency, event coverage, or marketplace review outside mainland China. Mark region capability `UNKNOWN`.
- **Privacy/compliance:** cross-border processing must follow applicable law and may require notice/separate consent. OBCX must avoid treating a granted API scope as sufficient legal basis. [S15]
- **Legacy/current APIs:** official documentation notes old and new server APIs coexist; newer card callback features can exist only in new APIs. Pin endpoint generation and surface version. [S9]

## 12. Conflicts and unknowns

1. The card-callback tutorial describes advanced-card OpenAPI and `callbackType: STREAM`, but its closing “best practices” text calls the example ordinary-card content. The card overview is clearer: ordinary cards do not support Stream; advanced cards do. Follow the overview/current endpoint schema. [S9][S17]
2. Developerpedia is official/community-maintained documentation and sometimes says content will later be synchronized into the main docs. Protocol-critical behavior was cross-linked to the official Stream specification, but must be regression-tested. [S8]
3. Main official pages were JavaScript-rendered; exact current message keys, payload byte limits, media type/size limits, retry timers, group-card instance ceilings, and endpoint-specific QPS were not extractable. `UNKNOWN` until verified in API Explorer/console.
4. Exact inbound media parity across DM and group application-robot conversations is unresolved. Do not claim group audio/video/file ingress until tested against the current receive-message schema.
5. No verified public general APIs for arbitrary IM history/search, edits, deletes, reactions, typing/presence, moderation, or organization-wide message audit were found.
6. Organization role groups are not proven to control chat permissions; model organization and conversation authorization separately.
7. Specialized intelligent-customer-service robots, AI assistants, connectors, and scene-group robots have separate eligibility and APIs. They were identified but not audited deeply; capability discovery must not merge them with generic robots.
8. App Market review SLAs, paid-tier gating, exact regional endpoints/residency, and current shutdown date for custom bots remain unknown.

## 13. OBCX design implications

1. **Use composable capability descriptors, not a giant `IBot`.** Suggested independent capabilities: `MessageIngress`, `MessageSend`, `MessageRecall`, `BatchSendStatus`, `CardSend`, `CardActionIngress`, `CardUpdate`, `MediaResolve`, `DirectoryRead`, `DirectoryWrite`, and `OrgEventIngress`. Surface and tenant installation determine availability. [S4-S10]
2. **Make transport process-owned.** A DingTalk gateway owns HTTP/Stream sockets, ticket refresh, ACKs, token refresh, Webhook signing/security, retries, and rate limiting. Business actors receive serializable typed events and emit typed requests/results. [S3][S7][S8]
3. **Represent delivery guarantees explicitly.** `EventEnvelope.delivery = AT_LEAST_ONCE` for Stream events and `BEST_EFFORT_NO_REDELIVERY` for robot callbacks. Persist/dedupe `eventId`/`messageId` before effects. [S8]
4. **Model robot surface as a required discriminator:** `APP_ROBOT`, `CUSTOM_GROUP_WEBHOOK`, and namespaced specialized kinds. Capability discovery should reject DM/inbound/cards/media operations on a custom robot rather than degrade silently. [S4][S6][S13]
5. **Separate reply routes from message semantics.** An inbound bot event may carry an expiring session route; egress selection chooses session Webhook while valid or app OpenAPI otherwise. Stream itself is never selected for outbound reply. [S6-S8]
6. **Use a content union with per-surface negotiation.** Normalize plain text, Markdown, link, image/file reference, and card reference; preserve DingTalk message-key/parameter and raw escape hatches. Unsupported sticker/reaction/poll types should fail capability checks. [S6][S10][S13]
7. **Cards need their own state machine.** Store template ID, track/instance ID, open space, variables, callback mode, and optimistic version. Treat action callbacks as commands and updates as card operations, not generic message edits. [S9][S17]
8. **Tenant credentials and permissions are first-class state.** Key token caches and webhook/application installations by `corpId`; record granted scopes and consent version; transition to `REAUTH_REQUIRED` on authorization failures. [S1-S3][S16]
9. **Type identifiers.** Use tagged `DingTalkUserId`, `CorpId`, `RobotCode`, `ConversationId`, `CardTrackId`, and `DownloadCode`; do not compare unlike ID namespaces. [S3][S8][S10][S17]
10. **Return partial/async results.** Batch sends should return tracking key, invalid recipients, flow-controlled recipients, and subsequent delivery/read query state. Do not collapse to Boolean success. [S10]
11. **Keep moderation/audit absent unless discovered.** Robot-owned recall may be exposed narrowly; arbitrary message delete, mute/ban, and audit search must not appear as callable no-ops. [S10]
12. **Centralize adaptive limits.** Read retry signals/HTTP status and model endpoint/surface buckets. Avoid stable constants from secondary quota pages. [S10][S14]

## 14. Claim-to-source checklist

| Claim / design conclusion | Sources |
|---|---|
| Single- vs multi-tenant and `corpId` token scope | S1,S3 |
| Delegated vs app-only and admin consent | S2,S16 |
| App robot is full DM/group/OpenAPI/card surface | S4,S5,S6 |
| Custom Webhook robot is group-only/outbound and lifecycle-risky | S13 |
| Group inbound requires @robot | S5,S8 |
| Stream is ingress-only; replies use Webhook/OpenAPI | S6,S7,S8 |
| Stream event duplicates vs robot fire-and-forget | S8 |
| Advanced card HTTP/Stream callbacks and ordinary-card limitation | S9,S17 |
| Robot send/query/read-status/recall/file operations and partial results | S10 |
| Contact/department/role access is permission-scoped | S2,S11,S12,S18 |
| Group/contact changes are events, not full audit | S12,S19 |
| Numeric quotas are provisional secondary evidence | S14 |
| Marketplace incremental consent | S16 |
| Regional parity unknown; cross-border policy applies | S15 |
| OBCX capability decomposition, typed IDs/routes/results | S1-S10,S13,S16,S17 |
