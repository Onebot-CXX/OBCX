## ADDED Requirements

### Requirement: Self lookup is part of the common typed client
BotClient SHALL expose get_self(GetSelfRequest) for the exact BotInstallationRef and retain the action bot.self.get. Its SelfInfo SHALL expose a scoped UserRef and optional display-name and username values using the same identity/comparison semantics as normalized events. The new DTO SHALL be protected by the new SDK contract gate; old installation/user_id and empty-display-name payloads SHALL be translated only at an explicit migration boundary, not treated as binary-compatible public layouts. The query MUST NOT classify messages, compute is_self, or change actor response policy.

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
The common self query SHALL read the existing process-owned SelfIdentityCache and MUST NOT start, wait for, or request provider refresh, including on cache miss. Empty, invalidated or closed caches SHALL return CacheUnavailable, DefinitelyNotSubmitted and no caller-driven retry instruction. No second actor-owned identity cache or query-triggered refresh schedule SHALL be introduced. A retained QQ login-info extension SHALL remain a projection of this same cache.

#### Scenario: Initial identity has not loaded
- **WHEN** get_self is called before the first successful background refresh
- **THEN** it immediately returns CacheUnavailable without a native getMe/get_login_info call or waiting for the active lookup

#### Scenario: Many actors read during refresh
- **WHEN** several actors concurrently query one installation
- **THEN** they receive a valid existing snapshot or explicit unavailability and create no extra refresh work

#### Scenario: Legacy QQ login query is used
- **WHEN** an explicitly retained OneBot login-info extension is invoked
- **THEN** it projects the same cache and cannot bypass the background refresh policy

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
ResolveReplyRequest SHALL contain a genuine observed ReplyRef identifying source and target scope and an explicit LocalOnly or AllowProviderLookup policy. The runtime SHALL verify that relationship against admitted event or approved local source evidence and use existing local author/receipt evidence before a permitted bounded native lookup. QQ get_msg results MUST agree with the requested message and conversation and contain a usable author. Telegram without event/cache evidence SHALL return explicit unavailability rather than pretending to support arbitrary history. The operation MUST NOT recurse or allow arbitrary user text to select native message IDs for lookup. The process-owned relationship admission store SHALL require explicit positive per-installation reply_evidence_max_entries and reply_evidence_ttl_ms in common domain configuration, without defaults. These values SHALL participate in the installation plan fingerprint. Entry sizes, capacity, expiry, installation/account isolation and closure SHALL remain bounded; legacy restoration alone SHALL NOT admit a relationship or refresh this store.

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

#### Scenario: Relationship store bounds are missing
- **WHEN** an installation omits either required relationship-store bound or supplies a nonpositive or unrepresentable value
- **THEN** configuration validation fails without substituting a default or starting provider work

#### Scenario: Previously admitted relationship expires or is evicted
- **WHEN** a caller presents its restored ReplyRef after admission evidence is no longer available
- **THEN** that reference cannot authorize provider lookup and no arbitrary get_msg is issued

### Requirement: Media reading returns bounded bytes rather than provider locators
ReadMediaRequest SHALL contain a scoped MediaRef and an explicit positive maximum_bytes. The owning installation SHALL resolve and read the media under request and installed limits and return owned bytes with available typed kind, encoding, filename, size and stable MediaIdentity metadata. Unknown metadata SHALL remain unknown. It MUST NOT expose provider clients, remote host paths, bot tokens or tokenized download URLs. Oversized, stale, unsupported and inaccessible resources SHALL return distinguishable typed errors without unbounded allocation.

#### Scenario: Actor reads a received image
- **WHEN** the authorized MediaRef is readable and within bounds
- **THEN** the same API returns owned bytes on QQ and Telegram without actor-specific file-resolution calls

#### Scenario: Declared or streamed content is too large
- **WHEN** media size exceeds the requested or installed byte bound
- **THEN** reading stops within the bound and returns a size error instead of the oversized payload

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
