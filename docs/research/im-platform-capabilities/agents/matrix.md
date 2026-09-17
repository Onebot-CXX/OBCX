# Matrix platform capability audit

## 1. Lane metadata

- **Platform:** Matrix protocol, not a particular branded client or homeserver.
- **Surfaces audited:** Matrix Client-Server API (CS API), Application Service API (AS API), room/event/state model, media, E2EE, federation boundary, Identity Service API, and non-core widget/integration surface.
- **Research date:** 2026-08-17 UTC.
- **Normative baseline:** Matrix Specification **v1.19** (released 2026-07-08). “Latest” links below resolved to the stable specification at research time; v1.19-specific links are used for lifecycle claims. The `matrix-spec` main branch already contained unreleased v1.20 annotations, which were not treated as v1.19 requirements. [S1][S2]
- **Status interpretation:** `NATIVE` means stable protocol semantics and an official API exist. `API_LIMITED` means support depends on authentication, room power, feature profile/module, server policy, local administrator configuration, or visibility. `EXTENSION` means custom namespaced events or a non-core/unstable MSC. Matrix “modules” are normative spec sections, **not MSC experiments**; however, the feature-profile table makes many modules optional for clients. A conforming server normally supports specified modules unless it targets a restricted feature profile. [S3]
- **Freshness caveat:** Matrix specifies a protocol, not uniform UX. A homeserver can be conforming while applying materially different registration, federation, directory, retention, media, rate, moderation, and account policies. Clients also vary substantially in optional-module and unstable-MSC support.

## 2. Executive findings

1. **Matrix’s common automation substrate is rooms plus arbitrary typed events, not a narrow bot API.** Clients and application services can send standard or namespaced events; state is keyed by `(type, state_key)`, while message events are one-off timeline activity. [S3]
2. **An application service is a homeserver-admin-installed, local trust integration—not a user-installable webhook.** Its YAML registration reserves user/alias/room namespaces and contains shared tokens. It passively observes interested traffic and injects events only by using CS APIs as its bot/virtual users. [S4]
3. **AS delivery is transactionally retryable but at-least-once.** The homeserver queues linearised transactions, retries the same transaction ID with exponential backoff, and the AS must deduplicate. An AS cannot block, rewrite, or veto an event in the send path. [S4]
4. **Rooms are the universal conversation/container abstraction.** DMs are ordinary rooms marked by per-user `m.direct` account data and invite hints; they are not guaranteed to contain only two humans. Spaces are rooms of type `m.space` with state links to children/parents. [S5][S6]
5. **Replies, threads, reactions, edits, and generic references are stable relation semantics.** They remain separate immutable child events; edits do not mutate stored originals, reaction changes require redact-and-resend, and relation metadata must remain cleartext even when event payloads are E2EE so the server can aggregate it. [S7][S8][S9][S10]
6. **Deletion is redaction, not guaranteed physical erasure everywhere.** Redaction strips non-protocol fields according to the room version, is irreversible at protocol level, and can leave membership/state effects intact. [S3]
7. **E2EE is client-managed and optional by feature profile.** Homeservers transport ciphertext, device keys, to-device key messages and backups; clients perform Olm/Megolm, cross-signing, verification, secret storage and attachment encryption. An ordinary unencrypted bot/AS cannot infer plaintext from `m.room.encrypted`. [S11]
8. **Federation is below the adapter boundary.** A CS/AS integration speaks to one local homeserver; that homeserver exchanges signed PDUs and ephemeral EDUs with remote homeservers. Server-server credentials and federation transactions should not leak into business actors. [S12]
9. **Authentication has two stable but incompatible families:** legacy Matrix login/UIA and OAuth 2.0 (added in v1.15). A homeserver may expose either or both; clients discover them separately and must continue with the family that issued the token. OAuth does not cover every legacy automation use case. [S3]
10. **Media is a separate content repository referenced with `mxc://` URIs.** Upload/download and thumbnails are native; authenticated downloads supersede deprecated unauthenticated routes. Limits and thumbnail formats are server policy, and E2EE attachments require client-side encryption and hash verification. [S13][S11]
11. **Sync is token-based long polling, not a generic webhook.** `/sync` can return limited timelines and state deltas; history gaps must be filled through `/messages`, and clients must deduplicate by event ID across APIs. Appservices instead receive push transactions. [S3][S4]
12. **Widgets, polls, rich cards/buttons/forms, and modern MatrixRTC must not be assumed from stable core.** The Matrix.org widget SDK explicitly says widgets are not yet in the Matrix spec; polls remain MSC/extensible-event practice rather than a v1.19 core module. Preserve these as `matrix.*` extensions and probe concrete implementations. [S14][S15]
13. **Capability negotiation is layered, not a single boolean.** Use `/_matrix/client/versions` (including `unstable_features`), `/capabilities`, supported room versions, endpoint/error probing, room state/power levels, and adapter configuration. Product branding is insufficient evidence. [S3]
14. **v1.18–v1.19 materially expanded safety and media semantics:** account lock/suspend admin endpoints, invite blocking, policy servers, animated media flags, image packs, encrypted-history sharing, and mutual-room lookup. Older clients and homeservers may not interoperate uniformly. [S2][S16]

## 3. Research action log

| Action | Result |
|---|---|
| Read the lane brief in full | Established evidence policy, required tables, status vocabulary and OBCX guardrails. |
| Searched Matrix stable CS API, AS API, relations/threads/spaces, auth/identity/E2EE/federation | Search located official specification entry points. The general web fetcher could not directly fetch spec.matrix.org, so the official `matrix-org/matrix-spec` repository was inspected at the source corresponding to the rendered spec. |
| Checked v1.19 and v1.18 changelogs | Confirmed current release and recent lifecycle additions. [S2][S16] |
| Checked CS API core and modules | Inspected event/state/sync/room/membership/power/redaction/version/auth sections and module files for IM, DM, spaces, relations, presence, receipts, typing, push, media, E2EE, moderation and reporting. [S3][S5]–[S13][S17] |
| Checked AS API | Inspected registration, auth, transaction retry, ephemeral delivery, identity assertion, virtual users/devices, timestamp massaging and CS extensions. [S4] |
| Checked S-S and Identity Service APIs | Established federation and 3PID trust boundaries. [S12][S18] |
| Searched official Matrix widget implementation/docs and poll proposal | Widget repository explicitly disclaims stable-spec status; MSC3381 is proposal-level evidence for polls. [S14][S15] |
| Conflicts/inaccessible sources | Direct page extraction failed, but official spec source was available through the official GitHub repository. No unofficial client-automation sources were used. |

## 4. Source register

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

## 5. Product vs official API boundary

“Product” means behavior a conforming Matrix ecosystem can expose, not a promise that Element or another client implements it.

| Surface | End-user/protocol product | Official automation/API boundary | Assessment |
|---|---|---|---|
| Normal user client | Rooms, messaging, state, media, relations, presence, receipts, E2EE | Authenticated CS API access as that user/device; room powers and server policy apply | `NATIVE` / `API_LIMITED` |
| Bot account | A normal Matrix user represented by software | Uses CS API with its own token/device; no special universal “bot” flag | `NATIVE` |
| Application service | Bridge, gateway, virtual-user fleet, server-side integration | Requires homeserver admin YAML registration; receives only “interested” traffic; cannot veto/modify traffic | `API_LIMITED` [S4] |
| Generic webhook | Some products may offer webhook bridges | No universal user-configurable incoming/outgoing webhook in stable core; implement via AS or a bot | `EMULATED` / `EXTENSION` |
| Widgets/integrations | Some clients embed interactive web apps | Widget API is outside stable Matrix spec and permission/client support varies | `EXTENSION` [S14] |
| Federation | Cross-domain rooms/users | S-S API is for homeservers, not bots/business actors | Product: `NATIVE`; adapter API: `UNSUPPORTED` direct [S12] |
| Identity lookup | Email/phone discovery may be offered | Separate Identity Service, separate token/trust/policy; optional deployment | `API_LIMITED` [S18] |
| E2EE bot/bridge | Encrypted UX may be offered | Integration must be a cryptographic device and manage keys; HS/AS transaction alone sees ciphertext | `API_LIMITED` [S11] |
| Moderation/admin | Room moderation; local server lock/suspend | Room powers are broadly available; server-admin module requires local admin and is not cross-server | `API_LIMITED` [S3][S16] |

## 6. Capability evidence table

| Capability | Product support | Official API / status | Restrictions and semantic notes | Evidence | Confidence |
|---|---|---|---|---|---|
| Bot/app identity | Bots are normal users; AS has sender user and virtual users | CS bot `NATIVE`; AS `API_LIMITED` | AS namespace/admin registration; no universal bot badge | S3,S4 | HIGH |
| Authentication | Legacy login/UIA and OAuth 2.0 | `NATIVE` | Server may expose either/both; token families incompatible; OAuth not all automation cases; bearer tokens opaque | S3 | HIGH |
| Multi-account / virtual identities | Clients can hold multiple sessions; AS can own fleets | `API_LIMITED` | One normal token maps to one user/device; AS masquerade restricted to registered local-user namespace and optional device | S4 | HIGH |
| Status | Online/unavailable/offline and status message | `NATIVE` presence module | Visibility to shared-room users; server may time out/limit presence | S17 | HIGH |
| User/profile | MXID, display name, avatar, custom profile fields (version-sensitive) | `NATIVE`/`API_LIMITED` | Remote/profile lookup can be denied outside required visibility; room-member profile is room state | S3 | HIGH |
| Contacts/follow/social graph | No normative contact/follow graph | `UNSUPPORTED`; custom account data `EXTENSION` | User directory and v1.19 mutual rooms are discovery, not consented contacts/follows | S2,S3 | HIGH |
| DM | Marked room UX | `NATIVE` | `m.direct` is per-user account data; room may have >2 users/bots and participants can disagree | S5 | HIGH |
| Group/server/guild/channel | Rooms are groups/channels; homeserver is deployment/identity domain | Room `NATIVE`; guild/server abstraction `EXTENSION` | Do not equate homeserver with a Discord guild; room aliases and directories are separate | S3 | HIGH |
| Space | Hierarchical room grouping | `NATIVE` module | Space is a room; links can be one-way, contextual, cyclic/malformed; hierarchy access depends on visibility | S6 | HIGH |
| Topic/name/avatar/pins | Room state | `NATIVE` | Requires power level for relevant state type | S19,S3 | HIGH |
| Thread | Branched conversation | `NATIVE` stable relation | Root must not already be relation child; no nested threads; optional client feature profile; dedicated list/relations APIs | S7 | HIGH |
| Message create | Standard and custom event types | `NATIVE` `PUT /send/.../{txnId}` | Must be joined/authorised; room powers; 64 KiB complete federated event limit; custom types namespaced | S3 | HIGH |
| Message get/history | Event-by-ID, `/messages`, context, search (module) | `NATIVE`/`API_LIMITED` | History visibility, retention, server gaps and E2EE constrain results/search; opaque pagination tokens | S3 | HIGH |
| Edit | Replacement child event | `NATIVE` stable `m.replace` | Same sender/type/room; no state edits; latest valid replacement rendered; original remains | S8 | HIGH |
| Delete | Redaction | `NATIVE` but semantically limited | Own-event/room-power rules; strips fields per room version, not hard-delete guarantee; state effect may survive | S3 | HIGH |
| Reply | `m.in_reply_to` | `NATIVE` | Not a `rel_type`; reply notification requires explicit `m.mentions`; old fallback behavior differs | S8 | HIGH |
| Quote | Reply can be displayed with context | `EMULATED` | No distinct durable “quote” semantic independent from rich reply; copied quote text is just content | S8 | HIGH |
| Forward | Clients can resend content | `EMULATED` | No stable provenance-preserving forward operation/event; use namespaced metadata if required | S3 | MEDIUM |
| Relations/reference | Parent/child event graph | `NATIVE` | Same room; cleartext relation metadata under E2EE; visibility/history can make aggregation incomplete | S10 | HIGH |
| Rich text | Plain body plus sanitized custom HTML subset | `NATIVE` | Client rendering subset/HTML safety variance; extensible events MSC is not stable core | S19 | HIGH |
| Mentions | User IDs / room mention metadata | `NATIVE` | Notification depends on push rules; encrypted mentions require decrypting event | S19 | HIGH |
| Reactions | `m.reaction` + `m.annotation` | `NATIVE` | Any string key; same-user duplicate is one; change via redact + new reaction; client module optional | S9 | HIGH |
| Stickers/custom emoji | `m.sticker`; v1.19 image packs/emoticons | `NATIVE` | Image packs are new in v1.19; pack media/state not encrypted even in E2EE rooms | S2,S19 | HIGH |
| Polls | Common clients may implement polls | `EXTENSION` (MSC3381) | Not a v1.19 normative module; event names/client behavior and stable migration vary | S15 | HIGH |
| Cards/buttons/forms | Possible with HTML constraints, custom events or widgets | Core `UNSUPPORTED`; `EXTENSION` | Arbitrary HTML is not an interaction protocol; widgets are non-core and permissioned | S14,S19 | HIGH |
| Image/audio/video/file | Standard `m.room.message` msgtypes | `NATIVE` | Upload first; captions/metadata client variance; server size/content policy; E2EE attachment schema differs | S13,S19 | HIGH |
| Media groups/albums | Multiple events/relations possible | `EMULATED`/`EXTENSION` | No stable atomic media-group contract | S10,S19 | MEDIUM |
| Upload/download/thumbnail | Content repository and `mxc://` | `NATIVE`/`API_LIMITED` | Upload max via server configuration; authenticated downloads preferred; remote media may be blocked/cached; thumbnail types unspecified | S13 | HIGH |
| Members | Invite/join/knock/leave/kick/ban; list members | `NATIVE` | Join rules, history visibility, room version, federation reachability and power levels apply | S3 | HIGH |
| Roles/permissions | Numeric per-room power levels | `NATIVE` | Not named roles; defaults/event-specific thresholds; room v12 creator has immutable infinite level | S3 | HIGH |
| Moderation | Redact, kick, ban, ignore, report, ACLs/policy lists | `NATIVE`/`API_LIMITED` | Reports handled by server policy; policy-list enforcement intentionally implementation-defined; ACLs target servers, not users | S3,S17,S20 | HIGH |
| Audit log | Products may have proprietary admin audit | `UNSUPPORTED` stable common API | Event history is not a complete security/admin audit log; admin module does not standardise universal audit export | S20 | MEDIUM |
| Commands/interactions | Bots can parse messages/custom events | `EMULATED`; custom event `EXTENSION` | No stable slash-command registration, callback, button, modal or interaction-ack API | S3,S14 | HIGH |
| Webhooks/event subscriptions | AS receives pushed transactions | AS `API_LIMITED`; generic webhook `UNSUPPORTED` | Admin-installed namespaces only; at-least-once, cannot veto; ordinary clients poll `/sync` | S4 | HIGH |
| Presence | Per-user aggregated presence | `NATIVE` module | Privacy exposure; interested/shared-room scope; implementation/server policy variance | S17 | HIGH |
| Typing | Room-scoped ephemeral set | `NATIVE` module | Full set replaces prior set; refresh timeout; not event history; AS only if `receive_ephemeral` | S17,S4 | HIGH |
| Read receipt | Public/private and threaded read-up-to | `NATIVE` module | Private only returned to sender; public receipt federation; not proof a human read | S7,S17 | HIGH |
| Push/notifications | Pushers, gateways and user push rules | `NATIVE`/`API_LIMITED` | Requires push gateway/provider; client profile optional except mobile; E2EE notification content constrained | S17 | HIGH |
| Feed/post/repost/quote/follow | Can model rooms/custom events | `UNSUPPORTED` as normative social API; `EXTENSION` | Matrix core is room/event messaging, not an SNS graph/feed contract | S3 | HIGH |
| Voice/video calls | Legacy VoIP signaling events/TURN module | `API_LIMITED` | Signaling is in Matrix; media path is external; client feature/profile and TURN policy vary; modern MatrixRTC should be namespaced/MSC-gated | S3 | MEDIUM |
| Live/voice space | Product implementations exist | `EXTENSION` | No stable generic Twitter/Discord-style live-space capability inferred from stable core | S3 | MEDIUM |
| E2EE | Encrypted rooms, devices, verification, backups | `API_LIMITED` optional module | Client-side crypto/device lifecycle required; metadata and relations leak as designed; bridges need explicit crypto support/trust | S11 | HIGH |
| Federation | Federated room participation | Product `NATIVE`; direct bot API `UNSUPPORTED` | Homeserver-only S-S API, signed PDUs/EDUs; room may disable/block federation; local policy and outages | S12 | HIGH |
| Tenant/compliance | Self-hosting/local policy possible | `EXTENSION`/`API_LIMITED` | No universal tenant object, retention/eDiscovery/legal-hold API contract; admin facilities implementation-specific | S12,S20 | HIGH |
| Identity/3PID | Optional email/phone mapping | `API_LIMITED` | Separate Identity Service, separate token/ToS/trust; privacy-preserving hash lookup; not reverse social graph | S18 | HIGH |
| Idempotency | Client txn IDs; AS txn IDs | `NATIVE` | CS scope is device + endpoint; AS must dedupe retries; state endpoint has no txn ID | S3,S4 | HIGH |
| Pagination | `/messages`, relations, threads, directory, etc. | `NATIVE` | Tokens opaque and endpoint-specific; ordering can differ from `/sync`; dedupe event IDs | S3,S7,S10 | HIGH |
| Rate limits | Standard 429 / `M_LIMIT_EXCEEDED` | `API_LIMITED` | No universal numeric quota; honor `Retry-After`; old `retry_after_ms` deprecated | S3 | HIGH |
| Passive-reply window | None | `UNSUPPORTED` | No WhatsApp-style 24-hour automation window in protocol; powers/policy/rate limits still apply | S3 | HIGH |
| Payload/message limits | Event max 65,536 bytes canonical federated form | `API_LIMITED` | Media separate; server may impose lower request/media limits and return `M_TOO_LARGE` | S3,S13 | HIGH |

## 7. Inbound event inventory

### 7.1 Client/device ingress (`/sync`)

Delivery is token-based HTTP long polling. Initial sync supplies recent room timelines and state at the **start** of each timeline; incremental sync uses `next_batch`. A limited timeline signals a gap and supplies a state delta; backfill uses `/rooms/{roomId}/messages`. [S3]

| Section/event class | Examples | Notes |
|---|---|---|
| Joined-room timeline | Any visible room event: `m.room.message`, state changes, redactions, reactions, edits, thread events, custom namespaced events, `m.room.encrypted` | Validate untrusted event shapes; dedupe by `event_id`. |
| Joined-room state | Current/delta state keyed by `(type,state_key)` | State list is not itself timeline order. |
| Invite/knock stripped state | `m.room.create`, name/avatar/topic/join rules/encryption and invite/knock membership | Incomplete, unsigned view; discard when full state is available. |
| Left rooms | Timeline/state if included | Leave rooms normally disappear unless filter requests them; forgotten rooms disappear until re-entry. |
| Room ephemeral | `m.typing`, `m.receipt` | Not persisted room DAG. |
| Global presence | `m.presence` | Interested users/shared-room scope. |
| Account data | `m.direct`, push/client config, tags, secrets metadata, image-pack refs, custom types | User/room account-data scopes; not federated room state. |
| To-device | Key verification, Olm/key requests, secrets and arbitrary to-device types | Device-targeted, outside room timeline. |
| E2EE extensions | device-list changed/left, one-time-key counts, fallback-key usage | Crypto clients must reconcile persisted device state. |

### 7.2 Application-service ingress

The homeserver calls `PUT /_matrix/app/v1/transactions/{txnId}` using bearer `hs_token`; the AS returns success and deduplicates repeated IDs. [S4]

| Transaction field | Deliverable content | Gate |
|---|---|---|
| `events` | Linearised persistent events for matching room/alias/local-user namespaces, including remote-user events when the room is otherwise interesting | Registration namespace interest. An encrypted room yields `m.room.encrypted`, not plaintext. |
| `ephemeral` | `m.presence`, `m.typing`, `m.receipt` | Registration `receive_ephemeral`; private receipts only for AS-namespace users. |
| to-device data | Device-targeted events for AS-controlled users/devices | Registration/implementation support and correct virtual device identity. |
| device-list/key counts | E2EE device changes and one-time-key information needed by encrypted AS clients | Registration E2EE settings; AS v1.17 device support. |
| Query callbacks | User/alias existence provisioning; third-party protocol/user/location lookup; ping | Separate HS→AS endpoints; provisioning query can block the initiating HS request. |

**Not inbound:** a pre-send interception hook. The AS is explicitly passive and cannot modify or prevent an event. [S4]

## 8. Outbound operation inventory

| Operation family | Principal endpoints/events | Result / error / async semantics |
|---|---|---|
| Send timeline event | `PUT /rooms/{roomId}/send/{eventType}/{txnId}` | Synchronous JSON returns `event_id`; txn ID makes retransmission idempotent per device+endpoint; federation continues asynchronously. Standard Matrix errors. [S3] |
| Send state | `PUT /rooms/{roomId}/state/{type}/{stateKey}` | Returns `event_id`; no transaction ID on state endpoint, so caller must reconcile retries. Power/auth rules apply. [S3] |
| Redact | `/redact/{eventId}/{txnId}` or v1.18-supported send of `m.room.redaction` | Accepted event ID; irreversible protocol redaction after room-version authorization. [S3][S16] |
| Relations | Send child event with `m.relates_to`; GET `/relations`; thread-list endpoint | Normal event send result. Aggregations may be incomplete because of visibility/missing history and are advisory snapshots. [S7][S10] |
| Read/history | `/sync`, `/messages`, `/event`, `/context`, state/members, search | Opaque cursors; sync may be limited; event access depends on membership/history/policy; encrypted search cannot rely on server plaintext. [S3] |
| Room lifecycle | create, alias, join/knock/invite/leave/forget, kick/ban/unban, directory visibility, upgrade | Standard error objects; remote joins/federation may add network latency/failure; permissions and room version apply. [S3] |
| Media | upload/create, authenticated download, thumbnail, config | Upload returns content URI; binary download exception to JSON; 413 `M_TOO_LARGE`, 403 and remote-fetch errors are policy-dependent. [S13] |
| Ephemeral | typing endpoint; receipt/read-markers; presence | Accepted update has no durable timeline event; typing expires/refreshes; public/private receipt visibility differs. [S17] |
| Push config | pushers and push rules | Configures homeserver→push gateway delivery; it does not synchronously guarantee end-device delivery. [S17] |
| To-device/E2EE | sendToDevice; keys upload/query/claim/changes; backup; signing/verification/secret storage | Txn-id idempotency for to-device send; key consistency and retry state must be persisted by device. [S11] |
| Profile/account/device | profile, account data, devices, logout, 3PID/account management | Capabilities and auth-family constraints; UIA-sensitive legacy endpoints may be unusable with OAuth tokens. [S3] |
| AS virtual-user actions | CS operations with AS token + `user_id` and optional `device_id`; register virtual user; timestamp massage | Restricted to namespace; account management excluded; unknown device gives `M_UNKNOWN_DEVICE`; `ts` changes displayed timestamp, not DAG order. [S4] |
| Reports/moderation/admin | report event/room/user; room powers/kick/ban/redact; local lock/suspend | Report handling async/implementation-defined. Admin calls require local admin and affect local users only. [S16][S17][S20] |

All Matrix API errors generally use `{errcode,error,...}`; rate limiting is HTTP 429/`M_LIMIT_EXCEEDED` with `Retry-After`. OAuth token/registration errors follow their referenced RFCs rather than Matrix error JSON. [S3]

## 9. Normalized common-capability candidates

Safe common DTO/capability candidates, each narrower than Matrix’s full semantics:

1. **Conversation/room reference:** `{platform, conversationId, kind?, displayName?}`. Map Matrix rooms and DMs, but keep `isDirect` as advisory metadata rather than cardinality. [S5]
2. **Immutable message/event:** stable IDs, sender, timestamp, type, body/structured content, raw extension. Never model edit as in-place database mutation; expose a derived current view plus original/replacement IDs. [S3][S8]
3. **Send message with idempotency key:** common request/result with platform event ID; Matrix `txnId` maps well, while state sends must declare lack of native idempotency. [S3]
4. **Reply reference:** target message/event ID. Keep “mention replied sender” independent. Do not promise quote fallback or cross-room reply. [S8]
5. **Reaction add/remove:** `{targetId,key}`; removal maps to redaction of the caller’s reaction event, requiring stored reaction-event ID. [S9]
6. **Thread reference:** root event ID plus optional reply target. Capability flags must state no nesting and distinguish thread root from latest reply. [S7]
7. **Attachment:** metadata plus opaque platform media reference, upload/download operations, optional encrypted descriptor. Do not expose raw `mxc://` as a universal URL. [S13][S11]
8. **Membership change and authorization result:** invite/join/leave/kick/ban can generalize; preserve knock/restricted-join and numeric power details in `matrix.*`. [S3]
9. **Ephemeral activity:** typing, presence and read-up-to receipt, each independently discoverable. Treat receipts as protocol acknowledgements, not proof of human reading. [S17]
10. **Inbound event envelope:** delivery ID, platform event ID, room, sender, event class, typed payload, raw payload; processing outcome independent from business actor. AS delivery is at-least-once, CS sync is cursor-based. [S3][S4]
11. **Capability/constraint report:** supported, auth mode, permissions, encryption readiness, max event/media size if known, rate-limit policy unknown/dynamic. [S3][S13]
12. **Moderation primitives:** redact, kick, ban and report generalize, but server lock/suspend, ACLs and policy rooms remain namespaced. [S16][S20]

## 10. Required namespaced extensions

| Extension | Why it must remain `matrix.*` |
|---|---|
| `matrix.event.type`, `matrix.event.raw_content`, `matrix.state_key` | Arbitrary namespaced event/state model is broader than common messaging DTOs. |
| `matrix.room.version`, `matrix.room.create`, `matrix.auth_events` (raw only where needed) | Authorization/redaction/event validity depends on room version; business code should not implement federation auth. |
| `matrix.power_levels` | Numeric per-event thresholds and v12 creator semantics do not map safely to named roles. |
| `matrix.relation` | `m.thread`, `m.replace`, `m.annotation`, `m.reference`, and `m.in_reply_to` have distinct validation/aggregation rules. |
| `matrix.space.parent/child/hierarchy` | One-way links, `via`, canonical parent, recursion and visibility are Matrix-specific. |
| `matrix.direct.account_data` | Per-user advisory DM map is not a universal DM guarantee. |
| `matrix.mxc` / encrypted-file descriptor | Media indirection, authenticated proxy fetch and AES-CTR descriptor are Matrix-specific. |
| `matrix.e2ee.*` | Devices, Olm/Megolm sessions, cross-signing, secret storage, withheld keys, backups and history sharing require dedicated crypto actors. |
| `matrix.appservice.*` | Namespace regexes, virtual users/devices, AS transaction IDs, timestamp massage and third-party protocol directories are admin integration concepts. |
| `matrix.identity.*` | 3PID hashes, pepper negotiation, identity-server trust and separate tokens are not profile/contact semantics. |
| `matrix.msc.*` / unstable feature key | Polls, widgets, MatrixRTC and any unstable endpoint/event must carry exact MSC/version namespace and never silently masquerade as stable common support. |
| `matrix.policy.*`, `matrix.server_acl`, `matrix.admin.*` | Enforcement and scope differ (client/room/server/local account), often administrator-only. |

## 11. Limits, policy, review, regional and lifecycle risks

- **Hard event bound:** complete canonical federation event ≤65,536 bytes; `type` and `state_key` ≤255 bytes. Media is separate. [S3]
- **No universal media/message count/quota:** media config may advertise upload size, but storage quotas, remote-media policy, thumbnails, retention and lower reverse-proxy limits vary. Expect `M_TOO_LARGE`, `M_USER_LIMIT_EXCEEDED`, `M_RESOURCE_LIMIT_EXCEEDED` and `M_FORBIDDEN`. [S3][S13][S16]
- **No stable numeric rate contract:** homeserver-specific rate limits; back off using `Retry-After`. AS delivery has separate exponential retry controlled by the homeserver. [S3][S4]
- **Power and policy are contextual:** check current `m.room.power_levels`, membership, join rules and history visibility per room. Cached authorization can race state changes. [S3]
- **Federation policy:** rooms can be non-federated at creation; server ACLs, policy servers, DNS/TLS/signing failures and remote server policy affect reachability. A successful local send does not imply immediate global delivery. [S12][S20]
- **Registration/review:** normal account registration may be closed, token-gated, CAPTCHA/terms/SSO controlled, or OAuth-web-only. AS installation always requires homeserver-admin action and token/namespace review. [S3][S4]
- **E2EE operational risk:** crypto state is per-device and persistent. Token/device loss, unverified devices, key backup policy, history-sharing versions, and bridge trust affect decryptability. Never downgrade an encrypted room silently. [S11][S2]
- **Authenticated media migration:** old unauthenticated media paths are deprecated and servers may freeze access for newly cached/uploaded media; adapter must use authenticated client media endpoints where specified. [S13]
- **Privacy:** presence can expose activity to any shared-room user; identity lookup is privacy-sensitive; reports and policy-list subscriptions can disclose interests. [S17][S18][S20]
- **Regional/commercial:** the open specification defines no universal region or paid tier. Hosting providers may add regional, quota, paid or compliance policies; discover at runtime and do not encode provider pricing as protocol.
- **Lifecycle:** v1.19 image packs and encrypted history sharing, v1.18 safety/OAuth additions, and earlier stable relations will have uneven rollout. `/versions` showing a spec version is stronger than brand/version strings, but client rendering still varies. [S2][S16]
- **Unstable MSCs:** accepted practice or popular implementation is not stable specification. Require explicit opt-in to `unstable_features`/MSC endpoints and preserve wire names for migration. [S3][S14][S15]

## 12. Conflicts and unknowns

1. **“Optional modules” wording:** The spec says modules are normative and not experimental, and compliant servers must support them unless targeting a profile; its feature-profile table nevertheless labels many modules optional for clients. Therefore this report calls their wire semantics **stable** while capability support/rendering remains profile/implementation-dependent. [S3]
2. **Polls:** Common Matrix clients have shipped poll UX, but no v1.19 poll module appears in the normative module list; MSC3381 remains the safe evidence classification. **Conclusion: `EXTENSION`, not `NATIVE`.** [S3][S15]
3. **Widgets:** Official SDK exists, but its README explicitly says widgets are not yet in the Matrix spec. **Conclusion: implementation extension with client-granted capabilities, not CS/AS normative support.** [S14]
4. **Sliding Sync / MatrixRTC:** These have active ecosystem implementations and MSC histories, but this audit did not find them as v1.19 normative modules. Exact supported MSC names/endpoints on a chosen homeserver/client are **UNKNOWN until `/versions` and implementation docs are checked**.
5. **Application-service E2EE delivery fields:** v1.17+ supports virtual devices/cross-signing and registration-controlled crypto traffic, but actual encrypted bridge interoperability still depends heavily on homeserver and bridge implementation. Plain AS transactions never imply decryption. [S4][S11]
6. **Hard deletion/compliance:** Redaction is specified; physical erasure across backups, remote federated servers and clients is not guaranteed. Retention, export, legal hold and audit APIs are implementation/provider-specific and therefore **UNKNOWN** as a portable contract. [S3][S12]
7. **Voice/video:** Legacy VoIP signaling is stable and required for several client profiles, but media transport/client UX and modern group calling differ. Exact group-call/MatrixRTC support is **UNKNOWN** without target implementation evidence. [S3]
8. **Numeric limits:** Aside from event/key bounds, standard quotas and rate values are not fixed. Query concrete homeserver config/capabilities and observe errors. [S3][S13]

## 13. OBCX design implications

1. **Use small capability interfaces, not `IBot`.** Suggested process-owned adapters: `MatrixSession`, `RoomEventReader`, `RoomEventSender`, `RoomStateStore`, `RelationService`, `MediaRepository`, `MembershipModerator`, `EphemeralService`, `AppserviceIngress`, and a separately isolated `MatrixCryptoDevice`. Enable only capabilities discovered from spec version, auth family, room state/powers and config. [S3][S4][S11]
2. **Keep network ownership in the adapter process.** `/sync` loops, AS HTTP listeners, retry queues, rate backoff, media streaming, federation-facing homeserver interaction and crypto machines must not live in business actors. Actors receive serializable request/result/event messages. [S3][S4][S12]
3. **Canonical ingress DTO:** `MatrixInboundEnvelope { deliveryMode, deliveryId?, syncCursor?, eventId?, roomId?, sender?, type, stateKey?, originServerTs?, unsigned?, content, raw }`. Preserve `raw`; validate before mapping because event bodies are untrusted and custom types are valid. [S3]
4. **Separate durable, state and ephemeral events.** Use distinct union cases for timeline event, state replacement, account data, to-device, presence, typing and receipt. Never append `/sync.state` to a user-visible timeline. [S3][S17]
5. **Model edits/redactions/relations as events.** Store immutable originals plus relation event IDs; materialize a current view. Reaction removal needs the original reaction event ID. Redaction must not be represented as guaranteed purge. [S8][S9][S10]
6. **Make outbound idempotency explicit.** `SendRoomEvent(requestId/txnId)` can promise native deduplication; `SetRoomState` cannot claim the same guarantee. Persist AS transaction IDs and return success only after durable deduplication state. [S3][S4]
7. **Crypto boundary:** encrypted content should enter business logic only after a dedicated crypto component emits `DecryptedRoomEvent` with verification/trust metadata. Also retain ciphertext/raw envelope for diagnostics without keys. Missing keys is a typed outcome, not an empty message. [S11]
8. **Permission preflight is advisory.** Compute from membership and power levels, but still send and handle authoritative server error because state can race. Preserve room version and required level in `matrix.authorization` diagnostics. [S3]
9. **Capability discovery response:** include stable spec versions, `unstable_features`, `/capabilities`, authentication APIs, supported room versions, configured AS features, E2EE readiness, room powers, media max if advertised, and tested optional endpoints. Treat `M_UNRECOGNIZED` as absence. [S3][S4][S13]
10. **Do not normalize homeserver as workspace/guild.** A room can span homeservers; identity domain and conversation administrative boundary differ. Model spaces separately and permit rooms in multiple spaces. [S6][S12]
11. **Typed extension escape hatch:** allow namespaced custom events, MSC event names and widget messages only through explicit `matrix.*` DTOs plus raw JSON. Never map an unstable poll/widget/RTC implementation to a stable common capability without exact feature negotiation. [S14][S15]
12. **History reducer must handle gaps.** Persist `next_batch`; when `limited`, enqueue backfill from `prev_batch`; dedupe by `event_id`; do not assume arrival order equals DAG/topological ordering. [S3]
13. **Media abstraction must be streaming and auth-aware.** Keep `mxc://` opaque, proxy downloads through the authenticated homeserver endpoint, enforce size/content limits before buffering, and carry an optional encrypted-file descriptor. [S13][S11]
14. **AS identity must be explicit per request.** Include acting user and optional device in the process message; enforce configured namespaces locally before network calls. Never let arbitrary business input select `user_id`. [S4]
15. **Moderation result types should distinguish scope:** room redact/kick/ban, local homeserver lock/suspend, report accepted, policy recommendation observed, and server ACL change are not interchangeable. [S16][S20]

## 14. Claim-to-source checklist

| Conclusion | Sources |
|---|---|
| E1 / D1–D3: event-centric API and AS boundary | S3,S4 |
| E2–E3 / D14: admin-installed AS, namespaces, at-least-once transactions | S4 |
| E4 / D10: rooms, DMs and spaces | S3,S5,S6,S12 |
| E5–E6 / D5: relations, immutable edits, reactions and redactions | S3,S7,S8,S9,S10 |
| E7 / D7: client-owned E2EE and bot/bridge limitation | S11,S4 |
| E8 / D2: federation below adapter boundary | S12 |
| E9: dual authentication families and automation gap | S3 |
| E10 / D13: media repository, authentication and encrypted files | S13,S11 |
| E11 / D12: sync gaps/cursors and AS push | S3,S4 |
| E12 / D11: widgets/polls/RTC must remain extensions | S14,S15,S3 |
| E13 / D9: layered capability discovery | S3,S4,S13 |
| E14 / D15: v1.18–v1.19 lifecycle/moderation changes | S2,S16,S20 |
| D4: typed event classes and state not timeline | S3,S17 |
| D6: differentiated idempotency | S3,S4 |
| D8: advisory power preflight | S3 |

---

### Bottom line

Matrix provides an unusually broad, open, stable **room/event protocol** and a powerful **administrator-installed application-service integration**. OBCX should expose its portable messaging subset through granular discovered capabilities, while retaining Matrix’s state keys, relations, room versions, E2EE device semantics, appservice identity, media descriptors and unstable MSCs under explicit `matrix.*` types. The adapter must assume retries, sync gaps, heterogeneous clients, server policy, and ciphertext—not uniform product behavior.
