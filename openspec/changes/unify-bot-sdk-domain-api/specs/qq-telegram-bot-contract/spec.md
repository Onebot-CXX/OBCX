## MODIFIED Requirements

### Requirement: Bot references are scoped to an implemented installation
The bot-operation contract SHALL identify a configured bot by its exact BotInstallationRef and registered surface, with current production support for telegram.bot_api and onebot11.qq and no alias to qq.official. The common ConversationRef SHALL contain only the installation and an opaque ConversationId encoding the complete destination, including conversation kind and any required sub-conversation identity. It MUST NOT expose thread, topic_id, native_group_id or a caller-assembled platform route. UserRef SHALL contain the installation and a typed UserId; MessageRef SHALL contain ConversationRef and a separate MessageId. The owning codec SHALL provide canonical versioned unambiguous encoding, bound lengths, validate native scope and distinguish group/private/topic identities without business parsing.

#### Scenario: Equal ids belong to different installations
- **WHEN** two configured bots report the same native conversation, user or message ID
- **THEN** their scoped references compare as different resources

#### Scenario: Group and private IDs have equal text
- **WHEN** one installation has a group and private user with the same native ID text
- **THEN** their ConversationIds remain distinct and cannot be substituted for each other

#### Scenario: Telegram delete is represented
- **WHEN** Telegram message 42 in a particular chat or topic is targeted for deletion
- **THEN** the request contains installation, the opaque complete ConversationId and a separate MessageId
- **AND** the adapter derives native chat/message fields without caller topic arguments or string splitting

#### Scenario: Current QQ adapter is referenced
- **WHEN** a request targets the implemented QQ-compatible bot
- **THEN** its exact surface is onebot11.qq and no field implies official QQ support

#### Scenario: Two topics share one native chat
- **WHEN** two events originate from different topics in the same Telegram chat
- **THEN** their ConversationIds differ and ordinary send can preserve each destination without exposing a thread field

#### Scenario: Address components include delimiter-like data
- **WHEN** native components contain characters used by the internal codec syntax
- **THEN** validated encoding remains unambiguous and cannot collide with another component tuple

### Requirement: The first operation set is closed to current QQ and Telegram calls
The production public domain contract SHALL admit only the registered common operations message.send, message.delete, bot.self.get, member.get, message.reply.resolve, media.read and bot.capabilities.get, together with explicitly reviewed provider extensions. Their implementations SHALL remain endpoint-local and capability-dependent. Legacy common message.send_group/message.send_private and the need for telegram.message.send_topic in ordinary sending SHALL be replaced by the unified typed request and complete ConversationRef. Existing approved QQ/TG extension capabilities SHALL remain separately namespaced and scoped; their presence MUST NOT create arbitrary native dispatch or claim common support. Portable history, arbitrary-user lookup, contact lists, moderation, reactions, polls, membership lists, request handling, credentials and unsupported platforms are outside this common operation set.

#### Scenario: Current actor operation is represented
- **WHEN** an actor needs an approved common operation or a retained platform extension
- **THEN** it constructs the corresponding typed request without a live bot object or arbitrary provider method

#### Scenario: Private text reply is requested
- **WHEN** an authorized command service replies to an exact private conversation
- **THEN** it uses the same SendMessageRequest and MessageRef result types as a group reply

#### Scenario: Unused legacy method is requested
- **WHEN** a caller submits an unregistered action or a native method outside the reviewed descriptors
- **THEN** the dispatcher rejects it before provider I/O instead of falling back to raw dispatch

#### Scenario: Another platform is considered
- **WHEN** code targets an unregistered surface such as official QQ or another unsupported platform
- **THEN** runtime validation rejects it and does not map it to Telegram or OneBot

#### Scenario: Self query is requested
- **WHEN** get_self is called for either implemented platform
- **THEN** bot.self.get returns the shared cache-only SelfInfo contract or explicit unavailability

### Requirement: Requests are data-only and preserve current payload semantics
Public domain requests SHALL contain strongly typed owned value data and MUST NOT contain live bot/provider interfaces, connection managers, executors, credentials or streams. A LocalFile path SHALL be an owned final path string supplied by the caller, not a filesystem authority or a request for SDK mapping. Ordinary sends SHALL use the typed MessageBody and common ConversationRef, not common::Message segment JSON. Existing text, exact reply and media meaning SHALL be preserved by platform conversion, explicit delivery policy or explicit rejection; unsupported native-only content MUST NOT silently degrade. LocalFile and HttpSource SHALL retain typed validation and bounds on actual SDK work; caller business authorization and deployment mapping SHALL remain outside SDK/core. Platform-only lookup, editing, forward and management operations SHALL remain explicitly provider-namespaced types rather than polluting the common model.

#### Scenario: Actor sends existing message segments
- **WHEN** an actor is migrated from existing text, reply and image segments
- **THEN** it constructs the equivalent common typed content and the adapter owns native conversion
- **AND** unknown legacy segments are explicitly rejected or handled through a reviewed extension instead of being accepted as generic JSON

#### Scenario: Private target is incomplete
- **WHEN** a private destination lacks an installation or a valid complete ConversationId
- **THEN** value validation rejects it before provider I/O

#### Scenario: Telegram file is fetched
- **WHEN** an actor requests media.read for a Telegram MediaRef with an explicit byte limit
- **THEN** it receives bounded owned bytes without a bot token, tokenized URL, HTTP client or provider-local path

#### Scenario: Telegram upload is submitted
- **WHEN** an actor submits supported typed media bytes and explicit caption/reply/delivery limits
- **THEN** the Telegram adapter owns multipart construction and topic extraction from the target reference

### Requirement: Successful results contain the values current actors consume
A successful common send SHALL return every validated output as a MessageRef using one exact ConversationRef contract for private, group and topic destinations, with typed input provenance and available reusable media in output_details. Callers MUST NOT extract message or media IDs from provider JSON. Delete SHALL return typed confirmation of the supplied MessageRef; platform edit results SHALL remain typed. Common member queries SHALL expose normalized available profile/role data, and platform extensions SHALL retain explicitly provider-specific fields required by their consumers. A provider forward result SHALL remain identified as provider-specific, with validated typed nodes, authors, ordered content and scoped media references rather than arbitrary native-node JSON. Partial or uncertain output SHALL use SendFailure with retained verified receipts, not a complete success or an evidence-free ordinary error.

#### Scenario: Telegram group send succeeds
- **WHEN** Telegram returns a valid group message result
- **THEN** the result contains its exact installation, opaque conversation and MessageId

#### Scenario: Telegram private send succeeds
- **WHEN** Telegram returns a valid private message result
- **THEN** the same MessageRef type identifies that exact private conversation

#### Scenario: OneBot group send succeeds
- **WHEN** OneBot returns successful data with a valid message ID
- **THEN** the result contains a common MessageRef for the requested exact group conversation

#### Scenario: OneBot private send succeeds
- **WHEN** OneBot returns successful private-send data with a message ID
- **THEN** the result contains a common MessageRef that can be passed directly to delete_message

#### Scenario: OneBot member lookup succeeds
- **WHEN** a common member query obtains native card/nickname and other profile evidence
- **THEN** the adapter returns normalized available common fields without requiring actor native-field selection
- **AND** native-only fields remain available through explicitly retained extensions rather than a raw common response

#### Scenario: Only a send prefix succeeds
- **WHEN** a logical send confirms some outputs and later fails
- **THEN** its SendFailure retains those exact references and indicates any additional unknown submission

### Requirement: Errors distinguish safe retry from possible submission
BotOperationError SHALL retain a stable error code, redacted message, optional provider code/retry delay, a retryable flag and DefinitelyNotSubmitted or PossiblySubmitted safety. Route, pure validation and unsupported failures before message-producing calls MUST be definitely not submitted. A malformed success, timeout, disconnect or exception after a side effect begins SHALL be possibly submitted unless absence of provider effect is proven. SendFailure SHALL additionally retain confirmed MessageRefs and an explicit unknown-output indicator. If any output is confirmed or possible, aggregate safety SHALL be PossiblySubmitted and whole-request automatic retry SHALL be disabled, even if the final failed step was definitely rejected.

#### Scenario: Exact route does not exist
- **WHEN** a request names an unknown installation
- **THEN** it returns a definitely-not-submitted route error and performs no provider call

#### Scenario: Transport fails before request writing
- **WHEN** DNS, TCP, proxy or TLS fails before the first message-producing request is written
- **THEN** the result may classify retry as safe according to the transport evidence
- **AND** an earlier confirmed output in the same logical request would instead prohibit whole-request automatic retry

#### Scenario: Provider explicitly rejects a send
- **WHEN** a valid provider error proves a particular step was rejected
- **THEN** that step's evidence is preserved while aggregate retry classification also accounts for prior confirmed or uncertain output

#### Scenario: Nominal success lacks message identity
- **WHEN** a side-effecting send returns nominal success without required verifiable identity
- **THEN** the outcome is possibly submitted, retains any other verified receipts and fabricates no message ID

### Requirement: The installed SDK includes its matching core
The installed SDK SHALL include its matching core library. Public and platform SDK targets SHALL transitively link that imported core, without requiring consumers to locate or add a separate core library. Interface selection SHALL NOT imply a standalone core-free SDK product. Platform implementations SHALL remain in core; this change SHALL NOT introduce separate platform implementation libraries or runtime platform plugins. Public headers SHALL retain their typed API boundaries and SHALL NOT expose process-private codec, registry or transport headers. Isolated actor builds SHALL consume the frozen installed SDK and SHALL NOT rebuild or replace a live core.

#### Scenario: Consumer selects a platform SDK target
- **WHEN** a consumer links the installed Telegram or QQ SDK target
- **THEN** its link closure includes the matching imported core through the SDK target
- **AND** public extension implementations may be supplied by that core without separate manual core linkage or platform implementation libraries

### Requirement: Public values serialize deterministically
Every public reference, request, outcome, event and error SHALL provide SDK-owned deterministic versioned serialization and validation suitable for independently built actor packages. Ordinary callers MUST NOT assemble or interpret provider payloads or opaque ID components. Common value deserialization SHALL reject missing scope, malformed or noncanonical encoding, inconsistent action/surface binding and exceeded media bounds without requiring provider SDK linkage or actor-side codec registration. The process-owned platform codec SHALL additionally reject invalid internal native sub-conversation identity at ingress, configuration, legacy binding and operation admission before provider I/O; structural decoding alone MUST NOT establish native validity or authorization. The new domain DTO schema SHALL be separated from the preceding SDK contract; changed SelfInfo and message/reference layouts MUST NOT be presented as unchanged JSON or binary contracts. Internal media gateway encoding SHALL preserve owned binary buffers without per-byte JSON arrays. A narrow generation-owned ReferenceResolver SHALL expose pure configuration/legacy conversion to actors without exposing process-private codec registrations or requiring provider SDKs. Raw native archives SHALL be accepted only through explicitly named restoration boundaries: the common ReferenceResolver or an owning provider SDK's pure typed specialized-content restoration function. Specialized restoration SHALL reuse bounded source/scope/recorded-time validation, preserve explicit source ordering, perform no I/O or reply admission, and return scoped typed views rather than native JSON. Neither boundary SHALL accept archives as ordinary send/media content. Full legacy MessageReceived restoration SHALL require recorded receive or first local ingestion time and SHALL NOT invent current/zero receive timestamps or confer reply-lookup authority. Archive aggregate bounds and declared field bounds SHALL be checked before extraction/copying. Recorded native kind and narrowly identified archival ID representation SHALL be reconciled with preserved legacy keys without weakening live native validation; configuration-only ambiguity SHALL be rejected rather than routed through a guessed native kind.

#### Scenario: Value crosses an installed SDK boundary
- **WHEN** a valid new-contract request or result is encoded and decoded using matching installed SDKs
- **THEN** complete routing, content, outcome and uncertainty evidence is preserved

#### Scenario: Surface and action disagree
- **WHEN** a OneBot installation is paired with a Telegram-only request
- **THEN** validation fails before provider I/O

#### Scenario: Encoded conversation is invalid
- **WHEN** a restored opaque ID is malformed, noncanonical or encodes an invalid native topic
- **THEN** the owning codec rejects it rather than routing to a guessed parent conversation

#### Scenario: Actor restores configured or historical source references
- **WHEN** a consumer invokes the explicit ReferenceResolver with complete recorded scope and archive evidence
- **THEN** the injected owning binding performs pure validated conversion without provider I/O, actor codec registration, changed legacy storage keys or new event admission

#### Scenario: Legacy receive time is unavailable
- **WHEN** a historical archive lacks recorded receive or first local ingestion time required for a full MessageReceived
- **THEN** restoration reports unavailability instead of filling the field with current time, native sent time or epoch zero

#### Scenario: Old self DTO is read
- **WHEN** a migration boundary reads the earlier installation/user_id SelfInfo shape
- **THEN** it explicitly translates into the new scoped UserRef shape or rejects it, rather than bypassing the new SDK contract gate

## ADDED Requirements

### Requirement: Final media paths and file lifetime remain caller owned
Callers SHALL supply final path strings after any caller-owned path mapping. SDK/core MUST NOT integrate a path mapper, require shared-file registration or impose OBCX-host-only path semantics. The external obcx.path-mapping library SHALL remain caller-used infrastructure. A path-capable OneBot adapter SHALL own native file-URI serialization for typed ordinary content and constructed forward nodes, without actor-authored CQ JSON. Consumers SHALL retain file ownership, accessibility, mounts, leases, quotas and cleanup obligations through the full submission lifecycle, including possible submission. Structural path validation SHALL NOT be presented as filesystem existence or size verification.

#### Scenario: Actor sends a shared image
- **WHEN** an actor supplies its final provider-visible path in typed image content
- **THEN** the adapter constructs the native file reference without registration or SDK path mapping
- **AND** sending does not release or transfer the actor's file lease

#### Scenario: Provider container path is not visible to OBCX
- **WHEN** a native path-capable adapter receives a provider-only path
- **THEN** it does not reject the path merely because it cannot stat that path on the OBCX host
- **AND** it does not claim to have verified the provider file's existence or size

#### Scenario: Shared media send may have been submitted
- **WHEN** a call using a caller-supplied path finishes with possible submission
- **THEN** the outcome preserves that evidence and cannot authorize treating the file as definitely unused for cleanup or automatic resend

#### Scenario: Caller restores a pending file send after restart
- **WHEN** a consumer restores a pending request and its file-lifetime state
- **THEN** deployment accessibility, lease recovery and retry permission remain caller responsibilities without a new SDK registration step
