# Consumer WeChat (微信) — officially supported developer-surface audit

## 1. Lane metadata

- **Platform:** consumer WeChat / 微信.
- **Scope:** ordinary personal WeChat product; WeChat Official Accounts (公众号, especially Service Accounts/服务号); Mini Programs (小程序); **WeChat Customer Service / 微信客服**; Open Platform website/mobile login and share SDK. WeChat Channels/视频号 is mentioned only where 微信客服 delivers typed commerce/content messages.
- **Explicit exclusion:** WeCom/企业微信 messaging, contacts and group-bot APIs are not treated as WeChat capabilities. 微信客服 is included as its own consumer-facing service because customers converse from WeChat, but its API and administration are hosted by the WeCom platform (`open.work.weixin.qq.com`, `qyapi.weixin.qq.com`); this does **not** make general WeCom capabilities available.
- **Research date / access date:** 2026-08-17 UTC.
- **Freshness:** Tencent documentation is live and mutable. Several current Service Account pages show 2025-11 changelog entries. Exact account entitlements, quotas and rollout status must be discovered per account. `UNKNOWN` is used where current official material did not establish a rule.
- **Status vocabulary:** `NATIVE`, `API_LIMITED`, `EMULATED`, `EXTENSION`, `UNSUPPORTED`, `UNKNOWN` as defined by the lane brief.

## 2. Executive findings

1. **There is no supported ordinary-personal-WeChat chat bot/API surface.** Personal 1:1 chats, group chats, Moments, contacts, reactions, calls and presence are consumer product features, not developer APIs. They must never enter OBCX as common WeChat automation capabilities. The supported surfaces use app/account identities instead. [S1][S16]
2. **Official Accounts are follower-to-account conversations, not ordinary DMs or groups.** Current official APIs expose inbound messages/events, synchronous passive XML replies, windowed customer-service sends, menus, followers, tags, media, notifications and publishing. [S2][S3][S4][S5][S6]
3. **Passive reply and customer-service send are different transports.** A passive reply is the HTTP response to a webhook and must arrive within 5 seconds; otherwise acknowledge `success`/empty and use the asynchronous customer-service endpoint. The platform retries up to three times if not acknowledged. [S3][S4]
4. **Current Official Account customer-service quotas are interaction-specific:** user message → 5 sends/48 hours; qualifying menu click, follow or QR scan → 3 sends/1 minute. Current docs restrict certified Official/Service Accounts and say the endpoint is primarily for human-service scenarios. [S5]
5. **Official Account notifications are distinct from chat.** Certified Service Accounts can send approved template messages; certified Official/Service Accounts can use subscription notices. Templates have typed fields, anti-marketing controls and account/review restrictions. [S6][S7]
6. **Mini Programs do not expose a general WeChat inbox.** They provide a user-opened customer-service conversation, push messages/events to a configured backend, user-consented one-time/long-term subscription notifications, login/session identity, and interactive UI inside the Mini Program. [S10][S11][S12]
7. **WeChat Customer Service is a separate, managed contact-center surface.** API send is allowed only while the customer is in designated automation states and only after the customer messages: 5 sends within 48 hours. It has session assignment/state, rich message menus, media, customer metadata, callbacks-plus-pull synchronization and delivery-failure events. [S13][S14][S15]
8. **微信客服 success is not delivery.** `send_msg` returns/accepts a unique `msgid`, but final failure is asynchronous (`msg_send_fail`); the adapter must correlate and revise delivery state. [S13][S14]
9. **Identifiers are scoped.** Official Accounts/Mini Programs use per-app OpenID; UnionID is available only under binding/authorization conditions. 微信客服 uses `external_userid`, with profile access limited to recently interacting users and different fields withheld from third-party apps. [S8][S9][S15]
10. **No surface offers a Discord/Matrix-style room/history model.** Official Account history is limited to its customer-service record APIs; 微信客服 pull covers only the most recent three days and explicitly omits successful messages sent by `send_msg`. Personal chat history is unsupported. [S2][S14]
11. **Open Platform login/share is user-mediated integration, not chat automation.** Reviewed website/mobile apps can perform OAuth login, and mobile SDKs can invoke a share composer to conversation/Moments/Favorites; this does not grant read, recipient selection, delivery receipt or later mutation. [S17][S18][S19]
12. **Review and compliance are structural capability constraints.** Certification, approved app/account, categories, templates, user consent, privacy disclosure, content safety and anti-spam rules can remove or limit APIs. Mini Programs prohibit non-public APIs and abuse of customer-service/subscription messaging. [S5][S6][S7][S16][S17][S20]

## 3. Research action log

- Read the lane brief and enumerated four angles: (1) personal-product/API boundary, (2) Official Account messaging/user/media/event rules, (3) Mini Program messaging/auth/review, (4) 微信客服 and Open Platform.
- Queried official domains for passive/customer-service windows, subscription/template messaging, follower data, menus, login/share, 微信客服 synchronization/events/customer profile, and personal-account automation policy.
- Entered the current Service Account API index, then checked individual pages for access token, customer-service send, templates/subscriptions, menus, followers, user info and media.
- Checked Service Account push pages for standard inbound messages, events and passive replies.
- Checked Mini Program customer-service, subscription-message client/server flow, and operating rules.
- Checked 微信客服 official pages for send, sync/events, service state and customer profile. Confirmed that this surface is administered through WeCom but kept it isolated from general WeCom.
- Checked Open Platform official website/mobile OAuth and Android share SDK pages.
- **Inaccessible/ambiguous:** current official search exposed no personal-account bot API; this negative conclusion is based on official developer catalogs plus the official Mini Program prohibition on non-public APIs, not on an undocumented endpoint claim. The current Mini Program customer-service page does not state all message types or an exact outbound quota; applicability of the common `sendCustomMessage` endpoint is official, but Mini-Program-specific trigger details remain `UNKNOWN`. The personal-account agreement page was not reliably extractable, so policy claims in the risk appendix use a clearly labeled secondary copy plus official platform rules.
- Dropped stale mirrors and tutorials when a live Tencent page was available. Older claims that Official Account customer-service sends were “unlimited for 48 hours” conflict with the current official 5-message table and were discarded.

## 4. Source register

All pages accessed 2026-08-17.

| ID | Authority | Title / relevant section | URL | Precise evidence |
|---|---|---|---|---|
| S1 | OFFICIAL | Service Account developer guide — backend/API and push | https://developers.weixin.qq.com/doc/service/guide/ | Service Accounts expose server APIs and event/message pushes; advanced interfaces may require certification. |
| S2 | OFFICIAL | Service Account server API index | https://developers.weixin.qq.com/doc/service/api/ | Enumerates supported menus, messages, customer service, materials, users, publishing, analytics and OAuth; no personal chat/group API. |
| S3 | OFFICIAL | Receive standard messages | https://developers.weixin.qq.com/doc/service/guide/product/message/Receiving_standard_messages.html | POST XML; text/image/voice/video/short-video/location/link; dedupe by `msgid`; 5-second timeout and three retries; optional push encryption. |
| S4 | OFFICIAL | Passive reply | https://developers.weixin.qq.com/doc/service/guide/product/message/Passive_user_reply_message.html | HTTP-response semantics; 5-second deadline; `success`/empty ack; text/image/voice/video/music/news reply; media upload; news limits. |
| S5 | OFFICIAL | `sendCustomMessage` | https://developers.weixin.qq.com/doc/service/api/customer/message/api_sendcustommessage.html | User message 5/48h; menu/follow/scan 3/1m; supported rich types; certified-account applicability; typed error response. |
| S6 | OFFICIAL | `sendTemplateMessage` | https://developers.weixin.qq.com/doc/service/api/notify/template/api_sendtemplatemessage.html | Certified Service Accounts only; typed approved data; anti-duplicate ID for 10 minutes; marketing-block errors; `msgid`. |
| S7 | OFFICIAL | `sendNewSubscribeMsg` | https://developers.weixin.qq.com/doc/service/api/notify/notify/api_sendnewsubscribemsg.html | Certified Official/Service Accounts; selected subscription template, OpenID, typed fields and optional Mini Program destination. |
| S8 | OFFICIAL | `getFans` | https://developers.weixin.qq.com/doc/service/api/usermanage/userinfo/api_getfans.html | Certified accounts; OpenID follower list; cursor pagination; maximum 10,000/page. |
| S9 | OFFICIAL | `userInfo` | https://developers.weixin.qq.com/doc/service/api/usermanage/userinfo/api_userinfo.html | Follow state/time, app-scoped OpenID, optional UnionID; nickname/avatar no longer returned since 2021-12-27. |
| S10 | OFFICIAL | Mini Program customer service | https://developers.weixin.qq.com/miniprogram/dev/framework/open-ability/customer-message/customer-message.html | `button open-type="contact"`; user enters service conversation; backend receives user messages and enter-session events. |
| S11 | OFFICIAL | Mini Program subscription-message guide | https://developers.weixin.qq.com/miniprogram/dev/framework/open-ability/subscribe-message.html | Template configuration, user permission, server send; consent/result/failure events; daily caps documented as 30m with payment and 10m without. |
| S12 | OFFICIAL | `wx.requestSubscribeMessage` | https://developers.weixin.qq.com/miniprogram/dev/api/open-api/subscribe-message/wx.requestSubscribeMessage.html | User UI, accept/reject/ban/filter; max five template IDs; one-time and long-term types cannot mix; user gesture/payment trigger. |
| S13 | OFFICIAL | 微信客服 `send_msg` | https://open.work.weixin.qq.com/api/doc/90001/90143/94677 | 5/48h after customer message; designated automation states; rich types/limits; unique optional `msgid`; success not final delivery. |
| S14 | OFFICIAL | 微信客服 callback and `sync_msg` | https://open.work.weixin.qq.com/api/doc/90001/90143/94670 | `kf_msg_or_event` callback token then pull; recent 3 days; max 1,000; media limits; message/event inventory; recalls and failure events. |
| S15 | OFFICIAL | 微信客服 customer profile | https://open.work.weixin.qq.com/api/doc/90001/90143/95149 | 1–100 customers; recent-48h interaction gate; nickname/profile fields; third-party avatar/gender/UnionID restrictions. |
| S16 | OFFICIAL | Mini Program operating rules — §§3, 5.12, 5.15, 5.18–5.21, 12.4, 15 | https://developers.weixin.qq.com/miniprogram/product/ | Review/category/privacy/content safety; non-public APIs prohibited; abuse of service/subscription messages sanctioned. |
| S17 | OFFICIAL | Website WeChat login | https://developers.weixin.qq.com/doc/oplatform/Website_App/WeChat_Login/Wechat_Login.html | Reviewed website app; OAuth 2 authorization-code; `snsapi_login`; user authorization; one-use 10-minute code. |
| S18 | OFFICIAL | Mobile WeChat login | https://developers.weixin.qq.com/doc/oplatform/Mobile_App/WeChat_Login/Development_Guide.html | Reviewed app; native OAuth; token/refresh rules; OpenID/UnionID; app must be installed; rate guidance. |
| S19 | OFFICIAL | Android Share and Favorites | https://developers.weixin.qq.com/doc/oplatform/Mobile_App/Share_and_Favorites/Android.html | SDK user-mediated share to conversation/Moments/Favorites; text/image/video/web/Mini Program/file objects and payload limits. |
| S20 | OFFICIAL | Access-token guidance | https://developers.weixin.qq.com/doc/oplatform/developers/dev/AccessToken.html | App-type-isolated token; about 2h; central refresh; repeated fetch invalidates prior token; five-minute overlap. |
| S21 | OFFICIAL | Official Account custom menu | https://developers.weixin.qq.com/doc/service/api/custommenu/api_createcustommenu.html | Three top-level × five submenus; click/view/scan/photo/location/media/article/Mini Program interactions; certification differences. |
| S22 | OFFICIAL | Official Account temporary media | https://developers.weixin.qq.com/doc/service/api/material/temporary/api_uploadtempmedia.html | Image/voice/video/thumb upload; three-day media ID; format/size constraints (page contains an internal image-limit inconsistency). |
| S23 | SECONDARY | Reproduced WeChat software-license terms | https://www.elawcn.com/agreement/2025/1015/1552.html | Reproduces ban on unauthorized third-party software/plugins and automated operation. Used only in risk appendix because official agreement text was inaccessible. |

## 5. Product vs official API boundary

| Surface | Personal/product behavior visible to users | Official developer exposure | Boundary verdict |
|---|---|---|---|
| **Ordinary personal WeChat account** | 1:1/group chat, contacts, Moments, calls, reactions, read/UI state and manual media sharing exist in the client. | No public bot identity, chat send/read/history, contact/friend graph, group management, webhook or presence API was found in official catalogs. | Product: `NATIVE`; API: `UNSUPPORTED`. Never infer API support from the client. |
| **Official Account / Service Account** | A user follows an account, opens its conversation, sends messages and uses its menu; account publishes content/notices. | Account-scoped OpenID, inbound push, passive replies, windowed service sends, notifications, followers/tags, media, menus, publishing and OAuth. | `API_LIMITED`; it is an account-follower channel, not a personal DM identity. |
| **Mini Program** | Embedded application UI; login; service conversation opened by user; service notifications. | Mini Program APIs, message push, customer-service send applicability, consented subscription notices and server tokens. | `API_LIMITED`; no general chat inbox/social graph. |
| **微信客服** | A WeChat user enters a branded customer-service conversation from a link/component/Channels context. | Contact-center accounts, customer sessions, callback-triggered pull, send/media/menu/profile/state operations. | `API_LIMITED` and `EXTENSION`; administered via WeCom, but not equivalent to WeCom messaging. |
| **Open Platform app/web integration** | User authorizes login or chooses a share destination in WeChat UI. | OAuth and SDK share invocation for reviewed apps. | `API_LIMITED`; no inbox access or autonomous recipient send. |
| **WeCom general APIs** | Separate enterprise client/product. | Separate tenant/contact/message APIs. | Out of scope; must not be merged into `wechat.*`. |

## 6. Capability evidence table

Abbreviations: **P** personal product, **OA** Official Account, **MP** Mini Program, **KF** 微信客服, **OP** Open Platform.

| Capability | Product support | Official API support/status | Restrictions / semantics | Evidence | Confidence |
|---|---|---|---|---|---|
| Bot/app identity | P accounts exist; OA/MP/KF have branded identities | P `UNSUPPORTED`; OA/MP/KF/OP `API_LIMITED` | AppID/account IDs; not an ordinary user bot | S1,S2,S10,S13,S17 | HIGH |
| Authentication | User login/product native | OA/MP server token `API_LIMITED`; OP OAuth `NATIVE`; KF enterprise app token `API_LIMITED` | AppSecret server-side; app-type isolation; ≈2h token; reviewed OP app | S5,S13,S17,S18,S20 | HIGH |
| Multi-account | User may manually switch some accounts | `EMULATED` by multiple adapter instances | Tokens, OpenIDs, quotas and callbacks remain per app/account; no global session | S8,S9,S20 | HIGH |
| Status/presence/typing | Client may show product UI | OA typing endpoint exists only for service context (`API_LIMITED`); general presence/typing `UNSUPPORTED`; KF servicer status is contact-center state (`EXTENSION`) | Not personal online presence | S2,S14 | HIGH |
| User/profile | P profiles exist | OA `API_LIMITED`; KF `API_LIMITED`; MP/OP consent-scoped | OA nickname/avatar removed; KF recent-interaction and app-type field restrictions; app-scoped IDs | S9,S15,S18 | HIGH |
| Contacts/follow/social graph | P contacts/friends and OA follow exist | P contacts/friends `UNSUPPORTED`; OA follower list/tags `API_LIMITED`; MP/KF social graph `UNSUPPORTED` | OA list only certified; 10k/page; no friend edges | S2,S8,S9 | HIGH |
| 1:1 DM | P supports personal chats | P `UNSUPPORTED`; OA/MP/KF customer conversation `API_LIMITED` | Only user↔account/service; windowed sends | S5,S10,S13 | HIGH |
| Group/guild/server/space/room/channel | P supports group chats | All studied official APIs `UNSUPPORTED` as personal/group automation | 微信客服 sessions are 1:1 service sessions, not rooms; do not import WeCom group APIs | S2,S13,S14 | HIGH |
| Thread/topic | Some product UIs may visually organize content | `UNSUPPORTED` | No thread/topic identifiers or operations | S2,S14 | HIGH |
| Message create | P manual; OA/MP/KF service sends | P `UNSUPPORTED`; OA/MP/KF `API_LIMITED`; OP share `API_LIMITED` | OA 5/48h or 3/1m; KF 5/48h; passive reply separate; OP user chooses target | S4,S5,S13,S19 | HIGH |
| Get/history | P client history exists | P `UNSUPPORTED`; OA customer-service record `API_LIMITED`; KF `API_LIMITED` | KF recent 3 days, pull cursor, excludes successful API-sent messages | S2,S14 | HIGH |
| Edit message | Product support varies | `UNSUPPORTED`; MP shared dynamic card update is `EXTENSION` | Dynamic shared-message update is not chat edit | S2 | HIGH |
| Delete message | P can locally delete; OA can delete a mass publication | Chat delete `UNSUPPORTED`; OA mass delete `EXTENSION` | Publication deletion ≠ recipient chat deletion | S2 | HIGH |
| Reply/quote | P manual reply/quote | OA passive response `API_LIMITED`; quote/reply linkage `UNSUPPORTED`; KF response code `EXTENSION` | Passive response correlates transport request, not a durable quote object | S4,S14 | HIGH |
| Forward | P manual forwarding | Autonomous forward `UNSUPPORTED`; OP share `API_LIMITED` | User-mediated composer; no target enumeration/delivery result | S19 | HIGH |
| Rich text / mentions | P product supports some formatting/@ | OA/KF text/link `API_LIMITED`; mentions `UNSUPPORTED` | No common rich-text AST or mention entity API | S5,S13 | HIGH |
| Reactions/stickers/polls | P has some product features | `UNSUPPORTED` | KF menu can emulate a small poll but is not a reaction/poll object | S13 | HIGH |
| Cards/buttons/forms | OA menu/cards; MP full UI; KF menus | `API_LIMITED`; platform-specific `EXTENSION` | OA account menu 3×5; KF message menu max 50 items, only 10 click/view/MP; MP forms live inside app | S13,S21 | HIGH |
| Image/audio/video | All service products accept media | OA/KF `API_LIMITED`; MP customer inbound `API_LIMITED` | Media IDs and type/size rules; passive image does not support GIF | S3,S4,S13,S14,S22 | HIGH |
| File | P/KF product supports files | KF `API_LIMITED`; OA general customer send lacks generic file; OP user-share file `API_LIMITED` | KF pull file max 20MB; OP share is user-mediated | S5,S13,S14,S19 | HIGH |
| Media groups/albums | P product supports grouped visual presentation | `UNSUPPORTED` as a durable media-group semantic | Send individual supported media or namespaced news/card | S4,S5,S13 | HIGH |
| Upload/download media | N/A | OA/KF `API_LIMITED`; MP surface-dependent | OA temporary media expires after 3 days; KF inbound media via IDs | S14,S22 | HIGH |
| Members/roles/permissions | P groups have admins; KF has agents | P group roles `UNSUPPORTED`; KF agent/session permissions `EXTENSION` | Enterprise admin configuration and app visibility control KF | S13,S14 | HIGH |
| Moderation/audit | Product/platform moderation exists | General chat moderation `UNSUPPORTED`; OA blacklist/comment APIs and MP content-safety duties `EXTENSION` | Content/category/policy constrained; not a universal moderation API | S2,S16 | HIGH |
| Commands/interactions | OA/KF menus; MP UI | `API_LIMITED` / `EXTENSION` | CLICK/VIEW/scan/photo/location events; menu IDs; no slash-command registry | S13,S21 | HIGH |
| Webhooks/event subscriptions | N/A | OA/MP `NATIVE` for configured callback; KF `API_LIMITED` callback→pull | OA XML 5s ack/retry; KF notification contains token, details pulled | S3,S10,S11,S14 | HIGH |
| Read receipts | Some consumer UI behavior | `UNSUPPORTED` | Delivery result events are not read receipts | S5,S13,S14 | HIGH |
| Feed/post/repost/quote/follow | Moments and OA publications exist | Personal Moments `UNSUPPORTED`; OA publishing/comments/analytics `API_LIMITED`; follow events `NATIVE` | Publication is separate capability family | S2,S3 | HIGH |
| Notification | OA/MP service notifications exist | OA templates/subscriptions, MP subscriptions `API_LIMITED` | Approved templates, consent/category/certification; not chat sends | S6,S7,S11,S12 | HIGH |
| Voice/video/live/space | P calls and Channels live exist | Personal calls/live `UNSUPPORTED`; KF may receive Channels typed messages `EXTENSION` | No media-call control or stream API in studied messaging surfaces | S14 | HIGH |
| Encryption | WeChat transport/product encryption | OA callback encryption `API_LIMITED`; KF callback security `API_LIMITED`; no E2EE key API | OA encrypted push/passive response optional; outbound REST unaffected | S3,S4,S14 | MEDIUM |
| Federation | None exposed | `UNSUPPORTED` | Closed platform identities/endpoints | S2,S20 | HIGH |
| Tenant | OA/MP app account; KF enterprise-managed | OA/MP `EXTENSION`; KF `EXTENSION` | KF administration must not leak generic WeCom tenant semantics | S13,S15 | HIGH |
| Compliance/review | Platform reviewed/moderated | `API_LIMITED` | Certification, app review, category, privacy, content and anti-abuse gates | S5,S6,S7,S16,S17 | HIGH |
| Idempotency/deduplication | N/A | OA ingress `API_LIMITED`; OA template/KF egress `API_LIMITED` | OA `msgid` or user+time dedupe; template client ID 10m; KF unique ≤32-byte `msgid` | S3,S4,S6,S13 | HIGH |
| Pagination/cursors | N/A | OA followers and KF sync `NATIVE` | OA `next_openid`; KF `next_cursor` + `has_more`, max 1,000 | S8,S14 | HIGH |
| Rate limits/quotas | Product hidden | `API_LIMITED` | Per-endpoint/account quotas; query quota API; MP subscription daily caps; interactions windows | S2,S5,S11,S13 | HIGH |
| Passive-reply window | OA conversation product | OA `NATIVE` | 5 seconds; up to 3 retries; acknowledge if asynchronous | S3,S4 | HIGH |
| Payload/message limits | Product varies | `API_LIMITED` | Type-specific bytes/counts; KF text 2,048 bytes; OA menu/card/news limits; media limits | S4,S13,S14,S19,S21,S22 | HIGH |

## 7. Inbound event inventory

### 7.1 Official Account / Service Account — direct HTTP(S) push, XML

- **Standard messages:** text, image, voice, video, short video, location, link. Fields include account/user OpenID, timestamp, type and usually `MsgId`; media arrives by URL and/or `MediaId`. [S3]
- **Core events:** `subscribe`, `unsubscribe`, parameterized-QR subscribe/`SCAN`, consented `LOCATION`, menu `CLICK` and `VIEW`. Menu families also include scan, photo/album and location-selection interactions. [S3][S21]
- **Delivery behavior:** configured URL; optional encrypted mode; receiver must answer in 5 seconds. Missing response causes disconnect and up to three total retries. Dedupe message pushes by `MsgId`, event pushes by user+creation time. [S3][S4]
- **Passive response:** response body may carry text/image/voice/video/music/news. It is not a separate outbound REST call. [S4]
- The API catalog contains additional specialized publication/template/customer-service events. OBCX should retain unknown/unmodeled XML in a raw typed envelope rather than pretend this list is exhaustive. [S2]

### 7.2 Mini Program — configured server URL / cloud function / cloud hosting

- User sends a customer-service message or enters the customer-service conversation. Current overview confirms push but does not enumerate all current customer message types on that page. [S10]
- Subscription events: user accepts/rejects after the prompt; user later rejects from notification management; final asynchronous subscription-message send result. [S11]
- Delivery supports XML/JSON in the subscription flow. Exact retry/ack behavior for all Mini Program event families is `UNKNOWN` from pages checked.

### 7.3 微信客服 — notification callback followed by pull

1. Callback event is `kf_msg_or_event`, carrying `Token` and `OpenKfId`; it is a wake-up signal, not the complete message. [S14]
2. Call `kf/sync_msg` with that token (10-minute validity), saved cursor and `open_kfid`. Without cursor it starts from the earliest item in the recent-three-day retention. Page until `has_more=0`, even if a page has an empty list. [S14]
3. Pullable inbound/agent types include text, image, voice, video, file, location, link, business card, Mini Program, menu reply, Channels product/order/content, merged chat record, and placeholder-only meeting/calendar/note. Successful messages sent through `send_msg` are explicitly not returned. [S14]
4. Events: `enter_session` (possibly with one-time welcome code), `msg_send_fail`, `servicer_status_change`, `session_status_change` (possibly response code), `user_recall_msg`, `servicer_recall_msg`, `reject_customer_msg_switch_change`. [S14]

### 7.4 Open Platform

OAuth redirect/SDK response returns authorization result/code; share SDK returns an invocation result, not recipient message events. No chat webhooks. [S17][S18][S19]

## 8. Outbound operation inventory

| Surface | Operation | Result/async semantics |
|---|---|---|
| OA | Passive HTTP XML reply | Synchronous webhook response, hard 5s transport deadline; no durable send receipt. [S4] |
| OA/MP applicability | `customerServiceMessage.send` / `/cgi-bin/message/custom/send` | JSON `errcode`/`errmsg`; multiple rich types; entitlement and interaction quota. Current page lists MP and certified OA/Service applicability. [S5] |
| OA | Template send | Synchronous acceptance with `msgid`; `client_msg_id` dedupe for same OpenID for 10 minutes; content may be blocked/restricted. [S6] |
| OA | Subscription-notice send | Synchronous error result; approved template and typed fields. [S7] |
| MP | Subscription-message send | Server acceptance plus a final-result push for asynchronous system failure; only after user-granted permission. [S11][S12] |
| OA | Menu create/get/delete/conditional | Synchronous API result; client refresh can lag up to about five minutes. [S21] |
| OA | Temporary/permanent media upload/get/delete | Returns `media_id`; temporary ID expires after three days. [S2][S22] |
| OA | Followers/profile/tags/blacklist | Synchronous, account-scoped; certification and privacy limitations. [S8][S9] |
| KF | `kf/send_msg` | Returns `msgid`; success is only acceptance. Correlate later `msg_send_fail`; send only in designated state and 5/48h window. [S13][S14] |
| KF | Event response/welcome/reply/ending message | Requires transient code returned by enter/session event/state transition; namespaced lifecycle semantic. [S14] |
| KF | Service-state get/transition and agent assignment | Synchronous state machine; enterprise permissions/visibility. [S14] |
| KF | Customer batch profile | 1–100 IDs; interaction recency gate and app-type privacy filtering. [S15] |
| KF | Media upload/download, account/link/agent management | Official catalog-linked operations, tenant-admin constrained; keep under `wechat.customer_service.*`. [S13][S14][S15] |
| OP | Website/mobile OAuth | User-mediated authorization-code flow; code is single-use and 10 minutes; tokens/profile scopes follow authorization. [S17][S18] |
| OP | Mobile share/favorite SDK | Opens WeChat-mediated share flow to conversation/Moments/Favorites; no autonomous target or delivery semantics. [S19] |

## 9. Normalized common-capability candidates

These are safe only as small optional interfaces, never a giant `IBot`.

1. **`InboundMessageEvent`** — account/channel, scoped sender, platform message ID, timestamp, typed content plus raw payload. Works for OA, MP customer service and KF; it must not imply a room or personal DM. [S3][S10][S14]
2. **`SendWindowedMessage`** — recipient, content, optional client correlation ID; capability discovery returns current trigger, expiry, remaining-count-if-known and supported types. OA and KF share the concept but not identical triggers/states. [S5][S13]
3. **`ImmediateIngressReply`** — serializable reply decision consumed by the process-owned ingress transport before a deadline. OA’s 5-second passive XML reply fits; most platforms/adapters may not advertise it. [S4]
4. **`MediaRef` / upload / download** — platform media ID, MIME/type, expiry and size metadata. Never assume media IDs are durable or cross-account. [S14][S22]
5. **`NotificationTemplateSend`** — separate from messages: template ID, typed values, consent/grant reference and destination. OA template/subscription and MP subscription can share an envelope, with platform-specific field validation. [S6][S7][S11][S12]
6. **`UserScopedIdentity`** — `(platform, surface, app/account, subject_id)` plus optional UnionID linkage provenance; avoids equating OpenID/external_userid with a global WeChat user. [S9][S15][S18]
7. **`WebhookEnvelope` / `PullCursor`** — raw signed/encrypted ingress with retry/dedupe metadata; KF additionally schedules a pull command. [S3][S14]
8. **`InteractiveChoiceMessage`** — limited menu/choice abstraction can cover KF menus and some other platforms, but URL/Mini Program actions remain typed optional variants. [S13]
9. **`DeliveryStateChanged`** — accepted vs failed, with platform correlation ID. Necessary for KF and MP subscription final result; does not imply read receipt. [S11][S13][S14]
10. **`UserMediatedShare`** — an application-side capability distinct from bot sending; result means share flow outcome, not known recipient delivery. [S19]

## 10. Required namespaced extensions

- `wechat.official_account.passive_reply` — XML response, 5-second deadline and retry/ack rules are transport-specific. [S4]
- `wechat.official_account.interaction_quota` — triggers `user_message`, `menu_click`, `follow`, `qr_scan` and their 5/48h or 3/1m quotas. [S5]
- `wechat.official_account.menu` — scan/photo/location/media/article/Mini Program actions and 3×5 hierarchy. [S21]
- `wechat.official_account.follow`, `.tag`, `.publication`, `.comment` — follower and publishing models are not chat-room membership/feed parity. [S2][S8][S9]
- `wechat.mini_program.subscription_grant` — one-time vs long-term, up to five IDs per prompt, user-gesture requirement and accept/reject/ban/filter states. [S11][S12]
- `wechat.mini_program.page_target` and customer-service contact button return path/query. [S10][S11]
- `wechat.customer_service.session_state` — unhandled/AI/waiting/human/ended state machine and agent assignment. [S13][S14]
- `wechat.customer_service.event_response_code` — transient welcome/reply/ending codes are not normal reply tokens. [S14]
- `wechat.customer_service.channels_payload` — Channels product/order/content context. [S14]
- `wechat.customer_service.external_userid` and restricted profile provenance. [S15]
- `wechat.open_platform.oauth` and `.share_scene` — reviewed-app OAuth and user-mediated `session`/`timeline`/`favorite`; never map to autonomous `sendMessage`. [S17][S18][S19]

## 11. Limits, policy, review, regional and lifecycle risks

### Operational limits

- OA passive reply: 5 seconds; up to three request attempts; dedupe required. [S3][S4]
- OA asynchronous customer service: 5/48h after user message; 3/1m after qualifying menu/follow/scan. [S5]
- KF send: 5/48h after customer message, only designated automation states; text 2,048 bytes; menu ≤50 items but only 10 click/view/Mini Program; `msgid` ≤32 bytes and unique per service account. [S13]
- KF pull: recent 3 days, up to 1,000 requested items, cursor persistence mandatory; callback token 10 minutes; image/voice/video/file pull limits 2/2/10/20MB. [S14]
- OA follower page: 10,000 OpenIDs. [S8] OA temporary media: three-day expiry; voice ≤60s; page contains conflicting image limits (2MB in one note, 10M in another), so runtime validation is required. [S22]
- MP subscription prompt: ≤5 distinct-title templates; one-time/long-term types cannot mix; prompted after user action/payment callback. Guide states daily send caps of 30m for payment-enabled MP and 10m otherwise, but quotas are mutable. [S11][S12]
- OP website OAuth code: one-use/10 minutes. Mobile OAuth token ≈2h, refresh token documented 180 days; mobile guidance states 180 calls/minute per OpenID across authorization interfaces. [S17][S18]

### Review/compliance

- Some OA operations are certified-account only; current pages explicitly gate customer-service send, followers/profile, templates and subscriptions differently. Capability discovery must be per account, not by surface name. [S5][S6][S7][S8][S9]
- Templates/subscriptions are approved service notices, not marketing bypasses. Current template API exposes errors for marketing content and restricted templates. [S6]
- MP submission is reviewed against declared categories/qualifications; privacy consent, purpose limitation, deletion, content filtering and anti-abuse are required. Abuse can remove customer-service/subscription/share capabilities or the app. [S16]
- Website/mobile WeChat Login requires an Open Platform app that has passed review and separately obtained login capability. [S17][S18]
- Overseas OA template destinations lack URL jump capability according to the current template page. Treat geography/account region as capability metadata. [S6]
- Tencent can change quotas, message types and rollout states. OA AI passive reply is explicitly gray rollout; do not normalize it. [S4]

### Risk appendix — unofficial personal-account automation

- Libraries that reverse-engineer Web WeChat, hook the desktop/mobile client, simulate UI, use unauthorized protocol clients, or control personal accounts are **not official developer surfaces** and are `UNSUPPORTED`.
- The official MP rules prohibit non-public APIs and reverse engineering and sanction interface abuse. A secondary reproduction of WeChat’s software agreement also states that unauthorized third-party software/plugins and automated operations are prohibited. [S16][S23]
- Risks include account restriction/termination, sudden protocol breakage, credential/session theft, privacy and data-retention violations, and absent delivery/identity guarantees. Such connectors, if ever experimented with outside production, must be quarantined as explicitly unsafe extensions and **must not contribute any common capability**.

## 12. Conflicts and unknowns

1. **Historical OA quota conflict:** older mirrors claim unlimited sends during 48 hours. Current official `sendCustomMessage` says 5/48h and 3/1m; this report uses the live official table. [S5]
2. **OA media conflict:** the current temporary-media page says image 2MB in one note and 10M in a later format list. Exact accepted size is `UNKNOWN`; preflight conservatively and honor runtime error. [S22]
3. **MP customer-service exact window/types:** the current common send endpoint lists Mini Program applicability and 5/48h interaction rules, while the MP overview only confirms backend push. It does not independently enumerate current MP-specific triggers/types. Model the operation as `API_LIMITED` and interrogate entitlements; do not claim every OA type for MP without live validation. [S5][S10]
4. **Personal product feature completeness:** consumer client features vary by client/version/region and are irrelevant to the supported API boundary. No attempt was made to encode every client gesture.
5. **OA full event catalog:** specialized payment, card, template, publication and commerce events exist outside the core messaging pages. Keep raw event extensibility; this inventory is complete for core conversation events, not every WeChat business product. [S2]
6. **Exact universal API rate limits:** Tencent exposes quota-query APIs and endpoint errors, but no single stable rate applies across this surface. `UNKNOWN` until account/endpoint inspection. [S2]
7. **Commercial pricing:** intentionally not asserted; certification/service fees are not stable protocol contracts.
8. **Encryption semantics:** docs confirm callback encryption, not developer-controlled end-to-end encryption keys. Do not advertise E2EE API support. [S3][S4]

## 13. OBCX design implications

1. **Surface-qualified adapter IDs:** use `wechat.official_account`, `wechat.mini_program`, `wechat.customer_service`, and `wechat.open_platform`; reserve `wechat.personal` as unavailable. Never route generic WeCom operations through these adapters. [S1][S2][S13]
2. **Capability discovery over empty methods:** publish per-account entitlements for passive reply, windowed send, message types, notification kinds, user lookup, menu, media and profile fields. Certification/review/state change actual capability. [S5–S9][S13][S15]
3. **Process-owned ingress:** HTTP listener verifies signature/decrypts, emits serializable `InboundEnvelope`, tracks the 5-second OA deadline, handles ack/retry/dedupe, and sends actors typed events. Business actors must not own sockets or emit XML. [S3][S4]
4. **Separate passive and asynchronous egress DTOs:** `CompletePassiveReply{ingress_id, content}` is deadline-bound; `SendWindowedMessage{surface, account, recipient, trigger_context, content, client_id}` is quota/state-bound. Rejecting one must not silently fall back to the other without policy. [S4][S5][S13]
5. **Notification DTO is not `MessageCreate`:** `SendTemplateNotification{template_id, typed_fields, grant_ref?, destination?}` preserves consent, review and typed-field errors. [S6][S7][S11][S12]
6. **Scoped identity DTO:** `WeChatSubjectId{surface, app_id/account_id, kind: openid|external_userid|unionid, value, linkage_provenance}`. Never join two OpenIDs by value; expose UnionID only when source binding/consent permits. [S9][S15][S18]
7. **KF two-stage ingress worker:** callback handler acknowledges and emits `SyncCustomerService{open_kfid, token}`; a cursor-owning worker repeatedly pulls while `has_more`, persists `next_cursor`, and deduplicates `msgid`. Empty list does not terminate when `has_more=1`. [S14]
8. **Delivery state machine:** outbound result starts `accepted`, not `delivered`; correlate KF failure events and MP subscription final-result events. `Read` is never inferred. [S11][S13][S14]
9. **Media DTO with lifecycle:** include `media_id`, owner account, kind, MIME, size, created/expiry, and retrieval permission. Reject reuse across account/surface and refresh expiring IDs. [S14][S22]
10. **Interaction actions as tagged unions:** common `Choice`, `OpenUrl` may normalize; preserve `wechat.*.open_mini_program`, scan/photo/location, article/card and event-response-code variants. [S13][S21]
11. **Explicit absence of rooms/history/presence:** do not instantiate common room/member/thread/presence/read-receipt capabilities for WeChat merely because the consumer client has them. [S2][S14]
12. **Raw escape hatch with versioning:** retain original XML/JSON, signature/encryption metadata, documented event/type name and parser version for specialized events. Raw payload must not authorize unsupported actions. [S2][S3][S14]
13. **Quota/error telemetry:** persist platform `errcode`, `errmsg`, request ID where available, trigger/window/state and account entitlement snapshot. Do not hard-code a single global rate. [S2][S5][S13]
14. **Policy gate before egress:** message purpose (`customer_service`, `transactional_notification`, `marketing`) and consent/grant should be explicit; reject marketing through restricted templates/service windows. [S6][S16]
15. **Share is a front-end request/result capability:** represent OP share as `RequestUserMediatedShare`, never as server-side message send and never promise recipient/delivery. [S19]

## 14. Claim-to-source checklist

| Claim / design conclusion | Sources |
|---|---|
| No official personal-account bot/chat API; use app/account surfaces | S1,S2,S16 |
| OA is follower/account messaging, not room/personal DM | S1–S5,S8,S9 |
| Passive reply deadline/retry/ack differs from service send | S3,S4,S5 |
| OA current 5/48h and 3/1m quotas | S5 |
| OA templates/subscriptions are restricted notifications | S6,S7 |
| MP service conversation and consented subscriptions | S10,S11,S12 |
| KF is a separate contact-center surface with 5/48h send | S13,S14,S15 |
| KF accepted send is not final delivery | S13,S14 |
| Scoped OpenID/UnionID/external_userid design | S8,S9,S15,S18 |
| No common room/history/presence/read model | S2,S14 |
| OP OAuth/share is reviewed and user-mediated | S17,S18,S19 |
| Certification/review/privacy/anti-abuse are capability constraints | S5–S9,S16–S18 |
| Process-owned ingress and separate passive/asynchronous DTOs | S3,S4,S5,S14 |
| KF callback→pull cursor worker and delivery-state correlation | S11,S13,S14 |
| Media lifecycle/account scope | S14,S20,S22 |
| Policy-aware notification/message separation | S6,S7,S11,S12,S16 |
| Unofficial personal automation remains unsupported/risky only | S16,S23 |
