# 九个平台产品/官方 API 能力矩阵

> 基线：2026-08-17。矩阵是调研快照，不是长期平台承诺。完整逐项证据在各[平台报告 §6](agents/discord.md#6-capability-evidence-table)；本页对第二轮[证据审计](reviews/evidence-audit.md)指出的问题进行了保守修正。

## 1. 读法、状态与平台 surface

每格同时给出 `P:`（终端产品）与 `A:`（官方自动化/API）状态；括号内是对应平台报告的 source ID。产品有功能不意味着 `A` 可用。

| 缩写 | 状态 |
|---|---|
| `N` | `NATIVE`：产品/协议与官方 API 具有对齐的原生语义 |
| `L` | `API_LIMITED`：官方 API 存在，但受 scope、权限、套餐、审核、场景、窗口、region 或实现 profile 限制 |
| `E` | `EMULATED`：adapter 可明确降级组合，且必须报告语义损失 |
| `X` | `EXTENSION`：只能进入平台命名空间 |
| `U` | `UNSUPPORTED`：官方边界或完整规范明确不支持 |
| `?` | `UNKNOWN`：证据缺失、冲突、不可访问，或仅由 overview omission 推断 |

平台列：`DC` Discord bot API；`X` X posts/legacy DM/XChat activity 分面；`QQ` `qq.official`；`WX` `wechat.official_account`/`mini_program`/`customer_service`/`open_platform`（个人微信单列为不支持）；`WC` WeCom 多分面；`LK` Feishu/Lark（环境必须显式）；`DT` DingTalk app robot/group Webhook/Stream；`MX` Matrix Client-Server/Application Service；`TG` Telegram Bot API。`onebot11.qq` 和 TDLib 不参与官方平台列，只在扩展/迁移中讨论。

> 为保持表格可读，`Sx` 均指该列平台报告的 source register，例如 `DC:S6` 指 [Discord S6](agents/discord.md#4-source-register)。没有空格；证据不足统一写 `?`。

## 2. 身份与会话拓扑

| 能力 | DC | X | QQ | WX | WC | LK | DT | MX | TG |
|---|---|---|---|---|---|---|---|---|---|
| Bot/app identity | P:N/A:N (S1,S12) | P:N/A:L (S4,S21) | P:X/A:N (S2,S4) | P:N/A:L (S1,S2,S13) | P:N/A:L (S1,S3-S5) | P:N/A:L (S3-S6) | P:N/A:L (S3-S6) | P:N/A:L (S3,S4) | P:N/A:N (S1,S2) |
| Authentication | P:N/A:N (S12) | P:N/A:L (S4,S15) | P:U/A:N (S2) | P:N/A:L (S5,S13,S18) | P:N/A:L (S1,S3,S4) | P:N/A:L (S1-S6) | P:N/A:N (S2,S3) | P:N/A:N (S3) | P:N/A:N (S1,S8) |
| Multi-account/tenant install | P:N/A:L (S12,S21) | P:N/A:L (S4,S21) | P:N/A:L (S4) | P:N/A:E (S8,S9,S20) | P:N/A:L (S1,S6) | P:N/A:L (S3,S5) | P:N/A:L (S1,S3,S16) | P:N/A:L (S4) | P:N/A:L (S1,S2) |
| User/profile lookup | P:N/A:L (S7,S12) | P:N/A:L (S4) | P:N/A:L (S2,S4) | P:N/A:L (S9,S15,S18) | P:N/A:? (S6 secondary) | P:N/A:L (S26,S27) | P:N/A:L (S2,S18) | P:N/A:L (S3) | P:N/A:L (S1,S12) |
| 1:1 DM/service conversation | P:N/A:L (S6,S7) | P:N/A:L legacy (S9-S11) | P:N/A:L C2C (S3,S4) | P:N/A:L official service surfaces；personal automation is unsupported (S2,S5,S13) | P:N/A:L app/service (S3,S4,S7) | P:N/A:L (S8,S35) | P:N/A:L app robot (S4,S5) | P:N/A:L room marker (S3,S5) | P:N/A:L; bot cannot initiate (S1,S2) |
| Group/room/channel | P:N/A:L (S5,S8) | P:N/A:X (S1,S7,S18) | P:N/A:L group/Guild distinct (S3,S4,S6) | P:N/A:U personal groups (S2,S13) | P:N/A:L appchat；ordinary groups are unsupported for general automation (S3,S7) | P:N/A:L (S17,S18,S37) | P:N/A:L group @bot (S5,S8) | P:N/A:L room；no portable guild abstraction (extension-only) (S3) | P:N/A:L group/channel (S1,S10) |
| Thread/topic/forum | P:N/A:L (S5,S19) | P:N/A:X reply chain (S7,S12) | P:N/A:? current Guild forum (S3) | P:?/A:U (S2,S14) | P:?/A:U (S2,S7) | P:N/A:L (S9,S10,S37) | P:?/A:? (no direct source) | P:N/A:L stable relation (S7) | P:N/A:L (S1,S14,S15) |
| Conversation create/update/delete | P:N/A:L (S5,S8) | P:N/A:X (S9,S10) | P:N/A:? outside Guild (S3,S6) | P:N/A:U (S2,S13) | P:N/A:L appchat only (S7) | P:N/A:L (S17,S18) | P:N/A:? generic (S10,S19) | P:N/A:L powers/profile (S3) | P:N/A:L topic/admin subsets (S1) |

**边界结论：**不存在可靠的通用 `Guild`。核心使用 `ConversationRef{kind,parent?}`；homeserver、tenant、department、X List、微信 follower set 和 Telegram channel 均不得强制映射为 guild。证据见 [DC §5](agents/discord.md#5-product-vs-official-api-boundary)、[X §5](agents/x.md#5-product-vs-official-api-boundary)、[QQ §5](agents/qq.md#5-product-vs-official-api-boundary)、[MX §5](agents/matrix.md#5-product-vs-official-api-boundary)。

## 3. 消息、内容与媒体

| 能力 | DC | X | QQ | WX | WC | LK | DT | MX | TG |
|---|---|---|---|---|---|---|---|---|---|
| Create/send message | P:N/A:L permissions/intents (S2,S6) | P:N/A:L entitlement (S7-S9) | P:N/A:L passive/scenario capability；context-free active send remains UNKNOWN and disabled (S4,S5) | P:N/A:L official service surfaces；personal automation is unsupported (S4,S5,S13) | P:N/A:L official robot/appchat; IA details ? (S3,S4,S7) | P:N/A:L scope/visibility (S8) | P:N/A:L surface-specific (S6,S7,S13) | P:N/A:L powers/server (S3) | P:N/A:L rights/context (S1) |
| Get one/history | P:N/A:L (S2,S6) | P:N/A:L; DM 30d (S4,S5,S10) | P:N/A:? C2C/group (S4,S9) | P:N/A:L OA/KF; personal U (S2,S14) | P:N/A:? ordinary; archive/KF X (S12; S18 secondary) | P:N/A:L (S10) | P:N/A:? general; send status only (S10 derived) | P:N/A:L history visibility/E2EE (S3) | P:N/A:U archive; current event only (S1,S8) |
| Edit | P:N/A:L (S6) | P:N/A:L post; DM U (S7,S22) | P:?/A:U; stream X (S4) | P:?/A:U; shared-card X (S2) | P:N/A:? card-only secondary (S15) | P:N/A:L own message (S11) | P:N/A:U ordinary; card X (S9,S17) | P:N/A:L replacement (S8) | P:N/A:L own/inline (S1) |
| Delete/recall/redact | P:N/A:L (S6) | P:N/A:L own content; XChat U (S7,S9,S19) | P:N/A:L constraints uncertain (S3,S4) | P:N/A:U chat; publication X (S2) | P:N/A:? recall detail secondary; archive observes X (S12,S14) | P:N/A:L recall (S12) | P:N/A:? SDK-derived recall (S10) | P:N/A:L redaction ≠ hard delete (S3) | P:N/A:L context/time (S1) |
| Reply/quote | P:N/A:N reply; quote E (S6) | P:N/A:L post reply/quote (S7,S8) | P:N/A:L trigger/ref (S4) | P:N/A:L passive; quote U (S4,S14) | P:N/A:L trigger-bound; quote ? (S4,S13 secondary) | P:N/A:L thread/ref (S9,S10) | P:N/A:E send/session limited，无 durable link (S6) | P:N/A:L reply; quote E (S8) | P:N/A:L (S1) |
| Forward/copy | P:N/A:N (S6) | P:N/A:E URL share (S9) | P:N/A:? official (S9,S10 OneBot-only) | P:N/A:U autonomous; OP L (S19) | P:N/A:? ordinary (S12 archive-only) | P:N/A:L type exclusions (S13) | P:N/A:?; card X (S17) | P:N/A:E no stable provenance (S3) | P:N/A:L forward/copy (S1,S9) |
| Rich text/mentions | P:N/A:L (S6,S23) | P:N/A:L entity model (S7,S8) | P:N/A:L grant/scenario (S3,S4) | P:N/A:L text/link; mention U (S5,S13) | P:N/A:L dialect/surface (S3,S4,S7) | P:N/A:L (S8,S14) | P:N/A:L schema varies (S6,S10,S13) | P:N/A:L sanitized HTML/push rules (S19) | P:N/A:L entities/dialects (S1,S15) |
| Reactions | P:N/A:L (S3,S6) | P:N/A:X likes; DM U (S4,S19) | P:N/A:L Guild only (S3) | P:N/A:U (S13) | P:N/A:? no official complete catalog (S4) | P:N/A:L (S15,S36) | P:N/A:? (no direct source) | P:N/A:L optional client profile (S9) | P:N/A:L admin/update gates (S1) |
| Stickers/custom emoji | P:N/A:L (S6,S8) | P:?/A:U (S1,S9) | P:N/A:? (S4,S10 nonofficial) | P:N/A:U (S13) | P:N/A:? send; archive X (S12) | P:N/A:L receive-key only (S8,S14) | P:N/A:? (no direct source) | P:N/A:L version/client (S2,S19) | P:N/A:L rights/ownership (S1,S3) |
| Poll | P:N/A:L (S3,S6,S20) | P:N/A:L post poll (S7) | P:?/A:? (no official proof) | P:N/A:U; menu E not poll (S13) | P:N/A:X card/archive; details secondary (S2,S12,S15) | P:N/A:E; native send U (S8,S20) | P:N/A:? (no official proof) | P:N/A:X MSC3381 (S15) | P:N/A:L own poll/update limits (S1,S15) |
| Card/button/form | P:N/A:X renderer (S10,S24) | P:N/A:X; generic U (S7) | P:N/A:L keyboard; card grants remain ? (S3,S4) | P:N/A:X menu/MP/KF (S13,S21) | P:N/A:X surface schema (S3,S4; S15 secondary) | P:N/A:X card schema (S20-S22) | P:N/A:X advanced card (S8,S9,S17) | P:E/A:X custom/widget (S14,S19) | P:N/A:L keyboard; Mini App X (S1,S3,S4) |
| Image/audio/video/file | P:N/A:L (S6,S23) | P:N/A:L media; arbitrary file/audio U (S9,S16-S19) | P:N/A:L SDK limits need live verify (S4) | P:N/A:L by service surface (S3-S5,S13,S14) | P:N/A:L official robot/appchat; IA/KF details ? (S3,S7,S12) | P:N/A:L keys/access (S23-S25) | P:N/A:L type parity ? (S4-S6,S10 derived) | P:N/A:L server/E2EE (S13,S19) | P:N/A:L hosted/local limits (S1,S13) |
| Atomic media group/album | P:N/A:X multiple attachments/gallery，portable album 不成立 (S6,S24) | P:N/A:L post array, not chat album (S16) | P:N/A:?; sequential E (S4) | P:N/A:U (S4,S13) | P:?/A:?; sequential E (S3,S10) | P:?/A:? (no source) | P:?/A:? (no source) | P:E/A:X no stable atomic contract (S10,S19) | P:N/A:L 2–10 (S1) |
| Upload/download media | P:N/A:L (S6,S23) | P:N/A:L URLs/async (S10,S16,S17) | P:N/A:L TTL/scope (S4) | P:N/A:L temp IDs (S14,S22) | P:N/A:L official GR/AR; others ? (S3,S12) | P:N/A:L key/access (S23-S25) | P:N/A:L download SDK claim qualified (S5,S10) | P:N/A:L MXC/server policy (S13) | P:N/A:L hosted/local (S1,S13) |

## 4. 成员、权限、互动与事件

| 能力 | DC | X | QQ | WX | WC | LK | DT | MX | TG |
|---|---|---|---|---|---|---|---|---|---|
| Member lifecycle | P:N/A:L intent/rights (S2,S8) | P:N/A:X DM/Space-specific (S10,S18) | P:N/A:L Guild; group ? (S3,S6) | P:N/A:U personal group; KF X (S13,S14) | P:N/A:L appchat; other claims ? (S7,S12) | P:N/A:L (S17) | P:N/A:? events, mutation unproved (S19) | P:N/A:L powers/federation (S3) | P:N/A:L no full enumeration (S1) |
| Roles/permissions | P:N/A:L hierarchy (S8,S17) | P:N/A:X Space/DM-specific (S10,S18) | P:N/A:L Guild; group ? (S6,S9) | P:N/A:U group; KF X (S13) | P:N/A:? generic; app scope X (S6 secondary,S7) | P:N/A:L coarse group roles (S17,S18) | P:N/A:? org role ≠ chat role (S11) | P:N/A:L power levels (S3) | P:N/A:L rights/chat type (S1,S10) |
| Moderation | P:N/A:L (S8,S16) | P:N/A:L privacy/reply control (S4,S23) | P:N/A:L Guild only (S6) | P:N/A:U generic; OA X (S2,S16) | P:N/A:U generic; appchat ownership only (S7) | P:N/A:L posting subset (S18) | P:N/A:? no primary proof | P:N/A:L policy implementation varies (S3,S17,S20) | P:N/A:L rights (S1) |
| Audit/compliance records | P:N/A:L audit log (S18) | P:N/A:U generic (S12,S13) | P:N/A:X message audit (S3,S7) | P:N/A:X publication/safety (S2,S16) | P:N/A:L archive only (S12); entitlement details ? | P:N/A:? general export (no source) | P:N/A:? (S12,S19 are events) | P:N/A:U stable audit; admin X (S20) | P:N/A:U Bot API (S9,S11) |
| Enterprise directory | P:N/A:E guild members (S8) | P:N/A:U directory (S4) | P:N/A:U (S4) | P:N/A:U (S2) | P:N/A:? secondary-only (S6) | P:N/A:L (S26,S27) | P:N/A:L (S11,S18) | P:N/A:X user directory ≠ enterprise (S2,S3) | P:N/A:U contacts (S3,S9) |
| Native command catalog | P:N/A:L (S9) | P:U/A:U generic (S7) | P:N/A:? console commands not universal (S3,S7) | P:N/A:X menu, no slash registry (S13,S21) | P:N/A:X menu/card (S4,S13) | P:N/A:X menu; slash U (S35,S36) | P:N/A:E text + X card (S5,S8,S17) | P:E/A:X custom event；text parsing only E (S3,S14) | P:N/A:L (S3) |
| Interaction ack/follow-up | P:N/A:L 3s/15m (S10) | P:?/A:U generic (S7) | P:N/A:L 5s (S3,S4) | P:N/A:L menu/KF callback；renderer X (S13,S21) | P:N/A:L IR/card；schema X (S4,S5) | P:N/A:L 3s card；schema X (S20) | P:N/A:L card/session；schema X (S8,S9) | P:E/A:X no stable generic (S14) | P:N/A:L callback (S1) |
| Fixed-destination publish | P:N/A:L incoming Webhook (S11) | P:?/A:? no fixed-destination posting endpoint established in the audited catalog | P:?/A:? no common proof | P:N/A:X OA publication (S2) | P:N/A:L GR only (S3) | P:N/A:L custom Webhook (S7) | P:N/A:L group Webhook (S6,S13) | P:N/A:U generic; AS differs (S4) | P:N/A:X channel bot context (S1) |
| Event subscription/ingress | P:U/A:L Gateway/Webhook (S2,S3,S13) | P:U/A:L entitlement/conflict (S6,S12-S14) | P:U/A:L Webhook; WS lifecycle risk (S3) | P:U/A:L OA/MP/KF (S3,S10,S14) | P:U/A:L official GR has no ingress (S3-S5,S12) | P:U/A:L HTTP/long connection (S28,S29) | P:U/A:L HTTP/Stream (S7,S8) | P:U/A:L sync/AS install (S3,S4) | P:U/A:N webhook/poll exclusive (S1) |
| Trigger-bound/passive reply | P:U/A:L interaction only (S10) | P:U/A:X summon eligibility (S8) | P:U/A:L exact current count ? (S4) | P:U/A:L 5s/window (S3-S5,S13) | P:U/A:L surfaces differ; many details ? (S4,S10,S13 secondary) | P:U/A:? messaging; card 3s (S20) | P:U/A:L session URL expiry (S6) | P:U/A:U protocol window (S3) | P:U/A:U generic; business X (S1,S3) |

## 5. 社交、状态、实时媒体与安全

| 能力 | DC | X | QQ | WX | WC | LK | DT | MX | TG |
|---|---|---|---|---|---|---|---|---|---|
| Contact/follow/social graph | P:N/A:U generally; partner L (S7,S12) | P:N/A:L follow/mute/block (S3,S4) | P:N/A:? events only, no list (S3) | P:N/A:L OA follower; personal U (S2,S8,S9) | P:N/A:? customer relation is mirror-only evidence (S8 secondary) | P:?/A:? omission-only；S26 establishes directory semantics, not a complete social API catalog | P:N/A:L enterprise contacts, follow U (S2,S11) | P:U/A:U; account data X (S2,S3) | P:N/A:U (S3,S9) |
| Feed/post/repost/search | P:N/A:X forum/announcement (S5,S6) | P:N/A:L core SNS (S4-S8) | P:N/A:? outside Bot API (S3) | P:N/A:X OA publication；Moments U (S2,S3) | P:N/A:? CRM/campaign is mirror-only evidence (S8 secondary) | P:?/A:? omission-only；S8/S14 cover messages, not a complete feed/social catalog | P:?/A:? no portable feed (S6,S10) | P:E/A:X no normative SNS (S3) | P:N/A:X channel posts; feed/follow U (S1,S9) |
| Notification management | P:N/A:U user settings (S12) | P:N/A:E selected activity; inbox U (S12,S13) | P:N/A:? toggle event ≠ arbitrary send (S3) | P:N/A:X template/consent lifecycle (S6,S7,S11) | P:N/A:? app/campaign evidence is mirror-only (S2,S8 secondary) | P:N/A:X urgent (S38) | P:N/A:X robot notification (S6,S10) | P:N/A:L pusher/profile (S17) | P:N/A:U generic control (S1,S9) |
| Presence | P:N/A:L (S2,S3) | P:?/A:U (S1,S13) | P:N/A:? no endpoint (S3,S4) | P:?/A:U general (S2,S14) | P:?/A:? omission only (S1-S6) | P:?/A:? omission only (S35) | P:N/A:? omission only | P:N/A:L module/policy (S17) | P:N/A:U (S2,S13) |
| Typing | P:N/A:L (S3,S5) | P:N/A:L inbound legacy; outbound U (S12,S13) | P:N/A:L outbound subset (S4) | P:N/A:L OA service only (S2) | P:N/A:? omission only | P:?/A:? omission only (S35) | P:N/A:? omission only | P:N/A:L module/profile (S17,S4) | P:N/A:L outbound only (S1,S13) |
| Read receipt | P:L/A:U (S3,S12) | P:N/A:L inbound legacy; outbound U (S12,S13) | P:N/A:U (S4) | P:N/A:U (S5,S13,S14) | P:N/A:? specific scenarios secondary (S2) | P:N/A:L own P2P/7d (S19) | P:N/A:? SDK-derived send status (S10) | P:N/A:L module/privacy (S7,S17) | P:N/A:U (S1,S8) |
| Voice/video/live control | P:N/A:L voice/Stage; video ?/X (S3,S14,S15) | P:N/A:L Space read; control/call U (S13,S18,S20) | P:N/A:U call control (S1,S3) | P:N/A:U personal calls/live (S14) | P:N/A:? generic control; archive observation X (S12) | P:N/A:? call API negative by omission (S14,S24) | P:N/A:? no primary proof | P:N/A:L legacy VoIP; RTC X (S3) | P:N/A:U Bot API participation (S9,S13) |
| E2EE/crypto control | P:L/A:U text; voice L (S14,S23) | P:N/A:U XChat plaintext/crypto API (S13,S19) | P:?/A:U bot E2EE (S3) | P:N/A:U E2EE keys; callback crypto L (S3,S14) | P:N/A:L transport/archive crypto; no E2EE claim (S12,S13 secondary) | P:?/A:? no key API evidence (S13) | P:?/A:? TLS only (S7,S8) | P:N/A:L optional E2EE/client crypto (S11) | P:N/A:U secret chat; transport L (S2,S8) |
| Federation | P:U/A:U (S8,S23) | P:U/A:U (S1) | P:U/A:U (S3) | P:U/A:U (S2,S20) | P:X/A:X interop not federation (S8,S12) | P:U/A:U Feishu/Lark not federation (S1,S2) | P:U/A:U (S1) | P:N/A:U direct bot S-S; room via homeserver L (S12) | P:U/A:U (S12) |
| Tenant/compliance | P:X/A:L guild audit (S8,S18) | P:X/A:X project/compliance-specific (S1) | P:X/A:? review/region (S7) | P:X/A:L review/account (S5-S7,S16) | P:N/A:X tenant/archive-specific；details ? (S1,S12) | P:N/A:L region/scope (S1-S6) | P:N/A:? region parity; auth L (S15,S16) | P:X/A:X deployment-specific (S12,S20) | P:U/A:U tenant; privacy duties X (S12) |

## 6. 可靠性与限制

| 能力 | DC | X | QQ | WX | WC | LK | DT | MX | TG |
|---|---|---|---|---|---|---|---|---|---|
| Ingress idempotency/dedupe | P:U/A:L delivery IDs/sequence (S2,S6,S13) | P:U/A:? ordering/redelivery (S7,S9,S12) | P:U/A:L msg/event IDs (S3,S4) | P:U/A:L msgid/user+time (S3,S4,S6,S13) | P:U/A:L callback/archive IDs; details ? (S12,S13 secondary) | P:U/A:L event ID/UUID (S8,S9,S28) | P:U/A:L duplicates despite ACK (S8) | P:U/A:N txn/event IDs (S3,S4) | P:U/A:L monotonic update_id (S1) |
| Outbound idempotency | P:U/A:L short nonce only (S6) | P:U/A:? assume absent (S7,S9) | P:U/A:L reply msg_seq; not universal (S4) | P:U/A:L selected sends (S6,S13) | P:U/A:? varies by surface (S2 secondary,S12) | P:U/A:L UUID per operation/window (S8,S9,S37) | P:U/A:? no universal proof | P:U/A:N txn IDs except state (S3) | P:U/A:? no generic key (S1) |
| Pagination/history horizon | P:N/A:L endpoint-specific (S6,S8,S23) | P:N/A:L token/window (S5,S10) | P:N/A:? message history; Guild subset (S6,S7) | P:N/A:L OA/KF cursor (S8,S14) | P:N/A:L official archive seq; other details ? (S12) | P:N/A:L page tokens (S10,S16,S17,S26) | P:N/A:? generic pagination (S11) | P:N/A:L opaque tokens (S3,S7,S10) | P:N/A:L no history cursor (S1,S8) |
| Rate/quota discovery | P:U/A:L headers/buckets (S4) | P:U/A:L plan+credits+endpoint (S2,S3,S12) | P:U/A:L no universal QPS (S2-S4) | P:U/A:L endpoint/account (S2,S5,S11,S13) | P:U/A:L known official GR/AR; broad numbers ? (S3,S12,S16 secondary) | P:U/A:L endpoint/target (S7-S9) | P:U/A:? mirror numbers provisional (S6,S14) | P:U/A:L 429 dynamic (S3) | P:U/A:L approximate guidance only (S2,S3) |
| Passive/interaction deadline | P:U/A:L 3s/15m (S10) | P:U/A:X summon, not time window (S8) | P:U/A:? current per-scenario count/window (S4) | P:U/A:L 5s/48h surfaces (S3-S5,S13) | P:U/A:L surface-specific; several values ? (S4,S10,S13 secondary) | P:U/A:? card 3s only (S20) | P:U/A:L session expiry (S6) | P:U/A:U generic (S3) | P:U/A:U generic; business X (S1,S3) |
| Payload/message limits | P:N/A:L entitlement/type (S6,S23) | P:N/A:? text max; media L (S7,S9,S16) | P:N/A:? text/grant; media SDK provisional (S4) | P:N/A:L type-specific (S4,S13,S14,S21,S22) | P:N/A:? secondary-only broad limits; official GR/AR L (S3,S12) | P:N/A:L type-specific (S7,S8,S21) | P:N/A:? type/schema not fully primary (S6,S14) | P:N/A:L 64KiB/server lower (S3,S13) | P:N/A:L type-specific (S1) |

## 7. Surface-qualified keys

以下键必须出现在 installation/capability metadata 中，不能只写平台名：

```text
discord.bot_api
x.posts / x.legacy_dm / x.xchat_activity
qq.official / onebot11.qq
wechat.official_account / wechat.mini_program /
wechat.customer_service / wechat.open_platform
wecom.internal_app / wecom.appchat / wecom.group_webhook /
wecom.intelligent_robot / wecom.customer_contact / wecom.archive
lark.feishu / lark.international
dingtalk.app_robot / dingtalk.group_webhook
matrix.client_server / matrix.application_service
telegram.bot_api / telegram.tdlib_client
```

微信客服统一为 `wechat.customer_service`；其 WeCom 管理/认证上下文属于 installation metadata，不复制为另一个 `wecom.kf` capability 家族。

## 8. 关键不确定项与冲突

| 范围 | 矩阵处置 | 实现要求 |
|---|---|---|
| WeCom | 12/18 来源为二手镜像；通讯录、内部应用消息、客户联系、客服、recall/card/rate 等二手-only 结论降为 `?` | 上线前以可访问的官方页面/SDK/租户集成测试重新取证 |
| QQ active send | 文档仓库生命周期说明与当前 SDK helper 冲突 | `qq.official.active_message = UNKNOWN`，默认关闭，按批准 app 实测 entitlement |
| QQ Gateway | 官方事件页提示退场，SDK 仍支持 | 新集成优先 Webhook；WS 标为 lifecycle risk |
| X Activity/AAA/search/DM/media/quote | 官方页面在 entitlement 与 API generation 上互相冲突 | 每个 operation 独立探测，不用一个“X API 已开通”布尔值 |
| Feishu/Lark | 无完整 parity matrix；presence/typing/call 等仅由 omission 推断 | 环境进入 installation ID；negative-by-omission 一律 `?` |
| Matrix | stable spec、optional module/profile、MSC 和客户端实现不同 | powers/profile/部署限制写 `L`；poll/widget/RTC/MSC 写 `X` |
| DingTalk | 部分 recall/read/download/status 结论来自 `pkg.go.dev` 派生页，数字 quota 有二手来源 | 相关格保持 `?` 或 `L`+低置信，不写死数值 |
| Discord/X Help | 少数官方 Help 页面只能访问索引片段 | 在[来源索引](sources.md)标记 `OFFICIAL — ACCESS-LIMITED` |
| Telegram 10.x | 2026 新模式可能有客户端采用差异；Webhook retry/global rate 未完整公开 | pin Bot API schema，容忍未知 update；retry 视为可能重复 |
| OBCX runtime | 平台 scout 未运行 conformance/runtime tests | 迁移建议是静态代码审计结论，不宣称已验证运行行为 |

更完整冲突表见[证据审查 §4](reviews/evidence-audit.md#4-conflict-register)。
