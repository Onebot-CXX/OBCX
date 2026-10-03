# OBCX Wave 2 Capability Taxonomy Review

## Review

- **Correct:** All ten Wave 1 artifacts were reviewed. They consistently support granular capability discovery, typed serializable actor messages, and process ownership of sockets, credentials, cursors, retries, and media transfer.
- **Correct:** The current OBCX audit identifies the root architectural problem: OneBot-shaped `IBot`, opaque JSON results, unsupported Telegram stubs, ambiguous platform-only account lookup, and direct actor access to live bot objects ([OBCX §Executive findings, F1–F21](../agents/obcx-current.md#executive-findings)).
- **Note:** Several platform reports contain documented evidence gaps or conflicts. These affect capability probing and extension boundaries but do not prevent defining a conservative common taxonomy.
- **Recommendation:** Adopt the **56-component candidate split** below: **13 core domain modules, 21 optional executable transport capabilities, 12 runtime infrastructure components, and 10 platform extension packs**.

---

## Evidence and recommendation convention

- **Evidence** is attributed to an exact Wave 1 artifact heading and its source IDs or local file IDs.
- **Recommendation** identifies the proposed OBCX design derived from that evidence.
- A capability is common only where at least two separately audited platforms have safely aligned semantics. Otherwise it remains in a platform extension pack.
- “Capability” below means a discoverable executable request/result or event lifecycle. DTOs are never executable capabilities.

---

# 1. Report completeness issues

All ten artifacts are structurally complete enough for taxonomy work, but the following limitations must remain visible.

| Artifact | Completeness issue | Taxonomy impact |
|---|---|---|
| Discord | Product Help Center pages were access-limited; bot video/screenshare and general read-state remain unknown or unsupported. Verification and privileged-intent gates have two distinct thresholds. | Keep video/screenshare namespaced; omit generic read receipts for Discord; make intent/review state dynamic. [Discord §12, S2, S12, S14, S19–S22](../agents/discord.md#12-conflicts-and-unknowns) |
| X | Official pages conflict on Account Activity entitlement, full-archive search, DM deletion, media upload, quote-post tiering, and edit timing. XChat exposes activity notifications but no documented plaintext content API. | Probe entitlements; keep X Activity, Account Activity, legacy DM, and XChat distinct. [X §12, S1, S4, S7–S13, S16–S19, S22](../agents/x.md#12-conflicts-and-unknowns) |
| QQ | Official documentation and SDK conflict on active sends and WebSocket lifecycle. Current Guild availability, ordinary passive-reply limits, group administration, cards, and history are unresolved. | Default active send false; prefer Webhook; keep Guild, C2C, group, streaming, and OneBot semantics separate. [QQ §12, S3–S7](../agents/qq.md#12-conflicts-and-unknowns) |
| Consumer WeChat | No personal-account bot API was found, but this is necessarily a negative result from official catalogs and rules. Mini Program service-message triggers/types are not fully enumerated; media-size text conflicts. | Personal WeChat remains unavailable; capability-probe MP service sending; do not infer ordinary rooms, contacts, or history. [WeChat §12, S2, S5, S10, S16, S22](../agents/wechat.md#12-conflicts-and-unknowns) |
| WeCom | Many important endpoint details rely on clearly marked secondary mirrors because official pages are client-rendered or path-unstable. Card-token lifetime and read-receipt support remain uncertain. | Keep surface boundaries strict and require integration tests against provisioned tenants. [WeCom §12, S2, S6, S8–S18](../agents/wecom.md#12-conflicts-and-unknowns) |
| Lark/Feishu | No authoritative Feishu/Lark parity matrix was found. Topic terminology, message-update events, audit export, presence, calls, and exact international limits remain unresolved. | Environment is part of installation identity; Feishu evidence cannot automatically enable Lark capabilities. [Lark §12, S1, S2, S11, S35–S37](../agents/lark.md#12-conflicts-and-unknowns) |
| DingTalk | Main documentation was often not extractable; some numeric quotas are secondary. Exact message/media schemas, specialized robots, regional parity, history, reactions, moderation, and audit remain unresolved. | Prefer app robots; capability-probe specialized surfaces; do not hard-code mirror quotas. [DingTalk §12, S8–S10, S14](../agents/dingtalk.md#12-conflicts-and-unknowns) |
| Matrix | Protocol semantics are strong, but optional client profiles, server policy, E2EE interoperability, polls/widgets/MatrixRTC, physical deletion, and provider limits vary. | Negotiate versions, room powers, crypto readiness, and unstable MSCs; never equate popular client behavior with stable core. [Matrix §12, S3, S4, S11, S14, S15](../agents/matrix.md#12-conflicts-and-unknowns) |
| Telegram | New 2026 modes may have uneven clients. The older FAQ conflicts with current bot-to-bot behavior; webhook retry and global rates are unspecified. Bot API has no history or member enumeration. | Pin generated DTOs, tolerate unknown updates, and keep TDLib outside the Bot API adapter. [Telegram §12, S1–S3, S8, S15](../agents/telegram.md#12-conflicts-and-unknowns) |
| OBCX current | No runtime/conformance tests were executed. Some legacy Bridge paths may be inactive; target-account selection, callback-query mapping, provider identity, retries, limits, and full pipeline type naming remain unresolved. | Migration needs account-explicit routing and adapter contract tests before removing legacy compatibility. [OBCX §Unknowns and open questions, F10, F12–F21](../agents/obcx-current.md#unknowns-and-open-questions) |

---

# 2. Proposed normalized resource, event, and operation taxonomy

## 2.1 Normalized resources

**Recommendation:** The common domain contains only data semantics evidenced across multiple platforms.

| Resource | Normalized meaning | Explicit exclusions |
|---|---|---|
| `InstallationRef` | One configured platform environment, application/account identity, tenant if applicable, and credential-binding reference. | No token or secret in actor-visible data. |
| `PrincipalRef` | Scoped human, bot, app, webhook, service agent, or virtual-user identifier. | No universal unqualified user string; no assumption that a bot is a human account. |
| `ConversationRef` | Direct conversation, group/room, broadcast channel, or owned chat, with optional parent. | Homeserver, tenant, department, X List, Space, publication feed, and WeChat follower set are not conversations. |
| `ThreadRef` | A conversation-local thread/topic root and optional parent reply. | Discord forum tags, Telegram topic settings, Matrix relation validation, and Lark topic mode remain extensions. |
| `MessageRef` | Provider message/event identifier plus its installation and conversation. | X social posts, OA publications, template notifications, audit records, and cards are not silently reclassified as messages. |
| `MessageSnapshot` | Author, timestamps, content, availability/redaction state, relations, and platform provenance. | It is not necessarily a mutable provider object. Matrix edits and X edit chains remain immutable histories. |
| `ContentBlock` | Conservative typed text, mention, link, media reference, and simple structured display content. | No universal card, Markdown dialect, arbitrary HTML, poll, sticker, or raw executable action. |
| `MediaHandle` | Opaque credential-scoped media reference, metadata, lifetime, and optional encrypted descriptor. | Never a durable public URL by default. |
| `InteractionRef` | A user invocation with action identifier, typed values, deadline, and response state. | Rendered card/component schemas remain platform extensions. |
| `MembershipRef` | Principal membership in a conversation and coarse observed role/rights snapshot. | No universal named RBAC model. |
| `TriggerContext` | Prior ingress authorization for a constrained response: trigger ID, expiry, remaining uses/count, and route reference. | Not a general active-send entitlement. |
| `DeliveryState` | Submitted, accepted, partially accepted, delivered if evidenced, failed, unknown, or read if explicitly evidenced. | Acceptance never implies delivery or reading. |
| `CapabilitySnapshot` | Supported operations plus scopes, rights, target kinds, horizons, deadlines, limits, and last probe. | Unsupported operations are omitted, not represented by inert methods. |

Evidence includes Discord conversation/message models [§9, S5–S10](../agents/discord.md#9-normalized-common-capability-candidates), QQ scoped IDs [§9, S3–S4](../agents/qq.md#9-normalized-common-capability-candidates), WeChat scoped identities and trigger windows [§9, S5, S9, S13–S15](../agents/wechat.md#9-normalized-common-capability-candidates), Matrix immutable events [§9, S3, S8–S10](../agents/matrix.md#9-normalized-common-capability-candidates), and Telegram compound chat/message identity [§13, S1](../agents/telegram.md#13-obcx-design-implications).

## 2.2 Normalized events

Actor-visible ingress consists only of serializable typed events:

```text
IngressEvent {
  delivery_id?,
  provider_event_id?,
  installation,
  occurred_at?,
  received_at,
  delivery_semantics,
  payload: TypedEvent,
  extension?,
  raw_ref?
}
```

Recommended common event cases:

1. `MessageCreated`
2. `MessageEdited`
3. `MessageRemoved` with semantic kind `Recall | Redaction | DeleteNotice`
4. `ReactionChanged`
5. `ThreadChanged`
6. `ConversationChanged`
7. `MembershipChanged`
8. `InteractionInvoked`
9. `TypingChanged`
10. `PresenceChanged`
11. `ReadReceiptChanged`
12. `DeliveryStateChanged`
13. `CapabilityChanged`
14. `UnknownPlatformEvent` with a versioned namespaced extension

Transport events such as heartbeats, reconnect instructions, CRC checks, Gateway session state, WebSocket sequence management, and OAuth callbacks remain runtime-internal unless represented by an authorized diagnostics event.

This follows the event-envelope recommendations in Discord [§13, S2, S3, S10, S13](../agents/discord.md#13-obcx-design-implications), X [§9, S12–S14](../agents/x.md#9-normalized-common-capability-candidates), Matrix [§13, S3–S4](../agents/matrix.md#13-obcx-design-implications), Telegram [§13, S1](../agents/telegram.md#13-obcx-design-implications), and the existing serializable OBCX envelope [OBCX §Executive findings, F12–F13](../agents/obcx-current.md#executive-findings).

## 2.3 Normalized operation envelope

```text
OperationRequest<T> {
  operation_id,
  installation,
  capability,
  target?,
  idempotency_key?,
  deadline?,
  payload: T
}

OperationResult<T> =
  Completed<T>
  | Accepted { provider_id?, delivery_state }
  | Partial { completed, rejected, warnings }
  | Unsupported
  | ScopeOrGrantMissing
  | PermissionDenied
  | PolicyRejected
  | RateLimited { retry_at?, scope? }
  | DeadlineExpired
  | NotFound
  | ValidationFailed
  | OutcomeUnknown
  | PlatformFailure { code?, request_id?, extension? }
```

`Unsupported` is valid only for capability state changes or race conditions after discovery. A capability absent from discovery has no callable actor endpoint.

---

# 3. Precise candidate four-way count

## Recommendation

| Category | Count | Purpose |
|---|---:|---|
| Core domain/data modules | **13** | Serializable resources, events, results, and constraints; no transport execution |
| Optional executable transport capabilities | **21** | Small cohesive request/result or event lifecycles |
| Runtime infrastructure components | **12** | Process-owned transport, routing, security, reliability, and media machinery |
| Platform extension packs | **10** | Namespaced semantics not safely common |
| **Total** | **56** | Candidate architecture surface |

---

# 4. Component catalog

## 4.1 Core domain modules — 13

| ID / component | Responsibility | Dependencies | Representative data | Supporting evidence and counterexamples |
|---|---|---|---|---|
| D01 `ScopedIdentity` | Typed principal and native identifier namespaces. | None | `PrincipalRef{installation, kind, native_id}` | QQ OpenIDs, WeChat OpenID/external_userid, Lark ID kinds, Matrix MXIDs, and Telegram IDs require scope. Counterexample: current OneBot-oriented raw IDs. [QQ §13, S3–S4](../agents/qq.md#13-obcx-design-implications); [WeChat §13, S9, S15, S18](../agents/wechat.md#13-obcx-design-implications); [OBCX §Gap table, F8](../agents/obcx-current.md#gap-table) |
| D02 `InstallationBinding` | Environment, app/account, tenant, surface, and secret-free credential reference. | D01 | `InstallationRef{platform, environment, tenant?, account, credential_ref}` | X sender is an authorized user; WeCom and DingTalk are tenant-scoped; Lark region matters. Counterexample: Telegram global bot has no tenant. [X §13, S4, S15](../agents/x.md#13-obcx-design-implications); [WeCom §13, S1](../agents/wecom.md#13-obcx-design-implications); [Lark §13, S1–S5](../agents/lark.md#13-obcx-design-implications) |
| D03 `ConversationModel` | Direct/group/room/broadcast/owned-chat references and ancestry. | D01, D02 | `ConversationRef{kind,id,parent?}` | Discord, Matrix, Lark, Telegram support real conversation containers. Counterexamples: WeChat personal groups are unavailable; X Lists/Spaces/DM groups are distinct resources. [Discord §9, S5](../agents/discord.md#9-normalized-common-capability-candidates); [Matrix §9, S5–S6](../agents/matrix.md#9-normalized-common-capability-candidates); [WeChat §6, S2](../agents/wechat.md#6-capability-evidence-table) |
| D04 `MessageReference` | Stable compound message identity and immutable relation references. | D02, D03 | `MessageRef`, `ReplyRef`, `ReplacementRef` | Discord, Lark, Matrix, Telegram expose message IDs and relations. Counterexample: current Telegram deletion encodes `chat_id:message_id` in a string. [Matrix §9, S8–S10](../agents/matrix.md#9-normalized-common-capability-candidates); [OBCX §Concrete migration map, F6](../agents/obcx-current.md#concrete-current-call-to-future-capability-migration-map) |
| D05 `PortableContent` | Conservative serializable text, mentions, links, and block composition. | D01, D07 | `Content{text,mentions,blocks}` | Broad text/media alignment exists. Counterexamples: Markdown dialects, Discord Components V2, Matrix custom events, and platform card JSON. [Discord §10, S24](../agents/discord.md#10-required-namespaced-extensions); [WeCom §10, S2–S4](../agents/wecom.md#10-required-namespaced-extensions); [Matrix §10, S3](../agents/matrix.md#10-required-namespaced-extensions) |
| D06 `MessageSnapshot` | Serializable observed message with content availability, author, timestamps, and provenance. | D01, D03–D05, D07 | `MessageSnapshot{ref,author,content,state,raw_ref?}` | Discord distinguishes redacted content; Matrix preserves originals; Telegram updates expose current messages. Counterexamples: X posts and audit records stay namespaced. [Discord §13, S2, S6](../agents/discord.md#13-obcx-design-implications); [Matrix §13, S8–S10](../agents/matrix.md#13-obcx-design-implications) |
| D07 `MediaReference` | Typed opaque media handles, metadata, expiry, credential scope, and encryption descriptor. | D02 | `MediaHandle{kind,id,mime,size?,expires?,scope}` | QQ, WeChat, WeCom, Lark, Matrix, Telegram all use scoped handles or upload/download APIs. Counterexample: URLs are not guaranteed durable storage. [QQ §9, S4](../agents/qq.md#9-normalized-common-capability-candidates); [Matrix §9, S11, S13](../agents/matrix.md#9-normalized-common-capability-candidates) |
| D08 `InteractionData` | Invocation/action/value/deadline data independent of renderer schema. | D01–D06 | `InteractionRef`, `ActionValue`, `AckState` | Discord, QQ, Lark, DingTalk, Telegram, and WeCom expose callbacks. Counterexample: X and stable Matrix have no generic interaction framework. [Discord §9, S9–S10](../agents/discord.md#9-normalized-common-capability-candidates); [Matrix §6, S14](../agents/matrix.md#6-capability-evidence-table) |
| D09 `MembershipAuthorization` | Membership state and observed coarse rights without claiming universal RBAC. | D01, D03 | `MembershipRef`, `PermissionSnapshot` | Discord, Matrix, Telegram, Lark, and QQ Guild have membership/rights. Counterexample: X DM groups and consumer WeChat expose no common admin model. [Discord §9, S8, S17](../agents/discord.md#9-normalized-common-capability-candidates); [Matrix §6, S3](../agents/matrix.md#6-capability-evidence-table) |
| D10 `IngressEventModel` | Typed serializable event envelope and delivery metadata. | D01–D09 | `IngressEvent<T>` | Every audited platform has event or polling ingress where automation exists. Current OBCX already has a serializable envelope. [OBCX §Executive findings, F12–F13](../agents/obcx-current.md#executive-findings) |
| D11 `OperationModel` | Typed request/result/error, pagination, partial success, and unknown outcome. | D01–D09 | `OperationRequest<T>`, `OperationResult<T>`, `Page<T>` | Discord, X, WeCom, DingTalk, Matrix, and Telegram evidence structured errors, rate results, partial results, or cursors. Counterexample: current OBCX returns opaque JSON strings. [DingTalk §13, S10](../agents/dingtalk.md#13-obcx-design-implications); [OBCX §Gap table, F1, F6–F7](../agents/obcx-current.md#gap-table) |
| D12 `DeliverySemantics` | Separate request acceptance, provider delivery, failure, read, and uncertainty. | D04, D11 | `DeliveryStateChanged` | WeChat KF success is not final delivery; WeCom may silently drop; Telegram uncertain send retries can duplicate. [WeChat §13, S11, S13–S14](../agents/wechat.md#13-obcx-design-implications); [WeCom §13, S7, S9](../agents/wecom.md#13-obcx-design-implications) |
| D13 `CapabilityConstraints` | Discoverable operation support, target kinds, permissions, horizons, deadlines, and dynamic limits. | D02, D03, D09, D11 | `CapabilitySnapshot`, `OperationConstraint` | Required across all reports. Counterexample: Telegram’s `{}` stubs and fabricated status violate this model. [OBCX §Adapter asymmetry, F3, F6](../agents/obcx-current.md#adapter-asymmetry-and-unsupported-behavior) |

## 4.2 Optional executable transport capabilities — 21

Each capability is process-owned and actor-visible only through serializable requests, results, and events.

| ID / capability | Cohesive lifecycle and dependencies | Representative request/result/event | Supporting platforms | Counterexamples / boundary evidence |
|---|---|---|---|---|
| K01 `EventIngress` | Configure subscription/filter, receive, validate, checkpoint, emit typed events. Depends D10, D13; R01, R02, R07. | `SubscribeEvents` → `SubscriptionState`; `IngressEvent<T>` | All audited automation platforms. | Delivery modes differ and remain metadata: Gateway, callback, stream, poll, AS transaction. [Discord §7, S2–S3](../agents/discord.md#7-inbound-event-inventory); [Matrix §7, S3–S4](../agents/matrix.md#7-inbound-event-inventory) |
| K02 `MessageSend` | Create one message and return its provider identity/acceptance. Depends D03–D07, D11–D13. | `SendMessage` → `Created<MessageRef>` | Discord, QQ, WeChat service surfaces, WeCom, Lark, DingTalk, Matrix, Telegram, X legacy DM. | X social posts and template notifications are not this capability; WeChat personal is unsupported. |
| K03 `MessageRead` | Get one or page accessible message history with explicit horizon. Depends D04, D06, D11, D13. | `GetMessage`, `PageMessages` → `Page<MessageSnapshot>` | Discord, Lark, Matrix, X posts/legacy DM; constrained WeChat/WeCom service records. | Telegram Bot API and official QQ C2C/group expose no general history. [Telegram §6, S1, S8](../agents/telegram.md#6-capability-evidence-table); [QQ §6, S4, S9](../agents/qq.md#6-capability-evidence-table) |
| K04 `MessageMutation` | Edit, recall, redact, or delete an existing message with exact semantic result. Depends D04–D06, D11, D13. | `EditMessage`, `RemoveMessage` → `MutationResult{kind}` | Discord, X, Lark, Matrix, Telegram; narrow QQ/WeCom cases. | Matrix redaction is not physical deletion; WeCom card update is not message edit. |
| K05 `ThreadLifecycle` | Create/list/update/close thread/topic containers or send to a thread. Depends D03–D06, D11, D13. | `CreateThread`, `ListThreadMessages`, `CloseThread`; `ThreadChanged` | Discord, Lark, Matrix, Telegram. | X reply chains have no mutable topic object; WeChat has no thread resource. |
| K06 `ReactionLifecycle` | Add/list/remove a reaction and emit changes. Depends D01, D04, D10–D13. | `AddReaction`, `RemoveReaction`; `ReactionChanged` | Discord, Lark, Matrix, Telegram. | X likes are social-post semantics; XChat reactions have no content API; WeChat reactions are unsupported. |
| K07 `PollLifecycle` | Create/stop/read a native poll and receive answer changes. Depends D01, D04–D06, D10–D13. | `CreatePoll`, `StopPoll`; `PollAnswerChanged` | Discord and Telegram. | Lark native vote is readable but not bot-sendable; Matrix polls are MSC extensions. [Lark §6, S8, S14, S20](../agents/lark.md#6-capability-evidence-table); [Matrix §12, S15](../agents/matrix.md#12-conflicts-and-unknowns) |
| K08 `PinLifecycle` | Pin, list, and remove message pins. Depends D03–D04, D09, D11, D13. | `PinMessage`, `ListPins`, `UnpinMessage` | Discord, Lark, Matrix, Telegram. | No common pin API is evidenced for WeChat service conversations or X DMs. |
| K09 `MediaTransfer` | Upload, resolve, download, and expire provider media handles. Depends D07, D11, D13; R11. | `UploadMedia` → `MediaHandle`; `DownloadMedia` → `BlobRef` | All major messaging platforms expose some media path. | Supported kinds, TTLs, and limits differ; bytes/sockets never enter business actors. |
| K10 `ConversationLifecycle` | Create/get/update/delete owned or manageable conversation containers. Depends D03, D09, D11, D13. | `CreateConversation`, `UpdateConversation`, `DisbandConversation` | Discord, Lark, Matrix, WeCom appchat; Telegram topic/chat administration subsets. | WeCom ordinary groups and WeChat personal groups are unavailable to automation. |
| K11 `MembershipLifecycle` | List/get/add/remove/join/leave conversation members. Depends D01, D03, D09–D11, D13. | `AddMember`, `RemoveMember`, `GetMember`; `MembershipChanged` | Discord, Lark, Matrix, Telegram, QQ Guild. | Telegram lacks full member enumeration; QQ group administration is unproved. |
| K12 `ConversationModeration` | Restrict, kick, ban, timeout, redact-as-moderator, or approve joins. Depends D03–D04, D09, D11, D13. | `RestrictMember`, `BanMember`, `ApproveJoinRequest` | Discord, Matrix, Telegram, QQ Guild; limited Lark posting controls. | X mute/block is social graph, not room moderation; WeCom ordinary-group moderation is unsupported. |
| K13 `DirectoryRead` | Read hierarchical enterprise users/departments and visibility provenance. Depends D01–D02, D11, D13. | `GetDirectoryUser`, `ListDepartments` | WeCom, Lark, DingTalk. | Discord guild membership is not an enterprise directory; X follow graph is not a directory. |
| K14 `InteractionResponse` | Receive UI invocation, acknowledge/defer it, and produce permitted follow-up outcome. Depends D08, D10–D13; R10. | `AcknowledgeInteraction`, `DeferInteraction`, `CompleteInteraction` | Discord, QQ, Lark, DingTalk, Telegram callbacks, WeCom intelligent robot. | Stable Matrix has no callback framework; X has no generic buttons/forms framework. |
| K15 `InteractiveArtifact` | Create, deliver, update, and retire a mutable interactive card/artifact. Depends D05, D08, D11, D13. | `SendInteractiveArtifact`, `UpdateArtifact`; `InteractionInvoked` | Lark cards, DingTalk advanced cards, WeCom template cards; Discord supports a subset through components. | Renderer schema and update tokens remain namespaced; WeChat OA menus are not mutable cards. |
| K16 `CommandCatalog` | Publish/remove a native command catalog and receive typed invocations. Depends D08, D10–D13. | `PublishCommands`; `CommandInvoked` | Discord and Telegram. | Lark bot menus and text parsing are not universal slash-command registration; Matrix commands are emulated. |
| K17 `TypingSignal` | Set or observe expiring typing activity. Depends D01, D03, D10–D13. | `SetTyping{expires}`; `TypingChanged` | Matrix and Discord; QQ and Telegram support outbound subsets. | It is not presence or a read receipt. |
| K18 `Presence` | Set/get/observe principal presence with visibility constraints. Depends D01, D10–D13. | `SetPresence`; `PresenceChanged` | Matrix and Discord. | Telegram bots, Lark, DingTalk, QQ, and WeCom lack common bot presence APIs. |
| K19 `ReadReceipt` | Set or observe provider read-up-to state where explicitly supported. Depends D01, D04, D10–D13. | `MarkRead`, `ReadReceiptChanged` | Matrix, Lark bot-sent P2P, DingTalk bot-send status; possibly narrow WeCom scenarios only after proof. | Discord and Telegram have no general bot read receipt. A delivery failure is not a receipt. |
| K20 `FixedDestinationPublish` | Publish to a preconfigured secret-bound destination without conversation ingress. Depends D05–D07, D11, D13; R06, R09. | `PublishFixedDestination` → `Accepted` | Discord incoming webhooks, Lark custom bots, WeCom group robots, DingTalk custom robots. | It does not grant history, member access, DM, or event subscription. |
| K21 `TriggerBoundReply` | Acquire response authorization from ingress, issue allowed replies, and expire/consume the trigger context. Depends D03–D06, D10–D13; R10. | `ReplyToTrigger{trigger,content}` → `Accepted | DeadlineExpired` | WeChat OA passive/service sends, WeCom IR/KF, QQ passive replies, DingTalk session Webhook. | It is not general active messaging. Discord UI interaction ACK belongs to K14, not both capabilities. |

## 4.3 Runtime infrastructure components — 12

| ID / component | Responsibility and dependencies | Representative internal request/result/event | Evidence |
|---|---|---|---|
| R01 `TransportSupervisor` | Own HTTP clients/servers, WebSockets, long polls, Gateway sessions, Matrix sync/AS listeners, and process lifecycle. Depends platform packs. | `StartInstallation`, `TransportStopped` | Process ownership is already correct in OBCX; all platform reports require it. [OBCX §Ownership, F17–F18](../agents/obcx-current.md#ownership-reload-and-network-lifecycle) |
| R02 `IngressGateway` | Verify signatures/encryption, decode, durably admit, acknowledge, and emit D10 events. Depends R01, R07, R10, R12. | `RawIngress` → `IngressAccepted` | Discord interactions/webhooks, X CRC/HMAC, WeChat/WeCom encryption, and webhook deadlines require process handling. |
| R03 `EgressDispatcher` | Accept typed actor requests, resolve capability implementation, enforce admission, and return correlated results. Depends R04–R06, R08–R10. | `OperationRequest<T>` → `OperationResult<T>` | Replaces Bridge’s direct `IBot&` calls. [OBCX §Concrete migration map, F20–F21](../agents/obcx-current.md#concrete-current-call-to-future-capability-migration-map) |
| R04 `CapabilityDirectory` | Store per-installation/target capability snapshots and invalidate them after grants, rights, or probes change. Depends D13, platform packs. | `DiscoverCapabilities` → `CapabilitySnapshot` | Required by every Wave 1 report; eliminates stubs and RTTI discovery. |
| R05 `AccountRouter` | Resolve explicit installation/account and target; reject ambiguity. Depends D02–D03, R04. | `ResolveRoute{installation,target}` | Current `find_bot(platform)` intentionally fails for multiple accounts. [OBCX §Bridge acquisition, F14–F15, F21](../agents/obcx-current.md#bridge-acquisitioncalls) |
| R06 `CredentialTokenService` | Own secrets, token refresh, OAuth grants, webhook secrets, and redaction. Depends secure process storage. | `AcquireCredentialLease` | Required by Discord, X, QQ, WeChat, WeCom, Lark, DingTalk, Matrix, and Telegram auth models. No secret enters actor DTOs. |
| R07 `IngressJournal` | Persist delivery IDs, cursors, update IDs, sequence/transaction checkpoints, and dedupe decisions. Depends durable storage. | `CommitIngress`, `AdvanceCursor` | Matrix AS retries, Telegram update offsets, Discord sequence resume, X/WeChat webhooks, and current missing Telegram update identity justify it. |
| R08 `OutboxReconciler` | Persist outbound intent, manage uncertain retries, reconcile provider IDs, and avoid claiming provider idempotency. Depends R03, R07, R09. | `EnqueueOperation`, `OutcomeUnknown`, `Reconciled` | X and Telegram lack general idempotency; Discord and Lark keys are operation-specific; Matrix state lacks txn-id semantics. |
| R09 `RateQuotaGovernor` | Enforce per-token, tenant, endpoint, target, recipient, and budget scopes from dynamic feedback. Depends D13. | `AcquireBudget`, `RateLimited` | All reports reject a universal rate constant; WeCom can silently protect recipients. |
| R10 `DeadlineTriggerCoordinator` | Track interaction ACKs, passive replies, transient response URLs, trigger expiry, and maximum uses. Depends D08, D13, R02–R03. | `RegisterTrigger`, `ConsumeTrigger`, `TriggerExpired` | Discord 3-second ACK, Lark 3 seconds, QQ 5 seconds, OA 5 seconds, WeCom IR one hour, and DingTalk dynamic expiry require a dedicated lifecycle. |
| R11 `MediaBlobGateway` | Stream upload/download, enforce limits, scan/store temporary blobs, and map process blob refs to D07 handles. Depends R06, R09. | `StoreBlob`, `UploadMedia`, `ResolveMedia` | Binary streams and platform handles must remain process-owned. [Lark §13, S23–S25](../agents/lark.md#13-obcx-design-implications); [Matrix §13, S11, S13](../agents/matrix.md#13-obcx-design-implications) |
| R12 `ExtensionCodecRegistry` | Version, validate, redact, and decode namespaced extension payloads and safe raw references. Depends platform packs. | `DecodeExtension`, `UnknownVersion` | Required by evolving Discord components, X events, WeCom archive/card schemas, Matrix custom events/MSC, Telegram updates, and OneBot segments. |

## 4.4 Platform extension packs — 10

| ID / pack | Responsibility and internal boundaries | Dependencies / common capabilities | Representative request/event | Counterexamples and evidence |
|---|---|---|---|---|
| P01 `discord.*` | Guild/channel types, permission overwrites, intents/shards, forwarded snapshots, polls/components, webhook modes, AutoMod, voice/Stage. | K01–K21 as discovered; R01–R12 | `discord.interaction.Defer`, `discord.gateway.IntentState`, `discord.voice.Connect` | Friends, read receipts, generic video/screenshare, and self-bots are absent. [Discord §10, S2, S5–S18, S21, S24](../agents/discord.md#10-required-namespaced-extensions) |
| P02 `x.*` | X posts/edit chains, social graph, Lists, Bookmarks, timelines/search, media jobs, legacy DM, X Activity, Account Activity, XChat metadata, Spaces summaries, billing. | Common K01–K04/K09 where semantics fit; most SNS operations stay here. | `x.post.Create`, `x.activity.Subscribe`, `x.legacy_dm.Send` | XChat plaintext, calls, Space control/audio, presence, and generic cards remain unavailable. [X §10, S7–S19](../agents/x.md#10-required-namespaced-extensions) |
| P03 `qq.*` | Official QQ C2C, QQ group, Guild/channel, Guild DM, intents, opaque IDs, passive context, streaming, audit, keyboard/Markdown grants. | K01, K02, K04, K09, K11–K15, K17, K21 as probed. | `qq.c2c_stream.Update`, `qq.message_audit.Result` | No numeric QQ identity, general group history/admin, friend list, or OneBot parity. [QQ §10, S3–S6](../agents/qq.md#10-required-namespaced-extensions) |
| P04 `wechat.*` | Separate `official_account`, `mini_program`, `customer_service`, and `open_platform`; follower/publication, notification templates, menus, subscription grants, customer-service state, user-mediated sharing. | K01, K02, K09, K21 where applicable; most notification/publication operations remain namespaced. | `wechat.official_account.PassiveReply`, `wechat.mini_program.SendSubscription`, `wechat.customer_service.Sync` | Personal WeChat automation, personal rooms/history, contacts, calls, and autonomous OP sharing are absent. [WeChat §10, S2–S22](../agents/wechat.md#10-required-namespaced-extensions) |
| P05 `wecom.*` | Internal app, appchat, group robot, intelligent robot, Customer Contact, Customer Service, and conversation archive remain distinct modules with distinct credentials. | K01, K02, K04, K09–K15, K20–K21 where supported. | `wecom.appchat.Send`, `wecom.aibot.Respond`, `wecom.msg_audit.Pull` | Archive is not message history; Customer Contact campaigns are not direct sends; group robot has no ingress. [WeCom §10, S1–S18](../agents/wecom.md#10-required-namespaced-extensions) |
| P06 `lark.*` | Feishu/Lark environment, chat modes, cards, urgent messages, directory, Calendar, Docs/Drive, and custom webhook bot. | Broad K01–K20; Calendar/Docs remain extension-only resource services. | `lark.card.Update`, `lark.calendar.Subscribe`, `lark.docx.MutateBlocks` | No assumed Feishu/Lark parity, presence, typing, call control, or native bot poll send. [Lark §10, S1–S38](../agents/lark.md#10-required-namespaced-extensions) |
| P07 `dingtalk.*` | Tenant distribution, app robot, custom robot, temporary session Webhook, Stream delivery, advanced cards, batch tracking, directory, and specialized robot classes. | K01, K02, K04, K09, K13–K15, K19–K21 as discovered. | `dingtalk.robot.BatchSend`, `dingtalk.card.Update`, `dingtalk.stream.Ack` | Custom robot has no DM/ingress; Stream is not egress; no common history, reactions, moderation, or audit. [DingTalk §10, S1–S19](../agents/dingtalk.md#10-required-namespaced-extensions) |
| P08 `matrix.*` | State events, room versions, relations, spaces, powers, MXC/encrypted files, E2EE device lifecycle, appservice namespaces/transactions, identity services, policy/admin, and unstable MSCs. | Broad K01–K19 where negotiated. | `matrix.SetRoomState`, `matrix.appservice.Transaction`, `matrix.e2ee.Decrypt` | Homeserver is not a guild; AS is not user-installable webhook; polls/widgets/MatrixRTC are not stable common features. [Matrix §10, S3–S20](../agents/matrix.md#10-required-namespaced-extensions) |
| P09 `telegram.*` | Bot API topics, inline mode, Mini Apps, business delegation, guest/ephemeral/managed bots, Stars/payments, file IDs, channel DM/community topology. | Broad K01–K19 where rights permit. | `telegram.inline.Answer`, `telegram.business.Send`, `telegram.forum.CreateTopic` | TDLib user login, history, contacts, presence, calls, secret chats, member enumeration, and admin log are excluded. [Telegram §10, S1–S15](../agents/telegram.md#10-required-namespaced-extensions) |
| P10 `onebot11.*` | Compatibility action/event envelopes, implementation status, CQ/message segments, numeric IDs, request/notice/meta families, implementation-specific actions and transports. | Translate only proven-safe K01–K04/K09/K11–K12; R01–R12. | `onebot11.ActionRequest{action,params,echo}`, `onebot11.Event` | OneBot is not official QQ evidence. Cookies, CSRF, honor, anonymous moderation, forwards, XML/JSON, and consumer-account behavior remain implementation-discovered extensions. [QQ OneBot appendix, S8–S10](../agents/qq.md#onebot-11-adapter-boundary-appendix-not-official-qq-api-evidence) |

---

# 5. Proposed Bridge MVP count and subset

## Recommendation: 26 components

The Bridge MVP should migrate only the currently necessary forwarding lifecycle, not the full 56-component catalog.

| Category | MVP count | Included components |
|---|---:|---|
| Core modules | **10** | D01, D02, D03, D04, D05, D06, D07, D10, D11, D13 |
| Capabilities | **4** | K01 `EventIngress`, K02 `MessageSend`, K04 `MessageMutation`, K09 `MediaTransfer` |
| Runtime infrastructure | **10** | R01–R09 and R11 |
| Extension packs | **2** | P09 `telegram.*`, P10 `onebot11.*` |
| **Total** | **26** | Minimal typed Bridge path |

### MVP data flow

```text
Process-owned adapter
  → K01 typed IngressEvent<MessageCreated|MessageRemoved>
  → actor/store pipeline
  → Bridge actor emits OperationRequest<SendMessage|RemoveMessage>
  → R03 EgressDispatcher
  → R05 explicit account route
  → discovered P09/P10 capability
  → typed OperationResult
  → provider message mapping persisted
```

### MVP exclusions

- No provider history fallback.
- No friends, contacts, honor, anonymous moderation, cookies, CSRF, or credential operations.
- No command catalogs, polls, reactions, member administration, cards, payments, or platform social posts.
- No automatic degradation from unsupported rich content unless the Bridge policy explicitly authorizes a recorded loss.
- Official QQ P03 is not required for the first migration because current QQ integration is OneBot-shaped; it can be added independently later.

---

# 6. Platform-extension boundaries

## 6.1 Must remain namespaced

| Semantics | Reason |
|---|---|
| X posts, edit chains, follows, mutes, Lists, Bookmarks, timelines, Spaces | No second platform has safely aligned SNS lifecycle semantics. |
| WeChat notification templates and subscription grants | Lark urgency and Telegram notifications are not consented typed service-template lifecycles. |
| WeChat/WeCom customer-service state and transient response codes | Contact-center state is not ordinary conversation membership. |
| WeCom archive | A compliance record stream with consent/coverage/encryption is not message history or Discord audit. |
| Discord AutoMod, intents, shards, Stage, Voice Gateway | Platform-specific authorization and protocol lifecycles. |
| QQ C2C streaming and message-audit result | Replace-mode stream and platform content-audit lifecycle are not common message edit/delivery semantics. |
| Lark Card JSON, DingTalk advanced cards, WeCom template cards | Common K15 owns artifact lifecycle only; renderer schemas and update tokens remain namespaced. |
| Matrix state keys, powers, room versions, appservices, E2EE, MSCs | These exceed portable messaging semantics and require protocol-specific state machines. |
| Telegram inline, Mini App, business, Stars, guest/managed bot modes | Product-specific identity, UI, and commercial semantics. |
| OneBot action/event/CQ schemas | Compatibility protocol semantics and implementation support cannot define official QQ or common DTOs. |
| Voice/calls/live/E2EE | Discord voice, Matrix signaling/E2EE, Telegram client calls, and X Spaces do not have safely aligned automation lifecycles. |
| Audit/compliance | Discord audit log, WeCom archive, QQ message audit, and Matrix event history have materially different purpose and guarantees. |

## 6.2 Surface separation requirements

1. `wechat.personal` stays unavailable; OA, MP, KF, and Open Platform are separate adapter modules.
2. WeCom IA, appchat, group robot, intelligent robot, Customer Contact, KF, and archive never share an implicit credential or operation set.
3. X legacy DM and XChat are different message stacks; XChat activity metadata must not imply readable content.
4. QQ official and OneBot 11 are different packs.
5. Telegram Bot API and TDLib must be separate deployments and authority models.
6. Matrix Client-Server bot and administrator-installed Application Service have separate installation and identity rules.
7. Lark and Feishu require explicit environment identity.
8. DingTalk app robot, custom group robot, and specialized robot types require a surface discriminator.
9. Discord Gateway, interaction HTTP, Event Webhooks, incoming Webhooks, and Voice transport remain distinct runtime routes even where they feed common events.
10. Fixed-destination publishers expose K20 only; they never inherit room ingress, history, membership, or moderation.

---

# 7. OneBot 11 and current OBCX migration implications

## 7.1 OneBot 11 disposition

| Current OneBot/OBCX behavior | Migration |
|---|---|
| `send_private_msg` / `send_group_msg` | K02 `SendMessage{installation,ConversationRef,content}` |
| `delete_msg` | K04 `RemoveMessage{MessageRef}` with typed recall/delete semantics |
| `get_msg` | Optional K03 only where the implementation advertises it |
| Image/record/file segments | D05/D07 plus K09; retain unsupported CQ segment variants under `onebot11.*` |
| Group member lookup/kick/ban | K11/K12 only when independently advertised |
| Forward/node, face, poke, honor, anonymous, requests | `onebot11.*`; not part of Bridge MVP |
| Cookies, CSRF, credentials | Removed from actor-visible capability surface; R06 only |
| `status/retcode/data/echo` | Decode to D11 typed result; preserve original envelope as diagnostics |
| Numeric QQ IDs | Scoped OneBot identifiers, never official QQ OpenIDs |
| HTTP/WS/reverse WS | R01 process-owned transport choices |
| Implementation status | Feed R04 capability discovery rather than creating stub methods |

Evidence: OneBot’s compatibility origin, public actions, segments, and transport model are documented in QQ [§OneBot appendix, S8–S10](../agents/qq.md#onebot-11-adapter-boundary-appendix-not-official-qq-api-evidence).

## 7.2 Current OBCX migration sequence

1. **Introduce D01–D13 independently of adapters.** Do not add network methods to DTO modules.
2. **Add R04 capability discovery beside `IBot`.** Derive temporary descriptors from concrete adapter support; never infer support from successful `{}`.
3. **Replace platform-only Bridge lookup.** Route by `InstallationRef` using `source_bot` and explicit target account configuration; no ambiguous compatibility fallback by default. Evidence: [OBCX §Bridge acquisition, F14–F15, F21](../agents/obcx-current.md#bridge-acquisitioncalls).
4. **Add R03 typed egress service.** Bridge emits serializable requests instead of resolving `IBot&` and using `await_asio`.
5. **Implement P09 and P10 capability handlers.** Map existing real operations first; unsupported methods are absent.
6. **Replace opaque JSON results with D11 unions.** Keep raw provider responses only as namespaced diagnostics.
7. **Correct message identity.** Eliminate Telegram `chat_id:message_id` encoding and preserve provider event/update IDs in D10.
8. **Move media through R11.** Actor messages carry `BlobRef` or `MediaHandle`, not arbitrary bytes, URLs, or connection-manager access.
9. **Persist outbound operation IDs and mappings through R08.** A send timeout becomes `OutcomeUnknown`, not blind retry success.
10. **Quarantine legacy `IBot`.** Adapt old callers through a compatibility façade, then delete stubs and unsafe default implementations.
11. **Retain process ownership and shutdown ordering.** Bot I/O contexts, connections, token refresh, and cancellation remain outside reloadable actors.
12. **Add conformance tests.** For every advertised capability, test supported success, permission failure, unsupported discovery, timeout/unknown outcome, and account routing.

---

# 8. Alternative splits considered and rejected

| Alternative | Rejection reason |
|---|---|
| One universal `IBot` with optional methods | Recreates current stubs, semantic leakage, and fake success. Unsupported operations must be omitted/discovered. |
| One interface per provider endpoint | Too fragmented and exposes transport/version churn. The proposed capabilities group one cohesive lifecycle rather than every HTTP route. |
| Universal `Message` covering X posts, OA publications, template notifications, cards, and audit records | These resources have different identity, mutation, delivery, consent, and retention semantics. Commonality would be nominal only. |
| Universal `Guild/Server/Channel` hierarchy | Matrix rooms span homeservers; Lark/DingTalk tenants are not guilds; WeChat lacks rooms; Telegram channels are broadcasts. |
| Universal social graph | X follows/mutes/blocks, OA followers, WeCom customer relationships, enterprise directories, and Discord guild membership are not aligned. |
| One portable card JSON schema | Renderer and update semantics differ materially. K15 normalizes lifecycle and invocation only; render payloads stay namespaced. |
| Treat every Webhook as one capability | Discord incoming Webhooks, Event Webhooks, WeCom/DingTalk/Lark group Webhooks, and callbacks have opposite direction and different identity. |
| Put retry, rate limiting, or sockets in actors | Violates process ownership and creates reload/shutdown/session-state hazards evidenced by every long-lived transport. |
| Common voice/call capability | Discord RTP voice, Matrix signaling, Telegram TDLib calls, and X Spaces are not safely aligned official bot lifecycles. |
| Common audit capability | Discord audit retrieval, QQ content-audit events, WeCom archive, and Matrix room history differ in purpose and guarantees. |
| Count every WeChat/WeCom surface as a top-level extension pack | This would inflate pack count without improving namespace safety. One platform pack may contain rigorously separate surface modules and credentials. |
| One pack combining official QQ and OneBot | Explicitly rejected because OneBot is community compatibility evidence, not Tencent Bot API evidence. |

---

# 9. Conflicts and unknowns requiring runtime discovery or later research

| Issue | Required disposition |
|---|---|
| Discord video/screenshare bot support and review thresholds | Keep video namespaced/disabled; report app verification and privileged-data review separately. |
| X Account Activity entitlement, quote tier, media/DM conflicts | Probe Console/endpoints and expose entitlement state. |
| XChat plaintext operations | Absent until an official content API exists. |
| QQ active sends and WebSocket status | Default active send false and Webhook preferred; enable only after live grant verification. |
| QQ Guild access for newly created apps | Separate discovered capability; do not assume from retained docs. |
| WeChat MP service-message exact types/triggers | Capability-probe by account and avoid importing OA type parity. |
| WeCom read receipts and card-token lifetime | Leave optional/unknown; design to stricter observed deadline. |
| Lark international parity | Require environment-specific probes and tests. |
| DingTalk custom-robot shutdown timing and specialized robot availability | Prefer app robot; expose lifecycle risk in capability state. |
| Matrix E2EE bridge interoperability and unstable MSCs | Require crypto readiness and exact version/feature keys. |
| Telegram new 10.x feature client adoption | Pin Bot API schema and retain unknown update fields. |
| Telegram webhook retry schedule and outbound idempotency | Treat delivery as at-least-once/unknown and retries as potentially duplicating. |
| OBCX source-bot identity and callback mapping | Preserve provider identity/update ID during ingress migration; add adapter tests. |
| OBCX shutdown admission/cancellation | Add explicit egress admission closure and outstanding-operation cancellation before bot teardown. |

Source detail is retained in each artifact’s conflict section: [Discord §12](../agents/discord.md#12-conflicts-and-unknowns), [X §12](../agents/x.md#12-conflicts-and-unknowns), [QQ §12](../agents/qq.md#12-conflicts-and-unknowns), [WeChat §12](../agents/wechat.md#12-conflicts-and-unknowns), [WeCom §12](../agents/wecom.md#12-conflicts-and-unknowns), [Lark §12](../agents/lark.md#12-conflicts-and-unknowns), [DingTalk §12](../agents/dingtalk.md#12-conflicts-and-unknowns), [Matrix §12](../agents/matrix.md#12-conflicts-and-unknowns), [Telegram §12](../agents/telegram.md#12-conflicts-and-unknowns), and [OBCX §Unknowns](../agents/obcx-current.md#unknowns-and-open-questions).

---

# 10. Evidence traceability matrix

| Recommendation | Exact Wave 1 evidence headings / source IDs |
|---|---|
| DTO modules separate from executable capabilities | [Discord §13, S2–S6, S10](../agents/discord.md#13-obcx-design-implications); [Lark §13, S5, S25, S28](../agents/lark.md#13-obcx-design-implications); [OBCX §Concrete migration map, F13, F20–F21](../agents/obcx-current.md#concrete-current-call-to-future-capability-migration-map) |
| No giant `IBot` | [OBCX §Current surface, F1, F3, F6](../agents/obcx-current.md#current-surface-and-coupling); every platform’s §13 |
| One cohesive lifecycle per capability | [Discord §9](../agents/discord.md#9-normalized-common-capability-candidates); [WeCom §9](../agents/wecom.md#9-normalized-common-capability-candidates); [Matrix §9](../agents/matrix.md#9-normalized-common-capability-candidates) |
| Unsupported operations omitted/discovered | [OBCX §Adapter asymmetry, F3, F6](../agents/obcx-current.md#adapter-asymmetry-and-unsupported-behavior); [Telegram §13, S1, S8](../agents/telegram.md#13-obcx-design-implications) |
| Actor-visible messages are serializable and typed | [OBCX §Executive findings, F12–F13](../agents/obcx-current.md#executive-findings); [Discord §13, S2–S3](../agents/discord.md#13-obcx-design-implications); [Matrix §13, S3–S4](../agents/matrix.md#13-obcx-design-implications) |
| Connection-owning transports remain process-owned | [OBCX §Ownership, F17–F18](../agents/obcx-current.md#ownership-reload-and-network-lifecycle); all platform §13 transport recommendations |
| Explicit account routing | [OBCX §Bridge acquisition, F14–F15, F21](../agents/obcx-current.md#bridge-acquisitioncalls); [X §13, S4, S15](../agents/x.md#13-obcx-design-implications) |
| Acceptance, delivery, and read are separate | [WeChat §13, S11, S13–S14](../agents/wechat.md#13-obcx-design-implications); [WeCom §13, S7, S9](../agents/wecom.md#13-obcx-design-implications); [DingTalk §13, S10](../agents/dingtalk.md#13-obcx-design-implications) |
| Dynamic rates, rights, and horizons | Discord/X/QQ/WeChat/WeCom/Lark/DingTalk/Matrix/Telegram §11 |
| OneBot quarantined from official QQ | [QQ §1 and §OneBot appendix, S8–S10](../agents/qq.md#onebot-11-adapter-boundary-appendix-not-official-qq-api-evidence) |
| Telegram Bot API isolated from TDLib | [Telegram §5 and §13, S8–S9](../agents/telegram.md#5-product-vs-official-api-boundary) |
| Matrix extensions negotiated, not assumed | [Matrix §10–§13, S3, S14–S15](../agents/matrix.md#10-required-namespaced-extensions) |

---

## Final recommendation

Adopt the **13 / 21 / 12 / 10** candidate split and implement the **26-component Bridge MVP** first. The key migration seam is not another bot base class: it is a process-owned capability dispatcher accepting serializable typed requests from actors, backed by explicit account routing, capability discovery, durable ingress/outbox state, and strictly namespaced platform extensions.