## ADDED Requirements

### Requirement: Portable outgoing content has explicit typed semantics
The public SDK SHALL define ordered message blocks for rich text, static images, animations, video, audio or voice, and files. Rich text SHALL distinguish literal text, explicitly modeled text styles, and a mention of an exact UserRef. Animations MUST NOT be silently converted to static images, and unsupported styles MUST NOT be silently stripped. Captions SHALL be explicit rich-text fields of their attachment. The ordinary sending API MUST NOT accept a provider segment type with arbitrary JSON, CQ strings as structured instructions, Telegram entity offsets, or an arbitrary provider method. Platform adapters SHALL own native serialization and validate the entire requested combination.

#### Scenario: Actor sends identical text and mention types on either platform
- **WHEN** an actor sends literal text and a valid scoped Mention through the common client
- **THEN** the selected adapter emits the platform's native representation without an actor platform branch
- **AND** literal user text is not reinterpreted as CQ or entity instructions

#### Scenario: Message contains text before and after an image
- **WHEN** the actor supplies ordered text, image, and text blocks
- **THEN** the adapter preserves that order or rejects the requested delivery policy
- **AND** it does not silently move either text block into a caption

#### Scenario: Provider-specific segment is submitted as portable content
- **WHEN** a request supplies unknown JSON fields or a platform-only segment instead of a declared content type
- **THEN** typed decoding rejects it before sending and no passthrough path is selected

#### Scenario: Actor supplies an animation or styled sender prefix
- **WHEN** the requested content includes Animation or an explicitly italic Text span
- **THEN** the adapter preserves the modeled semantics or rejects unsupported content before sending
- **AND** it does not discard animation frames, reinterpret markup strings, or require actor-native entity offsets

#### Scenario: Actor explicitly prepares a display substitute
- **WHEN** an actor's own policy converts a source attachment or constructs a placeholder and supplies that resulting typed body
- **THEN** the SDK transmits the supplied body subject to validation rather than performing an additional hidden lossy fallback

### Requirement: Delivery multiplicity is explicit and lossless
Every SendMessageRequest SHALL specify SingleMessageOnly or AllowSplit and explicit maximum output-message, per-media-byte and total-media-byte bounds. SingleMessageOnly SHALL allow exactly one resulting native message, not merely one provider request. AllowSplit SHALL permit multiple native outputs within the bounds while preserving all requested content, ordering and exact target conversation. An explicit reply target SHALL be attached only to the first output and MUST match the target conversation scope. The adapter MUST NOT drop content or replace semantic mentions, replies, or attachments with plain-text substitutes.

#### Scenario: Two images require more than one native message
- **WHEN** a two-image request uses SingleMessageOnly and the provider cannot represent it as one message
- **THEN** the request is rejected before a message-producing provider call
- **AND** the adapter does not send only the first image

#### Scenario: Actor permits image splitting
- **WHEN** the same request uses AllowSplit with sufficient explicit limits
- **THEN** both images are sent in order and all validated output references are returned

#### Scenario: Native batch produces several messages
- **WHEN** a Telegram media-group call would return several message IDs
- **THEN** that plan counts as multiple messages even though it uses one HTTP request

#### Scenario: Reply is split inside a topic conversation
- **WHEN** an allowed split targets an opaque topic conversation and quotes a message in that conversation
- **THEN** every output remains in the encoded conversation and only the first quotes the supplied message

#### Scenario: A later block is unsupported
- **WHEN** a request's later block or caption combination is known to be unsupported during planning
- **THEN** the whole request is rejected before the first message-producing call rather than sending a valid prefix

#### Scenario: Telegram image sequence exceeds one media group
- **WHEN** a compatible eleven-image sequence is explicitly allowed to split with sufficient bounds
- **THEN** the adapter plans legal native outputs for all eleven images, such as groups of nine and two
- **AND** it does not submit an illegal one-item media group, drop the trailing image, or reorder intervening non-image content

### Requirement: Send outcomes retain confirmed and uncertain effects
A complete SendReceipt SHALL contain every validated output MessageRef in logical order. A SendFailure SHALL contain a redacted typed error, all validated confirmed output references, and an explicit indication of any possible unconfirmed output. Both SHALL include corresponding typed output_details carrying verified input-block or fragment associations and any reusable media references actually established by the provider. Absent reusable media SHALL remain absent. Provenance SHALL come from the execution plan and validated results, not an actor assumption that each block produces exactly one output. If a partial or malformed result establishes an output reference but not its precise input association, that association SHALL be explicitly unavailable without discarding the confirmed reference or guessing provenance. Gateway encoding SHALL preserve these details and MUST NOT expose native file IDs or response JSON for callers to reconstruct them. The executor SHALL stop scheduling later steps at the first failure and MUST NOT automatically retry the whole logical send or delete previously confirmed output. Once any output is confirmed or possibly submitted, aggregate submission safety SHALL be PossiblySubmitted and whole-request automatic retry SHALL be disabled.

#### Scenario: Third output is explicitly rejected
- **WHEN** two outputs are confirmed and the third is definitively rejected
- **THEN** the failure retains the first two references, reports no additional uncertain output for that rejection, and does not send later steps or advertise safe whole-request retry

#### Scenario: Response is lost after an earlier confirmation
- **WHEN** a later message-producing request times out without proof of non-submission
- **THEN** the failure retains earlier confirmations and indicates possible unconfirmed output without inventing an ID

#### Scenario: Native batch response is only partly verifiable
- **WHEN** a batch response contains some exact valid receipts but does not prove the complete result
- **THEN** only verified references are retained and the remaining possible effect is reported as uncertain

#### Scenario: Cancellation occurs between steps
- **WHEN** cancellation arrives after one confirmed output and before the next step begins
- **THEN** the result retains that output and schedules no further sends or automatic compensation

#### Scenario: A split produces several outputs for one source
- **WHEN** one input body produces multiple confirmed native messages
- **THEN** the result includes each reference and its input provenance so a consumer can persist all outputs rather than keeping only the first

#### Scenario: Provider returns reusable media with a send result
- **WHEN** a confirmed output contains a validated reusable native media locator
- **THEN** its details expose an opaque MediaRef owned by the sending installation and available stable identity metadata
- **AND** missing or invalid locator fields do not produce fabricated reusable references

#### Scenario: Partial result crosses the internal gateway
- **WHEN** a send fails after confirming a media-bearing output
- **THEN** its verified reference, input association and available reusable media survive encoding without converting the failure into complete success

#### Scenario: Batch output is confirmed without a safe input association
- **WHEN** a malformed native batch response independently confirms a scoped output but cannot prove its input-block association
- **THEN** SendFailure retains that output with explicitly unavailable provenance rather than discarding its side-effect evidence or fabricating a mapping

### Requirement: One message reference supports private and group deletion
DeleteMessageRequest SHALL accept the common MessageRef for private, group, and encoded topic conversations. The adapter SHALL derive the required provider chat/message fields internally, validate installation and scope, and return a typed receipt for that exact reference. Unsupported permissions or provider deletion restrictions SHALL produce explicit errors rather than changing reference types or returning fabricated success.

#### Scenario: Actor deletes its private-send result
- **WHEN** an actor passes a confirmed private MessageRef to delete_message
- **THEN** no conversion to a group reference or platform-specific delete request is required

#### Scenario: Same Telegram message ID exists in two chats
- **WHEN** deletion names one of the fully scoped references
- **THEN** only its decoded chat and message ID are used for the provider call

### Requirement: Media sources are bounded owned values
MediaSource SHALL distinguish opaque installation-owned MediaRef, owned BytesSource, caller-supplied final-path LocalFile, and caller-selected HttpSource. Media payloads crossing the internal gateway SHALL use owned binary buffers rather than per-byte JSON number arrays. Callers SHALL own file placement, accessibility, path mapping, mounts and business authorization. The SDK/core MUST NOT map paths, infer their filesystem, impose OBCX-host-only semantics, require file preregistration or introduce a uniform path-authorization configuration. Adapters SHALL own native path/URI serialization or supported upload mechanics without changing the caller's destination path. Request and installed limits SHALL bound known sizes and all actual SDK reads, allocations and preparation memory; unknown provider-only file sizes MUST NOT be treated as zero or claimed as verified. Unsupported native source combinations SHALL fail explicitly, not request a mapping-policy installer. HTTP handling SHALL preserve protocol validation, credential isolation and existing transport redirect restrictions without introducing a new deployment-authorization gate.

#### Scenario: Actor supplies image bytes
- **WHEN** an authorized image within the explicit byte limits is submitted as BytesSource
- **THEN** the platform adapter selects the required native upload without actor multipart/base64 adaptation and without JSON byte-array expansion

#### Scenario: Caller supplies a provider-container path
- **WHEN** the caller maps a shared file to its final provider-visible path and submits LocalFile to a native path-capable adapter
- **THEN** the adapter serializes that path without SDK mapping, preregistration or an OBCX-host existence check
- **AND** deployment and file lifetime remain caller responsibilities

#### Scenario: SDK reads media for upload
- **WHEN** a supported upload path requires the SDK to read bytes
- **THEN** the actual read and preparation are bounded by the explicit limits and logical deadline
- **AND** the SDK reports an inaccessible file honestly rather than mapping it or claiming provider-only size verification

#### Scenario: HTTP source uses existing transport restrictions
- **WHEN** a caller-selected HTTP source is processed by the SDK
- **THEN** protocol and existing redirect restrictions remain enforced without forwarding bot credentials to the source

#### Scenario: Total preparation exceeds a bound
- **WHEN** individually acceptable attachments collectively exceed maximum_media_total_bytes
- **THEN** preparation stops before sending if known sizes or actual SDK reads establish the excess
- **AND** a provider-only path does not create a claim that the SDK measured the remote file

### Requirement: Media transfer across installations is explicit
Direct native MediaRef reuse SHALL require the correct owning installation and supported media capability. Cross-installation copying SHALL use an explicitly authorized read_media followed by a send of owned bytes to the destination. The SDK MUST NOT silently download from another installation merely because its MediaRef appears in a destination request. Authenticated media resolution SHALL remain process-side and MUST NOT return bot credentials or tokenized URLs.

#### Scenario: Telegram reference is supplied to a QQ send
- **WHEN** a QQ send contains a native MediaRef owned by a Telegram installation
- **THEN** direct reuse is rejected without implicit source-installation I/O

#### Scenario: Actor explicitly copies media
- **WHEN** an authorized actor reads the source within maximum_bytes and sends the returned BytesSource to another authorized installation
- **THEN** each operation validates its own route and limits, without actor platform API selection
