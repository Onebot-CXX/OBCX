## ADDED Requirements

### Requirement: Complete standard surface is a typed SDK capability

The complete SDK, including its matching core, SHALL provide the full OneBot11 standard through named typed QQ requests, results, content and events. This explicitly includes management, approvals, credentials and service operations, not only features currently used by migrated actors. The normative inventory SHALL be pinned to official botuniverse/onebot-11 commit `d4456ee706f9ada9c2dfde56a2bcfc69752600e4`, with exact source references and a field/variant coverage matrix. Source serialization, registered execution, installed public interfaces and provider/runtime verification SHALL be recorded separately.

The public API inventory SHALL include:

- `send_private_msg`, `send_group_msg`, `send_msg`, `delete_msg`, `get_msg`, `get_forward_msg`, `send_like`.
- `set_group_kick`, `set_group_ban`, `set_group_anonymous_ban`, `set_group_whole_ban`, `set_group_admin`, `set_group_anonymous`, `set_group_card`, `set_group_name`, `set_group_leave`, `set_group_special_title`.
- `set_friend_add_request`, `set_group_add_request`.
- `get_login_info`, `get_stranger_info`, `get_friend_list`, `get_group_info`, `get_group_list`, `get_group_member_info`, `get_group_member_list`, `get_group_honor_info`.
- `get_cookies`, `get_csrf_token`, `get_credentials`, `get_record`, `get_image`, `can_send_image`, `can_send_record`, `get_status`, `get_version_info`, `set_restart`, `clean_cache`.

The hidden `.handle_quick_operation` and standard `_async` / `_rate_limited` forms SHALL also be covered. No arbitrary action-name/parameter JSON escape hatch SHALL substitute for named typed support. Core/platform targets SHALL continue to include matching core automatically; no independent platform product or plugin is introduced.

#### Scenario: A standard operation is unused by existing actors

- **WHEN** a normative management, credential or service API has no current actor consumer
- **THEN** it remains required in the complete standard SDK, while actor policy and deployment remain unchanged
- **AND** an existing native serializer alone is not reported as complete public/runtime support

### Requirement: Standard content is complete and faithfully ordered

The SDK SHALL cover the standard segment types `text`, `face`, `image`, `record`, `video`, `at`, `rps`, `dice`, `shake`, `poke`, `anonymous`, `share`, `contact`, `location`, `music`, `reply`, `forward`, `node`, `xml`, and `json`, including their normative variants and send/receive restrictions. The inventory's variant counting SHALL be verified against the pinned source rather than used to omit a named type. Standard sources, options, referenced/authored nodes and single-object/array/string content forms SHALL be handled without fabricated identities, reordering, dropped captions, implicit Audio-to-Voice conversion or invented codec/exclusivity guarantees.

Standard XML/JSON message content SHALL have a dedicated bounded content representation; it SHALL NOT authorize arbitrary operation payloads. Ordinary shared message types SHALL remain platform-neutral. Native serialization, escaped/recursive expansion and output accounting SHALL remain SDK/core-owned.

#### Scenario: A standard forward lookup returns node segments

- **WHEN** `get_forward_msg` returns singular `data.message` with `type=node` and `data.user_id/nickname/content`
- **THEN** the SDK parses genuine node identity and ordered content, including string, array and single-segment content
- **AND** it does not fabricate a standalone node MessageRef, timestamp or reply relationship

### Requirement: Delegated work does not masquerade as bounded local acquisition

Explicit typed QQ provider-delegated URL/reference intents SHALL be distinct from common bounded core `HttpSource` acquisition. Provider `cache`, `proxy` and `timeout` controls SHALL preserve their actual meaning. SDK-owned input, preparation, expansion, envelopes, physical requests and output accounting SHALL remain bounded. Unknown provider-side acquisition/content expansion SHALL remain explicitly unverified, not zero or measured. Configuration defaults SHALL NOT be introduced.

#### Scenario: A caller requires a remote-work guarantee

- **WHEN** a selected delegated source cannot supply genuine evidence sufficient for the caller's requested end-to-end bound
- **THEN** the SDK rejects before submission rather than silently weakening that guarantee
- **AND** it does not perform a hidden lookup, fallback or provider-path mapping

### Requirement: Explicit standard queries preserve identity and evidence boundaries

A distinctly named standard login query MAY perform explicitly requested provider I/O through the same identity/refresh coordinator and publish valid synchronous observations through the same SelfIdentityCache. Existing common self and QQ compatibility queries SHALL remain cache-only; the approved300000ms schedule, coalescing and origin/account-transition protections SHALL remain. The standard result SHALL retain the exact observed values, including an empty nickname, rather than masquerading a subsequent cache reread as the original response. Queued acknowledgments SHALL NOT publish an identity observation.

Standard message/forward resource queries MAY use additive QQ-only installation-scoped opaque selectors distinct from MessageRef/QqForwardRef. Bounded selector import/export SHALL NOT prove existence, conversation membership, reply relationship or authority. Explicit query results SHALL be detached snapshots, preserving actual observed identity/content/time without invented conversation proof, event/reply admission or automatic lookup chains. Existing scoped forward and common ResolveReply policies SHALL remain unchanged.

#### Scenario: A standard lookup response lacks conversation evidence

- **WHEN** the provider returns a matching requested resource ID but no conversation identity
- **THEN** the result retains the genuine observation without claiming a ConversationRef or publishing MessageReceived
- **AND** legacy restoration and common history/reply paths do not automatically invoke this new query

### Requirement: Standard events and media observations retain genuine evidence

The typed SDK SHALL cover private/group messages; group upload/admin/decrease/increase/ban, friend add, group/friend recall and notify poke/lucky-king/honor notices; friend/group requests; lifecycle and heartbeat events, including all pinned standard subtypes and fields. It SHALL preserve origin, real timestamps, source identities, unknown values and bounded archival evidence. Public carrier/ABI/schema compatibility SHALL be reviewed explicitly; existing actors SHALL NOT receive an unsafe silently changed binary layout.

Standard received image/record/video URLs SHALL be handled as genuine bounded private evidence, not unchecked fallback or signed URLs exposed in ordinary public metadata/logs. `get_record` conversion SHALL be explicit and SHALL NOT be presented as original-byte preservation. Provider-local `get_image` / `get_record` paths SHALL NOT be assumed readable by OBCX. Group-upload file evidence SHALL NOT imply support for nonstandard file-message/file-URL APIs. Callers retain final-path mapping, authorization, leases and cleanup.

#### Scenario: A media query returns only a provider-local path

- **WHEN** no authorized acquisition route is available for that provider-local resource
- **THEN** the SDK reports the precise unavailable/inaccessible acquisition state without guessing a local mount or manufacturing bytes
- **AND** the typed standard query result is not confused with a successful bounded MediaBlob read

### Requirement: Standard acceptance and side-effect completion are distinct

Synchronous, `_async` and `_rate_limited` forms SHALL retain their actual completion semantics. An async/rate-limited acknowledgment SHALL mean accepted, not completed or delivered. Message-producing and non-message mutations SHALL preserve confirmed effects and uncertain submission independently of application-level success. No fabricated message ID, final result, whole-request retry or automatic compensation SHALL follow possible submission. Hidden quick operations SHALL use genuine context and typed variants, not arbitrary replayed event JSON.

#### Scenario: An asynchronous mutation is accepted

- **WHEN** the provider acknowledges queued work without a final result
- **THEN** the typed result expresses acceptance and unresolved completion without inventing a successful action or SendReceipt
- **AND** callers are not granted retry permission merely because no final output ID was returned

### Requirement: Sensitive and operational APIs do not authorize live execution

Credential-bearing requests/results SHALL have explicit sensitive-value and redaction handling across serialization, tracing, errors and diagnostics. Implementation SHALL NOT require querying real credentials or executing administrative/service actions for acceptance. Request parameters and protocol omission choices SHALL be explicit rather than new OBCX configuration defaults.

All new implementation SHALL be isolated and validated against a new matching complete core/SDK generation; frozen generations SHALL remain unchanged. No new/rewritten tests, live configuration/database/prompt change, deployment, restart or management execution is authorized by this requirement. Nonstandard extension additions/changes SHALL remain deferred until standard implementation is complete and the user decides their handling; existing extension behavior SHALL be preserved.

#### Scenario: A service API is added to the SDK

- **WHEN** typed restart or cache-clean support is implemented and compiled
- **THEN** no real provider restart or cleanup is executed
- **AND** source, compilation, full linking, installed SDK and runtime/provider gates remain separately reported
