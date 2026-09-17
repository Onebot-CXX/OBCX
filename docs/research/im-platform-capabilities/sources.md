# 来源索引与证据质量

> 调研访问日：**2026-08-17**。本页不新增事实来源；下方完整 register 从十个只读 lane 的报告中抽取，并按 Wave 2/4 审计归一化 authority。原始 authority 标签仅保留在 byte-identical [`agents/`](agents/) 归档中；原始 artifact/hash/run 信息见 [`audit-manifest.json`](audit-manifest.json)。

## 1. 来源质量概览

| Lane | Source IDs | 官方/规范/本地 primary | Secondary/derived | 审计结论 |
|---|---:|---:|---:|---|
| Discord | 24 | 24 | 0 | PASS；S19–S21 为 `OFFICIAL — ACCESS-LIMITED` |
| X | 25 | 25 | 0 | PARTIAL；S19–S20 为 `OFFICIAL — ACCESS-LIMITED/INDEXED`，多个官方页面互相冲突 |
| QQ | 11 | 10 | 1 | PARTIAL；S4/S5/S7 链接过宽或可变；S8–S10 只证明 OneBot |
| 微信 | 23 | 22 | 1 | PASS；二手来源只用于非官方自动化风险附录 |
| 企业微信 | 18 | 6 | 12 | **FAIL pending re-evidence**；二手-only 结论不得进入高置信矩阵 |
| 飞书/Lark | 38 | 38 | 0 | PARTIAL；Feishu 证据不能自动证明 Lark parity |
| 钉钉 | 19 | 17 | 2 | PARTIAL；S10 `pkg.go.dev` 已归类为 `SECONDARY/DERIVED` |
| Matrix | 20 | 20 | 0 | PASS；MSC/客户端实现不等于 stable spec |
| Telegram | 15 | 15 | 0 | PASS；S1 是大型可变 reference，需保留 API 版本 |
| OBCX current | 22 | 22 local | 0 | PARTIAL；仅静态本地证据，未执行 runtime/conformance tests |
| **Web 合计** | **193** | **177** | **16** | 一手比例 91.7%，但不衡量链接直接性/当前性 |
| **含本地证据** | **215** | **199** | **16** | 本地文件证据不能证明外部平台能力 |

权威标签应理解为：`SPEC — STABLE`、`SPEC — UNSTABLE MSC`、`OFFICIAL PRODUCT HELP`、`OFFICIAL DEVELOPER DOC`、`OFFICIAL SDK SOURCE`、`OFFICIAL — ACCESS-LIMITED`、`OFFICIAL — ACCESS-LIMITED/INDEXED`、`SECONDARY MIRROR`、`SECONDARY/DERIVED`、`LOCAL CODE EVIDENCE`。官方域名不自动等于当前、完整或无冲突。

## 2. 高影响证据入口

| 范围 | 关键 IDs | 支撑/边界 | 入口 |
|---|---|---|---|
| Discord | S2,S6,S10,S12,S13 | Gateway/intents、message、interaction deadline、OAuth/self-bot 边界、Event Webhook | [报告 source register](agents/discord.md#4-source-register) |
| X | S4,S7,S9,S12,S13,S19 | auth、post、legacy DM、Activity/AAA、XChat 产品/API 边界 | [报告 source register](agents/x.md#4-source-register) |
| QQ | S2,S3,S4,S5,S8-S10 | token、event/intent、SDK、active-send 冲突、OneBot 非官方边界 | [报告 source register](agents/qq.md#4-source-register) |
| 微信 | S2-S5,S13-S15,S19 | 个人号边界、OA callback/send、微信客服、用户介导 share | [报告 source register](agents/wechat.md#4-source-register) |
| 企业微信 | S1,S3-S5,S7,S12 | token、group robot、IR、appchat、archive 的官方证据 | [报告 source register](agents/wecom.md#4-source-register) |
| 飞书/Lark | S1-S2,S8,S20,S28-S29 | environment、send、card、event delivery；无 parity 保证 | [报告 source register](agents/lark.md#4-source-register) |
| 钉钉 | S3,S5,S7-S9,S17 | auth/app robot、HTTP/Stream ingress、card；S10 仅 derived | [报告 source register](agents/dingtalk.md#4-source-register) |
| Matrix | S3,S4,S11,S15 | stable Client-Server、Application Service、E2EE、unstable poll MSC | [报告 source register](agents/matrix.md#4-source-register) |
| Telegram | S1,S8,S9,S15 | Bot API、FAQ boundary、TDLib/client authority、2026 新特性 | [报告 source register](agents/telegram.md#4-source-register) |
| OBCX | F1,F6,F13,F17-F18,F20-F21 | giant `IBot`、TG stubs、actor envelope、ownership、Bridge direct call | [本地 file register](agents/obcx-current.md#sourcefile-register) |

## 3. 审计处置规则

1. 企业微信 S2、S6、S8–S11、S13–S18 为 secondary；未被官方来源独立支持的矩阵格保持 `UNKNOWN`。微信客服以微信 lane S13–S15 为 canonical evidence。
2. QQ S5 只是通用 repository root，不能单独证明 active send 当前已撤销；该能力保持 `UNKNOWN`、默认关闭。S8–S10 是 OneBot 规范 primary，但不是腾讯 API primary。
3. DingTalk S10 的 host 是第三方 Go package index；recall/read/download/status 等 SDK-derived 结论必须限定或重新取证。
4. Lark/Feishu 的 overview omission 不足以证明 unsupported；presence、typing、call 等保持 `UNKNOWN`。
5. Matrix stable spec、optional module/profile、unstable MSC 与 implementation behavior 分层记录。
6. Discord S19–S21 标为 `OFFICIAL — ACCESS-LIMITED`；X S19–S20 标为 `OFFICIAL — ACCESS-LIMITED/INDEXED`，均不支撑高置信细节。
7. moving branch/repository-root 链接在实施前应换为 tag/commit permalink；数字限额进入动态 constraint，不成为稳定 DTO 常量。

完整审计见 [`reviews/evidence-audit.md`](reviews/evidence-audit.md)。

## 4. 完整 lane registers

每个子节定义该表的 `platform/surface`；表内保留 lane 原始 source ID、title、URL/path、访问日和 relevant claim，但 authority 采用审计后的归一化标签。原始 authority 只在 `agents/` 归档可见；以下内容不替代原报告中的冲突/置信度说明。

### 4.1 Discord

- Surface: `discord.bot_api`
- 原报告：[`agents/discord.md`](agents/discord.md)

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
| S19 | OFFICIAL — ACCESS-LIMITED | Forum Channels FAQ | https://support.discord.com/hc/en-us/articles/6208479917079-Forum-Channels-FAQ | Product forums organize topic posts and require Community; direct fetch was 403. |
| S20 | OFFICIAL — ACCESS-LIMITED | Polls FAQ | https://support.discord.com/hc/en-us/articles/22163184112407-Polls-FAQ | Product polls support up to 10 answers and permission control; direct fetch was 403. |
| S21 | OFFICIAL — ACCESS-LIMITED | Automated User Accounts (Self-Bots) | https://support.discord.com/hc/en-us/articles/115002192352-Automated-User-Accounts-Self-Bots | Normal-user automation is forbidden and may cause termination; direct fetch was 403. |
| S22 | OFFICIAL | Updated Requirements to How Apps Access Data in Servers | https://discord.com/blog/updated-requirements-to-how-apps-access-data-in-servers | New 10,000-visible-user privileged-intent review and annual re-review framing. |
| S23 | OFFICIAL | API Reference — versioning, snowflakes, uploads, errors | https://docs.discord.com/developers/reference | API v10, serializable error model, snowflake cursors, multipart uploads and per-file limits. |
| S24 | OFFICIAL | Component Reference — Components V2 | https://docs.discord.com/developers/components/reference | Buttons/selects/text inputs/layout/media/file components and `IS_COMPONENTS_V2` restrictions. |

### 4.2 X

- Surface: `x.posts` / `x.legacy_dm` / `x.xchat_activity`
- 原报告：[`agents/x.md`](agents/x.md)

All sources accessed 2026-08-17.

| ID | Authority | Title / relevant heading | URL | Evidence (precise paraphrase/excerpt) |
|---|---|---|---|---|
| S1 | OFFICIAL | **X API Overview** — endpoint catalog/tiering | https://docs.x.com/x-api/overview | Catalogs Posts, Users, DMs, Spaces, Lists, Media, streaming and webhooks; says pay-per-use unless Enterprise-only; separately labels Account Activity and Stream Webhooks Enterprise-only. |
| S2 | OFFICIAL | **X API pay-per-usage pricing and credits** — credit consumption | https://docs.x.com/x-api/getting-started/pricing | “pay-per-usage”; reads per resource, writes/actions per request; 24-hour UTC resource dedupe, credit balance/spend controls, activity-event billing. |
| S3 | OFFICIAL | **X API Rate Limits** | https://docs.x.com/x-api/fundamentals/rate-limits | Endpoint/app/user windows, `x-rate-limit-*` headers and 429 behavior; examples used below for posts, DMs, Lists, graph and media. |
| S4 | OFFICIAL | **X API v2 authentication mapping** | https://docs.x.com/fundamentals/authentication/guides/v2-authentication-mapping | Maps endpoints to OAuth 1.0a, app-only, PKCE, and scopes; also contains stale Academic-only full-archive wording. |
| S5 | OFFICIAL | **Search Posts** — recent/full archive | https://docs.x.com/x-api/posts/search/introduction | Recent search is last 7 days/up to 100; full archive since March 2006/up to 500 and currently described as pay-per-use or Enterprise. |
| S6 | OFFICIAL | **Filtered Stream** — introduction | https://docs.x.com/x-api/posts/filtered-stream/introduction | Persistent near-real-time rule-matching post stream; rules/connections/operators vary by tier. |
| S7 | OFFICIAL | **Create Posts** — request schema | https://docs.x.com/x-api/posts/create-post | `POST /2/tweets` schema includes text, reply, quote ID, media, poll, reply settings, Community ID, edit options and other X-specific flags. |
| S8 | OFFICIAL | **Manage Posts integration guide** — self-serve restrictions/rates | https://docs.x.com/x-api/posts/manage-tweets/integrate | User-context auth; summoned-only replies and one-cashtag self-serve limits; 200 POST/15m, 50 DELETE/15m and combined 300/3h; source label. Contains stale media sentence. |
| S9 | OFFICIAL | **Manage Direct Messages integration guide** | https://docs.x.com/x-api/direct-messages/manage/integrate | 1:1/group/send/delete endpoints; user auth/scopes; text or attachment; one photo/video/GIF; shared post by URL. Contains contradictory v1.1 deletion sentence. |
| S10 | OFFICIAL | **Direct Messages Lookup** — introduction | https://docs.x.com/x-api/direct-messages/lookup/introduction | All/conversation/participant event lookup; MessageCreate/ParticipantsJoin/ParticipantsLeave; up to 30 days; cursor pagination. |
| S11 | OFFICIAL | **Manage Direct Messages** — introduction | https://docs.x.com/x-api/direct-messages/manage/introduction | Current v2 create conversation/send/delete operation catalog. |
| S12 | OFFICIAL | **Account Activity API v2** — activity types/tier table | https://docs.x.com/x-api/account-activity/introduction | Bundled webhook events for posts/deletes/mentions/replies/reposts/quotes/likes/follows/blocks/mutes/legacy DMs/typing/read/revoke; says pay-per-use 3 subscriptions/1 webhook, Enterprise 5,000+/5+. |
| S13 | OFFICIAL | **X Activity API** — events/privacy/subscriptions | https://docs.x.com/x-api/activity/introduction | HTTP stream or webhook; typed post/social/profile/Space/privacy/DM/XChat events; public vs OAuth-private rules; self-serve 1,500, Enterprise 75k, Partner 150k subscriptions. |
| S14 | OFFICIAL | **Webhooks quickstart** — CRC/security/management | https://docs.x.com/x-api/webhooks/quickstart | Public HTTPS, no explicit port, initial/hourly CRC, HMAC-SHA256 POST signature; create/list/delete/validate webhook with app-only bearer. |
| S15 | OFFICIAL | **OAuth 2.0 Authorization Code with PKCE** | https://docs.x.com/fundamentals/authentication/oauth-2-0/authorization-code | Fine-grained delegated user scopes; `offline.access` enables refresh tokens. |
| S16 | OFFICIAL | **Media introduction / best practices** | https://docs.x.com/x-api/media/introduction | Simple/chunked upload, categories and async processing; media format/size guidance. |
| S17 | OFFICIAL | **Initialize media upload** | https://docs.x.com/x-api/media/initialize-media-upload | `POST /2/media/upload/initialize`, total bytes/type/category and chunked flow; categories include post, DM and subtitles. |
| S18 | OFFICIAL | **Spaces endpoints on X API v2** — availability/lifecycle/roles | https://docs.x.com/x-api/spaces/introduction | Lookup/search only; live/scheduled lifecycle, ended-space removal, creator/hosts/speakers/listener aggregates; no participation operations listed. |
| S19 | OFFICIAL — ACCESS-LIMITED/INDEXED | **About Chat** — product encryption/features | https://help.x.com/en/using-x/about-chat | Product XChat: encrypted messages/media/files/reactions, groups, unsend/disappearing messages/voice notes; metadata not encrypted and no forward secrecy. Direct fetch 403; indexed official content only. |
| S20 | OFFICIAL — ACCESS-LIMITED/INDEXED | **Audio and Video Calls** | https://help.x.com/en/using-x/direct-messages/audio-video-calls | Product 1:1 and group calls; platform-dependent group availability; group calls not E2E encrypted. Direct fetch 403; indexed official content only. |
| S21 | OFFICIAL | **X Developer Policy / Automation rules** | https://docs.x.com/developer-terms/policy | Consent/opt-out and anti-spam obligations; no aggressive/bulk automation or substantially duplicate cross-account content; bot disclosure requirements. |
| S22 | OFFICIAL | **Edit Posts** — controls/history | https://docs.x.com/x-api/fundamentals/edit-posts | Edit chain/IDs, eligibility controls, limited edit window/count, and non-editable post classes. |
| S23 | OFFICIAL | **Follows / Blocks / Mutes docs** | https://docs.x.com/x-api/users/follows/introduction | Graph lookup and follow/unfollow; cross-referenced current Blocks/Mutes endpoint family and user-context restrictions. |
| S24 | OFFICIAL | **Bookmarks** — introduction | https://docs.x.com/x-api/posts/bookmarks/introduction | Private authenticated-user bookmark lookup/add/remove operations. |
| S25 | OFFICIAL | **Lists** — current endpoint family | https://docs.x.com/x-api/lists/manage-lists/introduction | Create/update/delete Lists and separate membership/follow/pin/timeline operations. |

### 4.3 QQ

- Surface: `qq.official` / `onebot11.qq`
- 原报告：[`agents/qq.md`](agents/qq.md)

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

### 4.4 微信

- Surface: `wechat.official_account` / `.mini_program` / `.customer_service` / `.open_platform`
- 原报告：[`agents/wechat.md`](agents/wechat.md)

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

### 4.5 企业微信

- Surface: `wecom.internal_app` / `.appchat` / `.group_webhook` / `.intelligent_robot` / `.customer_contact` / `.archive`
- 原报告：[`agents/wecom.md`](agents/wecom.md)

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

### 4.6 飞书/Lark

- Surface: `lark.feishu` / `lark.international`
- 原报告：[`agents/lark.md`](agents/lark.md)

All sources accessed 2026-08-17.

| ID | Authority | Title / relevant section | URL | What it proves (precise paraphrase) |
|---|---|---|---|---|
| S1 | OFFICIAL | Feishu SDK: Call server API / domain | https://open.feishu.cn/document/uAjLw4CM/ukTMukTMukTM/server-side-sdk/python--sdk/invoke-server-api?lang=en-US | SDK selects Feishu vs Lark API domain; app ID/secret configure client. |
| S2 | OFFICIAL | Lark server API list | https://open.larksuite.com/document/ukTMukTMukTM/uYTM5UjL2ETO14iNxkTN/server-api-list | Lark's official API catalog and `/open-apis` surface. |
| S3 | OFFICIAL | App types: custom and store apps | https://open.feishu.cn/document/home/app-types-introduction/self-built-apps-and-store-apps | Custom apps are tenant-specific; store apps are marketplace distributed. |
| S4 | OFFICIAL | API scopes / request permissions | https://open.feishu.cn/document/server-docs/application-scope/introduction?lang=en-US | Permission requests and tenant/platform approval gates. |
| S5 | OFFICIAL | API terminology / access tokens | https://open.feishu.cn/document/server-docs/api-call-guide/terminology?lang=zh-CN | Tenant token represents app in tenant and is short lived; user token represents user. |
| S6 | OFFICIAL | Configure app data permissions | https://open.feishu.cn/document/home/introduction-to-scope-and-authorization/configure-app-data-permissions | Sensitive resource range is an additional gate beyond API scope. |
| S7 | OFFICIAL | Custom bot usage guide | https://open.feishu.cn/document/ukTMukTMukTM/ucTM5YjL3ETO24yNxkjN?lang=zh-CN | One-group outbound webhook; no events/data permissions; 100/min, 5/sec, 20 KB; signature/IP/keyword; static cards. |
| S8 | OFFICIAL | Send message (`POST /im/v1/messages`) | https://open.feishu.cn/document/server-docs/im-v1/message/create?lang=zh-CN | Send types, identifiers, `uuid`, payload and rate limits. |
| S9 | OFFICIAL | Reply message | https://open.feishu.cn/document/server-docs/im-v1/message/reply?lang=zh-CN | Native reply, `reply_in_thread`, send limits and idempotency. |
| S10 | OFFICIAL | List chat history | https://open.feishu.cn/document/server-docs/im-v1/message/list?lang=zh-CN | Chat/thread containers, time/sort/pagination; thread retrieval behavior. |
| S11 | OFFICIAL | Edit message | https://open.feishu.cn/document/server-docs/im-v1/message/update | Only own text/post; max 20; not recalled/expired. |
| S12 | OFFICIAL | Recall message | https://open.feishu.cn/document/server-docs/im-v1/message/delete?lang=zh-CN | Recall semantics, ownership/role and age constraints. |
| S13 | OFFICIAL | Forward message | https://open.feishu.cn/document/server-docs/im-v1/message/forward | Forward API and excluded message types. |
| S14 | OFFICIAL | Receive-message content structures | https://open.feishu.cn/document/server-docs/im-v1/message-content-description/message_content | Readable types include vote/video_chat/system/location beyond outbound types. |
| S15 | OFFICIAL | Reaction create / event | https://open.feishu.cn/document/server-docs/im-v1/message-reaction/create?lang=zh-CN | Add reaction API and chat/recalled/system constraints. |
| S16 | OFFICIAL | Pin/list pins | https://open.feishu.cn/document/server-docs/im-v1/pin/list | Paginated pinned messages for a chat; bot must belong. |
| S17 | OFFICIAL | Chat/member overview and add members | https://open.feishu.cn/document/server-docs/group/chat-member/create?lang=en-US | Add up to 50 users or 5 bots; role/invite restrictions and serialization caveat. |
| S18 | OFFICIAL | Update chat moderation | https://open.feishu.cn/document/server-docs/group/chat/update | Posting policies and owner/creator/operate-as-owner rules. |
| S19 | OFFICIAL | Message read event | https://open.feishu.cn/document/server-docs/im-v1/message/events/message_read?lang=zh-CN | Read event limited to bot-sent P2P; reader query limitations. |
| S20 | OFFICIAL | Card callback communication | https://open.feishu.cn/document/feishu-cards/card-callback-communication?lang=zh-CN | `card.action.trigger`, 3-second response, action/form values and update token. |
| S21 | OFFICIAL | Card JSON 2.0 release notes | https://open.feishu.cn/document/feishu-cards/card-json-v2-breaking-changes-release-notes | 14-day interactivity/update lifetime and JSON 2.0 constraints. |
| S22 | OFFICIAL | Card input/form component | https://open.feishu.cn/document/feishu-cards/card-json-v2-components/interactive-components/input | Named form fields, grouped submit, required validation. |
| S23 | OFFICIAL | Upload image | https://open.feishu.cn/document/server-docs/im-v1/image/create | Multipart IM image upload, formats, 10 MB, returns `image_key`. |
| S24 | OFFICIAL | Upload file | https://open.feishu.cn/document/uAjLw4CM/ukTMukTMukTM/reference/im-v1/file/create | Upload audio/video/file and receive `file_key`. |
| S25 | OFFICIAL | Get resource from message | https://open.feishu.cn/document/server-docs/im-v1/message/get-2?lang=zh-CN | Message-bound image/audio/video/file binary retrieval, chat membership, 100 MB. |
| S26 | OFFICIAL | Contact permission range | https://open.feishu.cn/document/server-docs/contact-v3/scope/scope_authority?lang=zh-CN | App data scope vs user org-visibility scope; department inheritance. |
| S27 | OFFICIAL | Get user | https://open.feishu.cn/document/server-docs/contact-v3/user/get | User/field permission and out-of-scope behavior. |
| S28 | OFFICIAL | Event overview | https://open.feishu.cn/document/server-docs/event-subscription-guide/overview?from=from_parent_docs | HTTPS POST and SDK long-connection/WebSocket delivery. |
| S29 | OFFICIAL | Configure/subscription event case | https://open.feishu.cn/document/server-docs/event-subscription-guide/event-subscription-configure-/subscription-event-case?lang=zh-CN | V2 `event_id`, app/user identity visibility, v1+v2 duplicate warning. |
| S30 | OFFICIAL | Calendar event create | https://open.feishu.cn/document/server-docs/calendar-v4/calendar-event/create | App/user CRUD, calendar role and idempotency requirements. |
| S31 | OFFICIAL | Subscribe calendar changes | https://open.feishu.cn/document/server-docs/calendar-v4/calendar/subscription?lang=zh-CN | Calendar subscriptions require user token; endpoint-specific limits. |
| S32 | OFFICIAL | Docx overview | https://open.feishu.cn/document/server-docs/docs/docs/docx-v1/docx-overview | Documents are block trees with read/mutation APIs. |
| S33 | OFFICIAL | Docs permission overview | https://open.feishu.cn/document/server-docs/docs/permission/overview | Scope alone does not grant document access; resource ACL/application grant applies. |
| S34 | OFFICIAL | Subscribe cloud-document events | https://open.feishu.cn/document/server-docs/docs/drive-v1/event/subscribe?lang=zh-CN | Per-resource owner/manager subscription and event limits. |
| S35 | OFFICIAL | Bot overview | https://open.feishu.cn/document/uAjLw4CM/ukTMukTMukTM/bot-v3/bot-overview | Documented bot surface: conversations, messages/cards, custom menus; no presence/typing/call API. |
| S36 | OFFICIAL | IM event list | https://open.feishu.cn/document/ukTMukTMukTM/uYDNxYjL2QTM24iN0EjN/event-list | Enumerates IM/chat/contact event names. |
| S37 | OFFICIAL | Create chat / group overview | https://open.feishu.cn/document/uAjLw4CM/ukTMukTMukTM/reference/im-v1/chat/create | API-created group and chat/thread message form; topic-group creation limitation. |
| S38 | OFFICIAL | Message overview / urgent APIs | https://open.feishu.cn/document/uAjLw4CM/ukTMukTMukTM/reference/im-v1/introduction | In-app/SMS/phone urgent APIs, batch operations and reader/card-related inventory. |

### 4.7 钉钉

- Surface: `dingtalk.app_robot` / `.group_webhook`
- 原报告：[`agents/dingtalk.md`](agents/dingtalk.md)

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
| S10 | SECONDARY/DERIVED | Alibaba Cloud DingTalk Go SDK `robot_1_0` generated API | https://pkg.go.dev/github.com/alibabacloud-go/dingtalk/v2/robot_1_0 | 2026-08-17 | `BatchSendOTO`, group/private send/query/recall, read-query result, message-file download, partial failure lists. |
| S11 | OFFICIAL | *创建部门* and *添加角色组* | https://open.dingtalk.com/document/development/address-book-creation-department-established-department ; https://open.dingtalk.com/document/development/add-a-role-group | 2026-08-17 | Official department and role-group management surfaces exist. |
| S12 | OFFICIAL | *主数据回调事件* | https://open.dingtalk.com/document/orgapp-server/primary-data-callback | 2026-08-17 | Contact lifecycle event family; user add/modify/leave and department create/modify/remove. |
| S13 | OFFICIAL | *群自定义机器人概述* and official access page | https://open-dingtalk.github.io/developerpedia/docs/learn/bot/webhook/overview/ ; https://open.dingtalk.com/document/orgapp/custom-robot-access | 2026-08-17 | Custom bot is outbound group-only/no inbound/no DM; lifecycle warning; official security configuration entry. |
| S14 | SECONDARY | DingTalk API mirror — *调用频率限制* | https://dingtalk.apifox.cn/doc-392370 | 2026-08-17 | Candidate numeric global/IP/app/custom-bot limits; requires primary verification. |
| S15 | OFFICIAL | *Privacy Policy of DingTalk* — transfer/storage provisions | https://www.dingtalk.com/en/privacy_policy | 2026-08-17 | Cross-border handling is conditional on applicable legal requirements/notice/consent. |
| S16 | OFFICIAL | *应用市场开通授权* | https://open-dingtalk.github.io/developerpedia/docs/learn/permission/manage/app-store-consent/ | 2026-08-17 | Tenant administrator grants requested app permissions; later scopes need incremental consent. |
| S17 | OFFICIAL | *进阶：卡片回调* — callback parameters and update example | https://open-dingtalk.github.io/developerpedia/docs/explore/tutorials/stream/bot/go/card-callback/ | 2026-08-17 | Button return-request parameters, Stream callback processing, card-variable update, group/robot spaces. |
| S18 | OFFICIAL | *获取用户通讯录个人信息* | https://open.dingtalk.com/document/isvapp/dingtalk-retrieve-user-information | 2026-08-17 | User profile fields are scope-controlled (`Contact.User.Read`; mobile has a separate sensitive scope). |
| S19 | OFFICIAL | *感知群变化（事件订阅）* | https://open.dingtalk.com/document/org/group-change-awareness-event-subscription | 2026-08-17 | Group member add/remove, disband, owner change, title change subscription family. |

### 4.8 Matrix

- Surface: `matrix.client_server` / `.application_service`
- 原报告：[`agents/matrix.md`](agents/matrix.md)

Accessed 2026-08-17 UTC.

| ID | Authority | Title / relevant section | URL | What it proves (precise paraphrase/excerpt) |
|---|---|---|---|---|
| S1 | SPEC | Matrix Specification, overview | https://spec.matrix.org/latest/ | Stable specification entry point and protocol scope. |
| S2 | SPEC | v1.19 Changelog | https://spec.matrix.org/v1.19/changelog/v1.19/ | v1.19 release date; mutual rooms, image packs and encrypted-history sharing became stable. |
| S3 | SPEC | Client-Server API — API versions; Events; Syncing; Rooms; Modules/Feature Profiles; authentication | https://spec.matrix.org/v1.19/client-server-api/ | Core HTTP/event model, 64 KiB event limit, version discovery, long-poll sync, room membership/power, redaction, legacy/OAuth auth, normative module/profile distinction. |
| S4 | SPEC | Application Service API — Registration; Pushing events/ephemeral data; Identity assertion; CS extensions | https://spec.matrix.org/v1.19/application-service-api/ | Admin YAML registration, namespaces/tokens, interested-event transactions, retries/deduplication, optional ephemeral delivery, virtual-user/device impersonation and AS constraints. |
| S5 | SPEC | Client-Server API — Direct Messaging | https://spec.matrix.org/v1.19/client-server-api/#direct-messaging | All communication occurs in rooms; `m.direct` is account-data marking and does not impose exactly two participants. |
| S6 | SPEC | Client-Server API — Spaces | https://spec.matrix.org/v1.19/client-server-api/#spaces | `m.space` room type, `m.space.child`/`m.space.parent`, hierarchy API and cycle/visibility caveats. |
| S7 | SPEC | Client-Server API — Threading; Receipts | https://spec.matrix.org/v1.19/client-server-api/#threading | Stable `m.thread`, non-nesting/root semantics, thread aggregation/list endpoint and threaded receipts. |
| S8 | SPEC | Client-Server API — Rich replies; Event replacements | https://spec.matrix.org/v1.19/client-server-api/#rich-replies | `m.in_reply_to`; stable `m.replace`, validity rules, immutable original and client-side application. |
| S9 | SPEC | Client-Server API — Event annotations and reactions | https://spec.matrix.org/v1.19/client-server-api/#event-annotations-and-reactions | `m.annotation`/`m.reaction`, dedup/count semantics and redact-to-remove. |
| S10 | SPEC | Client-Server API — Forming relationships; Reference relations | https://spec.matrix.org/v1.19/client-server-api/#forming-relationships-between-events | Relations are child events; cleartext relation metadata, aggregation and relations endpoints. |
| S11 | SPEC | Client-Server API — End-to-End Encryption | https://spec.matrix.org/v1.19/client-server-api/#end-to-end-encryption | Optional E2EE; key APIs, device lists, Olm/Megolm, cross-signing, backups, to-device traffic and encrypted attachment procedure. |
| S12 | SPEC | Server-Server API — overview/API standards | https://spec.matrix.org/v1.19/server-server-api/ | Homeserver federation over HTTPS/signatures; PDUs, EDUs, queries and server-to-server transactions. |
| S13 | SPEC | Client-Server API — Content repository | https://spec.matrix.org/v1.19/client-server-api/#content-repository | `mxc://`, upload/download/thumbnail behavior, authenticated-media transition and policy-controlled size/security handling. |
| S14 | OFFICIAL | matrix-org/matrix-widget-api README | https://github.com/matrix-org/matrix-widget-api | Official SDK states: “Widgets are not yet in the Matrix spec,” so portability/support is not guaranteed. |
| S15 | OFFICIAL / MSC (UNSTABLE) | MSC3381: Polls | https://github.com/matrix-org/matrix-spec-proposals/pull/3381 | Proposal-level poll event semantics; not evidence of a v1.19 normative module. |
| S16 | SPEC | v1.18 Changelog | https://spec.matrix.org/v1.18/changelog/v1.18/ | Stable lock/suspend, invite blocking, policy server, account limit, OAuth device flow and animated-media additions. |
| S17 | SPEC | Client-Server API — Presence; Typing; Receipts; Push; Reporting | https://spec.matrix.org/v1.19/client-server-api/#modules | Normative ephemeral and notification modules and moderation report behavior. |
| S18 | SPEC | Identity Service API — General principles; Privacy; Authentication; Lookup | https://spec.matrix.org/v1.19/identity-service-api/ | Optional third-party identifier mapping service, independent tokens, oracle trust and privacy limits. |
| S19 | SPEC | Client-Server API — Instant Messaging; mentions; stickers; image packs | https://spec.matrix.org/v1.19/client-server-api/#instant-messaging | Message types/rich HTML/media captions, `m.mentions`, stickers and v1.19 image-pack semantics. |
| S20 | SPEC | Client-Server API — Moderation policy lists; server administration | https://spec.matrix.org/v1.19/client-server-api/#moderation-policy-lists | Policy-rule data structures leave enforcement implementation-defined; server-admin APIs are a distinct module. |

**Dropped:** SEO summaries, client-specific feature pages, unofficial bot automation, old r0.x specs, and DeepWiki/npm mirrors—unnecessary or weaker than primary specification evidence.

### 4.9 Telegram

- Surface: `telegram.bot_api` / `telegram.tdlib_client`
- 原报告：[`agents/telegram.md`](agents/telegram.md)

| ID | Authority | Title | URL | Relevant section / precise evidence (accessed 2026-08-17) |
|---|---|---|---|---|
| S1 | OFFICIAL | Telegram Bot API 10.2 | https://core.telegram.org/bots/api | “Authorizing your bot,” “Getting updates,” `Update`, messaging, chat administration, stickers and files. Token-authenticated HTTPS; long polling/webhook exclusivity; 24-hour queue; operations, rights and limits. |
| S2 | OFFICIAL | Bots: An introduction for developers | https://core.telegram.org/bots | “How do bots work?” / “How are bots different from users?” Bots are special accounts, cannot start user conversations, have no online status, and use a token. |
| S3 | OFFICIAL | Telegram Bot Features | https://core.telegram.org/bots/features | “Inputs,” “Inline Requests,” “Mini Apps,” “Secretary Bots,” “Privacy Mode.” Defines commands/buttons, inline enablement, business access, and group-message visibility. |
| S4 | OFFICIAL | Telegram Mini Apps | https://core.telegram.org/bots/webapps | “Implementing Mini Apps,” launch modes, validation and attachment menu. Direct/inline Mini Apps cannot read chat or send without active user flow; attachment menu is restricted; validate `initData`. |
| S5 | OFFICIAL | Inline Bots | https://core.telegram.org/bots/inline | Inline query/results and feedback. User invokes `@bot query` in any chat; feedback can be sampled. |
| S6 | OFFICIAL | Bot Payments API (physical goods/services) | https://core.telegram.org/bots/payments | Provider-token flow, invoices, shipping/pre-checkout, third-party processing, 10-second pre-checkout deadline. |
| S7 | OFFICIAL | Payments for Digital Goods and Services | https://core.telegram.org/bots/payments-stars | Digital purchases require `XTR` Telegram Stars; invoice, `pre_checkout_query`, `successful_payment`, refund/support flow. |
| S8 | SPEC | TDLib: Getting Started | https://core.telegram.org/tdlib/getting-started | TDLib is a fully functional asynchronous client; four chat types, user authorization, updates, files and explicit `getChatHistory` pagination. |
| S9 | OFFICIAL | Telegram APIs | https://core.telegram.org/api | Explicitly separates simplified Bot API from Telegram API/TDLib customized clients; lists user authorization, contacts, calls, read metrics, stories, secret chats and admin log as client API features. |
| S10 | SPEC | Channels, supergroups, gigagroups and basic groups | https://core.telegram.org/api/channel | Product/client topology: basic groups ≤200, supergroups ≤200,000, unlimited broadcast channels, migration, rights and admin-log links. |
| S11 | SPEC | Admin log | https://core.telegram.org/api/recent-actions | `channels.getAdminLog` is a Telegram API/MTProto operation for supergroups/channels—not an ordinary Bot API method. |
| S12 | OFFICIAL | Bot Platform Developer Terms | https://telegram.org/tos/bot-developers | Availability, privacy/retention, anti-scraping, rate-limit circumvention, business-bot duties and digital/physical payment rules. |
| S13 | OFFICIAL | Telegram FAQ | https://telegram.org/faq | Product limits/features: groups up to 200,000, unlimited-audience channels, cloud chats, calls/read marks, 2 GB product file baseline, and secret-chat distinction. Page itself warns some content may be outdated. |
| S14 | SPEC | Forums | https://core.telegram.org/api/forum | Group and bot forum topology, topic rights and identifiers. Useful to distinguish product/MTProto details from Bot API topic methods. |
| S15 | OFFICIAL | Bot API changelog | https://core.telegram.org/bots/api-changelog | Current lifecycle evidence: Bot API 10.2 (2026-07-14), 10.1 rich messages, 10.0 guest/bot-to-bot/poll changes, 9.4 private topics. |

**Dropped / down-weighted sources**

- **Bot FAQ** (https://core.telegram.org/bots/faq) — retained only for rate-limit corroboration and as conflict evidence; its categorical “bots cannot see other bots” statement is stale against Bot API 10.0/current Bot Features.
- Search-provider summaries — dropped because searches failed and primary documentation was directly accessible.
- Community SDK documentation, blogs and “userbot” libraries — dropped as unnecessary and inappropriate for establishing official capability.

### 4.10 OBCX current

- Surface: local code evidence
- 原报告：[`agents/obcx-current.md`](agents/obcx-current.md)

| ID | File and exact range | What it proves |
|---|---|---|
| F1 | `include/interfaces/bot.hpp:24-379` | Entire `IBot` contract and protected transport/runtime ownership. |
| F2 | `include/interfaces/qq_bot.hpp:8-35` | Stable QQ-specific extension. |
| F3 | `include/interfaces/telegram_bot.hpp:14-170` | Telegram DTOs and optional platform capability interfaces/default fallbacks. |
| F4 | `include/core/qq_bot.hpp:16-350` | QQ implements `IBot` + `IQQBot`; OneBot-centric full implementation declaration. |
| F5 | `include/core/tg_bot.hpp:17-421` | Telegram forced to implement base plus Telegram extensions. |
| F6 | `src/core/tg_bot.cpp:20-504` | HTTP-only connection, real methods, delete encoding, and unsupported/synthetic methods. |
| F7 | `src/core/qq_bot.cpp:10-178` | QQ connection callback and request/echo/response flow. |
| F8 | `include/common/message_type.hpp:44-178,180-219` | OneBot event hierarchy/variant and shared connection config. |
| F9 | `src/onebot11/adapter/event_converter.cpp:10-67` | OneBot post-type mapping. |
| F10 | `src/telegram/adapter/protocol_adapter.cpp:51-173` | Telegram recognized update types and message normalization/raw preservation. |
| F11 | `include/core/event_dispatcher.hpp:22-108` | Typed handlers, variant dispatch, detached coroutine scheduling on bot I/O executor. |
| F12 | `src/core/runtime/message_event_ingress.cpp:21-174` | Conversation identity and raw message/notice envelope construction. |
| F13 | `include/core/actor/actor.hpp:81-110,174-284` | Envelope/result/services/context and Asio/blocking crossings. |
| F14 | `include/core/bot_registry.hpp:15-107` | Account keys, weak ownership, ambiguity behavior. |
| F15 | `tests/cpp/bot_registry_test.cpp:9-58` | Multi-account, unambiguous, expired and unregister behavior tests. |
| F16 | `src/common/component_manager.cpp:21-61,68-170,173-190` | Bot factory, connection selection/config and private `connect` setup. |
| F17 | `src/interfaces/bot.cpp:7-57` | Per-bot I/O/dispatcher creation and strict destruction ordering. |
| F18 | `src/app/main.cpp:284-418,420-510` | Process registry, bot ownership/threads, ingress callbacks, reload and shutdown sequence. |
| F19 | `local_actor/actor-registry/actors/bridge.toml:1-20` | Bridge actor identity/dependency/publication metadata. |
| F20 | `local_actor/obcx-actor-bridge/actor/bridge_actor.cpp:104-250` | Runtime service resolution, Asio crossing, accepted message and failures. |
| F21 | `local_actor/obcx-actor-bridge/dependency/bridge_forwarding_runtime.cpp:13-112` | Platform routing, registry lookup, direct handler calls and mapping requirement. |
| F22 | `local_actor/obcx-actor-bridge/tests/bridge_actor_test.cpp:156-210` | Injected forwarder seam and emitted/persisted mapping behavior. |
