# bot-portable-events Specification

## Purpose
Define typed received-message, sender, mention, attachment and reply evidence for portable actors, with explicit current platform observations and truthful scope and authority.

## Requirements

### Requirement: Ordinary message consumers receive typed domain events
The common SDK SHALL expose MessageReceived with an exact MessageRef, a typed Sender, optional native sent_at, received_at, ReceivedBody, optional ReplyInfo and optional scoped MediaGroupRef. Ordinary consumers MUST NOT need platform raw JSON, CQ decoding, Telegram entity offsets or a platform-specific send target to read or reply to a message. Platform-specific information needed by current consumers SHALL use named bounded typed observations or explicit extension views. Normalization MUST preserve the genuine source observation identity without requiring a historical event archive or compatibility projection.

#### Scenario: Actor replies to a QQ or Telegram event
- **WHEN** an actor uses the event's message.conversation as the send target and message as reply_to
- **THEN** the adapter routes to the exact source conversation, including internally encoded topic scope, without a platform branch

#### Scenario: Normalization retains source observation identity
- **WHEN** a native message is normalized into the current typed event
- **THEN** its genuine source identity, scope and available evidence remain associated with that observation rather than creating a replacement source observation

#### Scenario: Stored-message consumer adopts typed projection
- **WHEN** a storage actor supplies a normalized projection with a successful stored-message notification
- **THEN** the source identity and notification correlation remain associated with the same typed observation
- **AND** publishing its normalized projection does not admit that observation a second time

### Requirement: Mention evidence is normalized without inventing identity
Adapters SHALL normalize native QQ at and Telegram mention/text_mention evidence, including caption entities, into typed known-user mentions, everyone mentions, or unresolved username mentions. UTF-16 boundaries and username comparison normalization SHALL be handled by the owning adapter and SDK value codec. A known-user mention SHALL use the same UserRef contract as SelfInfo and Sender. Malformed or unresolvable evidence MUST NOT be promoted to a verified user identity; ordinary text MUST NOT manufacture mention or reply evidence.

#### Scenario: Telegram entity follows a non-BMP character
- **WHEN** a valid native mention entity uses UTF-16 offsets after an emoji
- **THEN** the normalized content identifies the intended mention without actor byte-offset arithmetic

#### Scenario: Username mention has no known native user ID
- **WHEN** Telegram supplies a real username mention without a resolvable user ID
- **THEN** the event carries an unresolved username mention with normalized comparison semantics rather than a fabricated UserRef

#### Scenario: Telegram mention entities overlap
- **WHEN** a native text or caption field contains duplicate, nested or partially overlapping mention/text_mention ranges
- **THEN** normalization preserves that field as literal text with an explicit invalid-entities marker and publishes no mention evidence from that field
- **AND** provider entity order cannot choose an author or manufacture partial mentions; adjacent valid mentions remain distinct

#### Scenario: Native username mention lacks its marker
- **WHEN** a Telegram mention entity selects text without a leading @
- **THEN** the field uses the invalid-entities literal fallback instead of borrowing profile-username normalization to manufacture mention evidence
- **AND** profile username comparison remains separate from native mention evidence

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

### Requirement: QQ card metadata is interpreted by the actor
The QQ-specific receiving interface SHALL identify the native card segment kind, including MiniApp, and carry its bounded original metadata with the exact source MessageRef and source block position. The SDK MUST NOT interpret embedded card JSON strings, choose title or URL fields, unescape card text, or decide fallback presentation. Bridge SHALL own that parsing and rendering. This explicit platform extension is not an arbitrary native JSON sending interface and grants no media-read, reply or mutation authority.

#### Scenario: Card contains multiple links and provider-specific fields
- **WHEN** a QQ json, app, ark, miniapp or share segment is observed
- **THEN** the extension preserves its original segment kind and metadata, including unknown fields and embedded JSON string contents, for Bridge to interpret
- **AND** source and normalized block positions remain associated with the original observation

#### Scenario: Embedded card payload is malformed
- **WHEN** the outer message is valid and bounded but its card metadata contains an invalid embedded JSON string
- **THEN** the SDK retains the string rather than dropping the card or substituting a summary
- **AND** Bridge applies its existing parse-failure display policy

### Requirement: QQ music and inline-node presentation observations preserve source data
The owning QQ SDK SHALL expose named typed receive observations for the current music-title and inline-node display paths. Each observation SHALL retain the exact parent source, recorded receive time and native/normalized positions. Missing text SHALL remain distinct from empty text. Inline nodes SHALL distinguish missing content, original literal text, segment arrays and unsupported shapes; array parts SHALL expose only text, actual face IDs and native kind markers. The SDK MUST NOT invent a node author, node MessageRef, media locator or access authority, recursively look up nested content, or choose display labels. Existing specialized-content and forward result layouts SHALL remain unchanged.

#### Scenario: Bridge renders music and an inline node
- **WHEN** the admitted typed message contains captured music or inline-node observations
- **THEN** Bridge renders its existing title/name/part labels from the corresponding typed observations before submitting the whole logical delivery
- **AND** empty titles/names remain empty, while fallback wording for missing fields remains actor policy

#### Scenario: Inline node has no valid original face ID
- **WHEN** a face part is missing or has malformed identity evidence
- **THEN** normalization leaves the unsupported source explicit rather than inventing face zero
- **AND** Bridge cannot silently omit that specialized block or fall through to a legacy send after selecting the typed path

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
The common event layer SHALL provide objective typed evidence, not mandatory core-owned is_self or response-policy flags. SelfInfo SHALL supply the same comparable identity types, while actors retain responsibility for self comparison, unknown relationship handling, sparse rendering and response policy. Migration SHALL remove actor-native parsing without changing command ACL, direct/declinable intent, current objective addressing evidence or ordinary-message rendering merely because a new SDK identity encoding is used.

#### Scenario: Chat LLM migrates its addressing inputs
- **WHEN** Chat LLM receives normalized mentions and reply evidence plus cached SelfInfo
- **THEN** it can compute relationships without native QQ/TG field parsing while retaining its existing marker and decline policy

#### Scenario: Self identity refresh completes
- **WHEN** the cache changes without any source-message edit
- **THEN** earlier observations are not re-admitted, historical relationship evidence is not rewritten, and completed sends are not replayed

### Requirement: Configured scopes preserve authorization semantics
Current configured references SHALL be resolved by the owning platform codec using explicit installation and unambiguous conversation/topic evidence, without actor parsing of opaque IDs. Missing required scope MUST NOT be guessed. Existing group-wide and exact-topic ACL semantics SHALL remain distinct. SDK adoption MUST NOT silently rekey source, session or completed-delivery identities; any persisted representation change SHALL remain an explicitly authorized operator operation rather than a runtime historical conversion.

#### Scenario: Required configured scope is unavailable
- **WHEN** a configured destination cannot be resolved without guessing its conversation kind or required topic
- **THEN** resolution reports unavailability instead of selecting a guessed main conversation

#### Scenario: Existing group-wide command scope is migrated
- **WHEN** an ACL covers a group and its topics
- **THEN** current scope resolution preserves that coverage without parsing IDs in actor business code or broadening an exact-topic ACL

#### Scenario: SDK adoption affects persisted identity representation
- **WHEN** a consumer adopts opaque domain references
- **THEN** adoption alone does not authorize rewriting existing source/session/delivery keys or replaying completed or uncertain effects
