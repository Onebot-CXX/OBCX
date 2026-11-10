# bot-portable-queries Specification

## Purpose
Define cache-only self lookup, truthful member views, evidence-bound reply resolution, bounded media acquisition and local installed-capability discovery.

## Requirements

### Requirement: QQ file-ID acquisition is scoped and read-only
The QQ-specific SDK SHALL provide an owned file-ID read request with a full installation-scoped group/private ConversationRef and an explicit byte limit. Native file IDs SHALL remain provider values, not caller filesystem paths. The adapter SHALL retain the original account authority and one deadline through the native URL query and bounded credential-isolated byte acquisition. This interface SHALL NOT provide arbitrary file management or a core cache service. Bridge SHALL retain responsibility for caching, filenames, conversions, final paths, sending policy and cleanup.

#### Scenario: Bridge reads a QQ group or private file
- **WHEN** Bridge supplies a valid QQ conversation, native file ID and byte limit
- **THEN** the adapter uses the matching named group/private query and returns File-kind owned bytes with their actual size
- **AND** no signed locator, provider path or invented stable media identity appears in returned metadata

#### Scenario: Native file query or byte acquisition loses its original authority
- **WHEN** cancellation, deadline expiry or account retirement interrupts either stage
- **THEN** work is drained under the original call ownership and no replacement account or automatic retry is selected

#### Scenario: Provider file metadata disagrees with completed bytes
- **WHEN** an explicit file-size declaration differs from the completed download, declarations conflict, or the resource is empty or exceeds the caller's limit
- **THEN** the read does not return a successful MediaBlob and the failure does not expose the private locator

#### Scenario: Received QQ file has a direct URL and no usable file ID
- **WHEN** an authentic live file segment supplies a valid HTTP resource but its file ID is absent, empty or malformed
- **THEN** the adapter may retain the original origin-bound private read evidence and return bounded File bytes through media.read
- **AND** that selected URL recipe does not fall back to a guessed file ID or replacement account when evidence expires; archive restoration alone cannot mint it

#### Scenario: Unsupported historical file reference is supplied
- **WHEN** a historical representation does not distinguish an actual provider file ID from a display filename
- **THEN** it is rejected rather than imported or used to authorize a guessed native file query
- **AND** current explicit file-ID vocabulary does not authorize rewriting existing source or storage keys

#### Scenario: A forwarded node contains a scoped file identifier
- **WHEN** a forward lookup observes a file ID without proof of its original file conversation
- **THEN** it preserves typed file metadata and source position without using the outer forward's conversation to authorize a file read

### Requirement: Self lookup is part of the common typed client
BotClient SHALL expose get_self(GetSelfRequest) for the exact BotInstallationRef and retain the action bot.self.get. Its SelfInfo SHALL expose a scoped UserRef and optional display-name and username values using the same identity/comparison semantics as normalized events. The DTO SHALL be protected by the new SDK contract gate; preceding installation/user_id layouts SHALL be rejected by the current SDK rather than treated as binary-compatible public values. The query MUST NOT classify messages, compute is_self, or change actor response policy.

#### Scenario: Either supported platform is queried
- **WHEN** an actor calls get_self for a valid QQ or Telegram installation
- **THEN** the same GetSelfRequest and SelfInfo types are used and no provider-specific client is required

#### Scenario: Actor compares a known sender with self
- **WHEN** a verified sender and SelfInfo refer to the same native account in the same installation
- **THEN** their UserRef values compare equal without numeric parsing or platform checks

#### Scenario: Profile lacks a display name
- **WHEN** the cached validated account has no display name
- **THEN** SelfInfo marks that field absent and does not invent an empty-name identity or persona

### Requirement: Self lookup preserves the existing cache-only contract
The common self query SHALL read the existing process-owned SelfIdentityCache and MUST NOT start, wait for, or request provider refresh, including on cache miss. Empty, invalidated or closed caches SHALL return CacheUnavailable, DefinitelyNotSubmitted and no caller-driven retry instruction. No second actor-owned identity cache or query-triggered refresh schedule SHALL be introduced.

#### Scenario: Initial identity has not loaded
- **WHEN** get_self is called before the first successful background refresh
- **THEN** it immediately returns CacheUnavailable without a native getMe/get_login_info call or waiting for the active lookup

#### Scenario: Many actors read during refresh
- **WHEN** several actors concurrently query one installation
- **THEN** they receive a valid existing snapshot or explicit unavailability and create no extra refresh work

### Requirement: Self publication preserves account and lifecycle isolation
Self cache ownership SHALL remain scoped by installation and account/connection generation. Background QQ get_login_info and Telegram getMe reads SHALL validate native identity, including Telegram is_bot. Reconnect, account conflicts and successful runtime reload SHALL retain the previously specified invalidation/refresh semantics, concurrent refresh coalescing and old-generation result rejection. Same-generation failures SHALL retain only an already valid snapshot; validation-only and rejected candidates MUST NOT mutate live cache state. This change SHALL preserve the previously approved refresh configuration policy and MUST NOT introduce or broaden any configuration default.

#### Scenario: Old account lookup completes late
- **WHEN** a refresh begun before account replacement completes after invalidation
- **THEN** it cannot publish the old UserRef or username into the current cache

#### Scenario: Candidate validation fails
- **WHEN** a reload candidate is rejected before activation
- **THEN** it neither refreshes nor changes the live identity cache

#### Scenario: Telegram response does not describe a bot
- **WHEN** getMe returns invalid identity or is_bot is not true
- **THEN** the background refresh rejects that response rather than publishing a successful SelfInfo

### Requirement: Member queries return a truthful common view
GetMemberRequest SHALL identify an exact conversation and same-installation UserRef. Adapters SHALL return common member profile fields and independently optional role/membership evidence. Display-name selection SHALL occur in the adapter, including QQ card-before-nickname and Telegram first/last-name composition. Unsupported/private conversation queries, unknown fields and permission failures SHALL be explicit. No arbitrary cross-platform get_user or member-history guarantee SHALL be inferred from this API.

#### Scenario: Actor needs a member display name
- **WHEN** get_member succeeds on either platform
- **THEN** the actor consumes the same display_name field without native field precedence logic

#### Scenario: Member role is missing
- **WHEN** the native result does not establish a member role
- **THEN** the result marks role unknown rather than treating the user as an ordinary member

#### Scenario: Target and user belong to different installations
- **WHEN** a member request mixes installation-scoped references
- **THEN** validation rejects it before provider I/O

### Requirement: Reply resolution is evidence-bound and explicitly budgeted
ResolveReplyRequest SHALL contain a genuine observed ReplyRef identifying source and target scope and an explicit LocalOnly or AllowProviderLookup policy. The runtime SHALL verify that relationship against admitted event or approved local source evidence and use existing local author/receipt evidence before a permitted bounded native lookup. QQ get_msg results MUST agree with the requested message and conversation and contain a usable author. Telegram without event/cache evidence SHALL return explicit unavailability rather than pretending to support arbitrary history. The operation MUST NOT recurse or allow arbitrary user text to select native message IDs for lookup. An independent relationship cache and mandatory reply_evidence_max_entries or reply_evidence_ttl_ms configuration are not approved requirements. Supplying these options MUST NOT become an additional startup prerequisite, and migration MUST NOT introduce replacement defaults or silently approve a new retention policy. The existing reference/value bounds, installation/account-generation isolation and invalidation/closure protections SHALL be preserved. Structural restoration of a reference alone SHALL NOT admit a relationship or authorize provider lookup. Correcting the unapproved configuration requirement MUST NOT remove approved reply functionality; the evidence-retention mechanism remains an implementation design issue rather than an operator configuration obligation.

#### Scenario: Local receipt proves the author
- **WHEN** an observed reply points to an exact confirmed send in the authorized scope
- **THEN** resolution returns the known author without provider I/O

#### Scenario: QQ requires a native lookup
- **WHEN** local evidence is insufficient, the source reply is verified and AllowProviderLookup is explicitly selected
- **THEN** a bounded deduplicated get_msg lookup can resolve the author only after full result scope validation

#### Scenario: Historical caller selects LocalOnly
- **WHEN** a history projection contains many unresolved references under LocalOnly
- **THEN** missing references remain unavailable without a provider query per message

#### Scenario: Text fabricates a target
- **WHEN** a request supplies a target without a verified source reply relationship
- **THEN** the runtime rejects the lookup and issues no get_msg call

#### Scenario: Installation omits unapproved relationship-cache options
- **WHEN** an otherwise valid installation omits reply_evidence_max_entries, reply_evidence_ttl_ms and their containing configuration table
- **THEN** their absence does not fail configuration validation or cause a replacement default to be supplied
- **AND** reply resolution still requires genuine admitted evidence and the explicit lookup policy

#### Scenario: Previously admitted relationship is invalidated
- **WHEN** a caller presents its restored ReplyRef after account-generation invalidation or closure has removed admission evidence
- **THEN** that reference cannot authorize provider lookup and no arbitrary get_msg is issued

### Requirement: Media reading returns bounded bytes rather than provider locators
ReadMediaRequest SHALL contain a scoped MediaRef and an explicit positive maximum_bytes. The owning installation SHALL resolve and read the media under request and installed limits and return owned bytes with available typed kind, encoding, filename, size and stable MediaIdentity metadata. Unknown metadata SHALL remain unknown. It MUST NOT expose provider clients, remote host paths, bot tokens or tokenized download URLs. Oversized, stale, unsupported and inaccessible resources SHALL return distinguishable typed errors without unbounded allocation. Network destination policy belongs to the caller and deployment environment: the SDK MUST NOT impose hostname, IP or CDN allowlists or introduce received-media download authorization configuration. This applies to authentic received-media HTTP(S) sources as well as caller-selected HTTP sources, while preserving existing protocol validation, credential isolation, redirect behavior, byte limits, timeout/cancellation and original installation/source-generation validation.

#### Scenario: Actor reads a received image
- **WHEN** the authorized MediaRef is readable and within bounds
- **THEN** the same API returns owned bytes on QQ and Telegram without actor-specific file-resolution calls

#### Scenario: Declared or streamed content is too large
- **WHEN** media size exceeds the requested or installed byte bound
- **THEN** reading stops within the bound and returns a size error instead of the oversized payload

#### Scenario: Media lookup size disagrees with the completed download
- **WHEN** a supported media lookup explicitly declares a size but the completed HTTP body has a different size
- **THEN** media.read returns MalformedResponse instead of replacing the declaration and accepting potentially incomplete bytes
- **AND** absence of a size declaration remains distinct from an explicit zero; successful reads report the actual owned-byte size without inventing MIME, filename or stable identity metadata

#### Scenario: A native filename field contains a path or signed locator
- **WHEN** QQ received-media metadata supplies a filename containing path separators, a colon, query or fragment syntax, or a dot-directory name
- **THEN** the portable filename projection does not expose that value or derive a guessed basename from it
- **AND** omission of unsafe filename metadata does not alter caller-owned final-path/mapping policy or manufacture a locator

#### Scenario: Native image lookup is followed by account retirement
- **WHEN** a QQ media.read request resolves image metadata and the original installation/account origin retires before or during its HTTP acquisition
- **THEN** the native query and download retain the initially captured authority and one deadline, reject retired work, and do not publish bytes under a newly captured origin
- **AND** cancellation awaits owned native and HTTP operation cleanup; query retries and provider-host file reads are not introduced

#### Scenario: Native MIME metadata is path-shaped
- **WHEN** Telegram received MIME metadata lacks a nonempty type/subtype, contains multiple slash separators, or starts either token with a non-alphanumeric character
- **THEN** the portable projection leaves MIME unknown instead of exposing or repairing that value
- **AND** existing supported type/subtype token characters and explicit sticker-format metadata remain unchanged

#### Scenario: OneBot image lookup returns only a provider-host path
- **WHEN** no authorized source capability can read the resolved image and the lookup returns only a remote path
- **THEN** media.read returns typed unavailability without treating that path as local or falling back to an unvalidated URL from the original event

### Requirement: Capabilities describe installed support without promising provider state
GetCapabilitiesRequest SHALL return a typed local description for the exact conversation, including supported common operations, content kinds and styles, media sources, encoding combinations and known byte/dimension/grouping limits. It SHALL be derived from the same installed descriptors/adapters as execution, require no provider I/O and contain no credentials. Unknown dynamic permission or resource availability MUST remain unknown. Advertised support SHALL never be inferred merely from platform brand or an action registered by another installation.

#### Scenario: Optional uploader is absent
- **WHEN** an installation lacks a required upload component
- **THEN** its capability description does not advertise those upload sources and execution rejects them before sending

#### Scenario: Permission changes after capability inspection
- **WHEN** a provider subsequently denies an otherwise supported operation
- **THEN** execution returns a typed provider/permission failure instead of treating capability discovery as a permanent authorization guarantee
