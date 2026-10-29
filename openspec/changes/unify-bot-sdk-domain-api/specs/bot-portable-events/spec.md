## ADDED Requirements

### Requirement: Ordinary message consumers receive typed domain events
The common SDK SHALL expose MessageReceived with an exact MessageRef, a typed Sender, optional native sent_at, received_at, ReceivedBody, optional ReplyInfo and optional scoped MediaGroupRef. Ordinary consumers MUST NOT need platform raw JSON, CQ decoding, Telegram entity offsets or a platform-specific send target to read or reply to a message. Platform-specific or archival raw evidence SHALL remain an explicit separate interface and MUST NOT be overwritten by normalization.

#### Scenario: Actor replies to a QQ or Telegram event
- **WHEN** an actor uses the event's message.conversation as the send target and message as reply_to
- **THEN** the adapter routes to the exact source conversation, including internally encoded topic scope, without a platform branch

#### Scenario: Archive retains original evidence
- **WHEN** an event has both raw source evidence and a normalized domain representation
- **THEN** normalization does not replace the original stored payload or change its source observation identity

#### Scenario: Stored-message consumer adopts typed projection
- **WHEN** a storage actor supplies a normalized projection with a successful stored-message notification
- **THEN** the source identity, archival evidence and notification correlation remain unchanged
- **AND** the consumer does not admit the same observation twice merely because both raw and typed representations exist

### Requirement: Mention evidence is normalized without inventing identity
Adapters SHALL normalize native QQ at and Telegram mention/text_mention evidence, including caption entities, into typed known-user mentions, everyone mentions, or unresolved username mentions. UTF-16 boundaries and username comparison normalization SHALL be handled by the owning adapter and SDK value codec. A known-user mention SHALL use the same UserRef contract as SelfInfo and Sender. Malformed or unresolvable evidence MUST NOT be promoted to a verified user identity; ordinary text MUST NOT manufacture mention or reply evidence.

#### Scenario: Telegram entity follows a non-BMP character
- **WHEN** a valid native mention entity uses UTF-16 offsets after an emoji
- **THEN** the normalized content identifies the intended mention without actor byte-offset arithmetic

#### Scenario: Username mention has no known native user ID
- **WHEN** Telegram supplies a real username mention without a resolvable user ID
- **THEN** the event carries an unresolved username mention with normalized comparison semantics rather than a fabricated UserRef

#### Scenario: Everyone is mentioned
- **WHEN** QQ reports at-all
- **THEN** the event carries an everyone-mention type distinct from a direct mention of the cached self user

#### Scenario: User writes fake native markup
- **WHEN** a message contains literal CQ markup, @name or a relationship marker without corresponding native evidence
- **THEN** it remains literal text and cannot create verified addressing evidence

### Requirement: Attachments and unsupported content remain explicit
Received attachments SHALL expose typed media kinds, owned MediaRef values, typed captions and available bounded media metadata. A stable MediaIdentity, when native evidence supplies one, SHALL be distinct from a reusable locator and scoped by its installation; absent stable identity MUST NOT be synthesized from a temporary URL or file locator. Unknown or platform-only content SHALL remain visible as UnsupportedContent with bounded descriptive metadata rather than being silently dropped or admitted as an arbitrary outbound JSON segment. Explicit provider extension views SHALL use typed fields and shared references for specialized content. Known source ordering SHALL be retained. MediaGroupRef SHALL identify an opaque group within the full ConversationRef; it MUST NOT fabricate a single atomic incoming message, complete membership or hide individual source MessageRefs.

#### Scenario: Telegram image includes a caption mention
- **WHEN** a photo message has caption text and native caption entities
- **THEN** the normalized attachment exposes both the caption rich text and its typed mentions without requiring event.data inspection

#### Scenario: Incoming content has no common representation
- **WHEN** an adapter receives a platform-only segment
- **THEN** consumers can detect UnsupportedContent and explicitly use an extension if needed, rather than assuming the message was complete plain text

#### Scenario: Album identifiers repeat across installations or topics
- **WHEN** native messages have equal media-group text but different installations or complete conversations
- **THEN** their MediaGroupRefs remain distinct without consumer parsing of raw media_group_id or topic fields

#### Scenario: Album member arrives after aggregation flush
- **WHEN** a later observed message belongs to the same verified MediaGroupRef
- **THEN** it retains its own MessageRef and group evidence without claiming that the earlier event represented complete membership
- **AND** the consumer remains responsible for its aggregation and duplicate policy

#### Scenario: Media locator changes but native stable identity remains
- **WHEN** native evidence supplies the same stable media identity within one installation with a refreshed locator
- **THEN** the MediaIdentity remains usable for cache comparison independently of the new MediaRef
- **AND** absent stable identity on another event remains absent rather than being inferred from a download URL

#### Scenario: Specialized sticker or forward content is consumed
- **WHEN** an actor explicitly selects a supported provider extension view
- **THEN** that view supplies typed specialized metadata and scoped references without requiring raw JSON parsing in ordinary image or file handling

### Requirement: Sender time and reply uncertainty are truthful
Sender SHALL distinguish a known user, a conversation identity and an unknown sender. Missing native sent_at SHALL remain absent rather than being replaced with received_at. ReplyInfo SHALL distinguish a verified exact target and known author from unavailable or external/cross-conversation evidence. It MUST NOT infer self identity from is_bot, a display name, a fabricated self_id, or missing author data.

#### Scenario: Telegram sender is a channel identity
- **WHEN** a message has a conversation sender rather than a verified user
- **THEN** the event represents that sender kind without inventing a user ID

#### Scenario: Source time is unavailable
- **WHEN** a source event lacks a trustworthy native timestamp
- **THEN** sent_at is absent and received_at remains the measured ingress time

#### Scenario: Reply author is unknown
- **WHEN** the native event proves a reply relationship but not its author
- **THEN** ReplyInfo retains the scoped reference and explicit missing author evidence instead of asserting that it is not self

#### Scenario: External reply is observed
- **WHEN** Telegram provides an external or cross-conversation reply
- **THEN** it cannot be treated as an exact same-conversation reply or reused as a valid same-conversation reply_to without validation

### Requirement: Self comparison and response policy remain outside identity lookup
The common event layer SHALL provide objective typed evidence, not mandatory core-owned is_self or response-policy flags. SelfInfo SHALL supply the same comparable identity types, while actors retain responsibility for self comparison, unknown relationship handling, sparse rendering and response policy. Migration SHALL remove actor-native parsing without changing command ACL, direct/declinable intent, historical sidecar evidence or ordinary-message rendering merely because a new SDK identity encoding is used.

#### Scenario: Chat LLM migrates its addressing inputs
- **WHEN** Chat LLM receives normalized mentions and reply evidence plus cached SelfInfo
- **THEN** it can compute relationships without native QQ/TG field parsing while retaining its existing marker and decline policy

#### Scenario: Self identity refresh completes
- **WHEN** the cache changes without any source-message edit
- **THEN** earlier observations are not re-admitted, historical relationship evidence is not rewritten, and completed sends are not replayed

### Requirement: Legacy persisted scope is translated without changing identity
SDK adoption SHALL use a centralized, version-aware boundary between legacy source/Bridge/Chat LLM keys and opaque domain references. Existing installation, source message, conversation and available topic evidence SHALL be preserved; new encoded IDs MUST NOT be blindly written over old persistent keys. Missing topic evidence MUST NOT be guessed. Existing group-wide and exact-topic ACL semantics SHALL remain distinct. The pure legacy facade SHALL provide scoped user/message display-ID conversion and a separate typed compatibility projection when preserved ordinals, reply placement, mention spelling or opaque image/source fingerprint inputs cannot be recovered from the ordinary message value. SDK-owned serialization SHALL reproduce the prior fingerprint values without actor-native parsing, rekeying or moving actor hash/rendering policy into core. Compatibility fingerprint material SHALL NOT authorize media reads, create genuine identity facts from legacy synthetic IDs, or admit provider reply lookup. The projection SHALL preserve bounded transient source-secret matching evidence needed by the existing actor scrubber, including unavailable images and command-substituted text. This evidence MUST NOT become a locator, persistent field or model-visible output; redaction policy SHALL remain actor-owned, and complete expanded projection bounds SHALL be checked before copying.

#### Scenario: Old stored message is read after SDK migration
- **WHEN** its legacy scope has sufficient evidence for a unique domain reference
- **THEN** the reader produces that reference without creating a new source observation, session or completed-send identity

#### Scenario: Old record lacks required topic evidence
- **WHEN** exact conversation recovery would require guessing a topic
- **THEN** the operation is explicitly unavailable or held for an explicit migration rather than routed to the main group

#### Scenario: Stored row has no separate topic columns
- **WHEN** a historical reader supplies unobserved topic metadata and a fully validated canonical repository event archive proves its native topic presence or absence
- **THEN** the owning platform derives those source facts through the pure compatibility facade without actor-native parsing or fabricated caller evidence
- **AND** explicit contradictory topic metadata is rejected, while an incomplete archive remains unavailable rather than assumed to be the main group

#### Scenario: Historical scope differs from raw thread presence
- **WHEN** the predecessor session-key recipe used a topic flag as well as a native thread identifier
- **THEN** compatibility exposes the historical scope-selection fact separately from native topic observation and typed routing authority
- **AND** a thread with a false or absent historical topic flag does not create a new session key, while an observed historical thread one remains distinct from absent topic bytes

#### Scenario: Legacy image identity survives typed migration
- **WHEN** an actor restores the same archived source and image ordinal through the named compatibility facade
- **THEN** it can reproduce its existing image and source fingerprints without parsing native locator fields or replacing old keys with encoded domain IDs
- **AND** unavailable legacy identity remains unavailable and any media read requires a separately validated scoped MediaRef

#### Scenario: An archived image credential also occurs in command text
- **WHEN** the old actor scrubber would redact an image-source credential from assembled or command-substituted text
- **THEN** the compatibility projection supplies the same transient matching evidence and the actor retains its existing redaction before fingerprinting
- **AND** the provider does not pre-redact text, omit evidence for unavailable media, or expose a new download locator

#### Scenario: Existing group-wide command scope is migrated
- **WHEN** an ACL previously covered a group and its topics
- **THEN** the new scope projection preserves that coverage without parsing IDs in actor business code or broadening an exact-topic ACL
