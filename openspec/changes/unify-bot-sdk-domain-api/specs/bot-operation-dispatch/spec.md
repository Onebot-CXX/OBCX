## MODIFIED Requirements

### Requirement: Supported actions match the closed implementation matrix
Each operation endpoint SHALL publish only reviewed common domain operations and provider extensions implemented by its actual installed descriptors and adapters. Typed conversation capabilities SHALL be derived from the same source, including supported content kinds, sources, combinations and known bounds. Optional uploader/source support SHALL be advertised only when the same installation has the required capability. Neither raw action presence nor platform brand SHALL imply support for arbitrary message content. Static support reporting MUST NOT infer support from RTTI, inheritance, another installation or live provider I/O.

#### Scenario: Telegram endpoint is inspected
- **WHEN** a caller queries a composed Telegram installation
- **THEN** it sees implemented common operations and separately namespaced Telegram extensions, with truthful media-source capabilities

#### Scenario: OneBot endpoint is inspected
- **WHEN** a caller queries a composed OneBot installation
- **THEN** it sees its implemented common operations and OneBot extensions and never another installation's Telegram capabilities

#### Scenario: Multipart uploader capability is absent
- **WHEN** a Telegram recipe omits a required uploader
- **THEN** related upload source/action capabilities are absent and attempted use is rejected before sending

### Requirement: Actors call one data-only operation client
The runtime SHALL expose the common typed BotClient to actors and reuse the same process-owned routing/lifecycle backend for generation-owned command services. Ordinary actor methods SHALL accept finite domain requests and return typed results, including a complete SendOutcome for sending and the existing cache-only self action. The internal gateway and its codecs SHALL remain implementation/binding infrastructure rather than a required business-facing JSON API. The common SDK MUST NOT include all provider DTOs or expose live bots, registry entries, connection managers, credentials or provider executors. Explicit platform clients SHALL use the same controlled backend without arbitrary native dispatch.

#### Scenario: Actor sends to a configured group
- **WHEN** an actor supplies an exact configured ConversationRef and typed body
- **THEN** the matching adapter performs the operation without actor bot resolution, topic arguments or native payload construction

#### Scenario: Command service replies to a private user
- **WHEN** generation-owned help processing replies to a private ConversationRef
- **THEN** it uses the same common client and typed message reference as group processing

#### Scenario: Request supplies only a platform
- **WHEN** a caller omits installation identity or supplies only qq/telegram/a surface
- **THEN** dispatch returns a definitely-not-submitted validation error and selects no bot

#### Scenario: Target belongs to another installation
- **WHEN** a request combines incompatible installation-scoped conversation, user, reply or native media references
- **THEN** validation rejects it before provider I/O rather than rerouting based on nested payload data

### Requirement: Wrappers parse current provider responses conservatively
Platform adapters SHALL continue parsing Telegram ok/result and OneBot status/retcode/data/echo envelopes process-side. Every claimed common send receipt SHALL be validated against the requested complete destination and native message identity. A native batch SHALL retain each independently verified receipt and SHALL not claim complete success if any required receipt is missing or invalid. Empty, malformed and synthetic results MUST NOT be success. Aggregate side-effect failure SHALL preserve confirmed receipts, typed input/output provenance, available reusable media and uncertainty while retaining redacted error semantics. Media references SHALL be installation-scoped and validated from actual provider evidence; absent fields MUST NOT be fabricated.

#### Scenario: Telegram group, topic, or private send succeeds
- **WHEN** the adapter receives valid results for the requested destination
- **THEN** it returns common MessageRefs with the exact decoded conversation and native IDs

#### Scenario: OneBot group send succeeds
- **WHEN** the OneBot adapter receives successful data with a valid message ID
- **THEN** it returns a common reference scoped to the requested group

#### Scenario: OneBot private send succeeds
- **WHEN** the OneBot adapter receives successful private-send data with a valid message ID
- **THEN** it returns the same common reference type scoped to the private conversation

#### Scenario: Provider response is malformed
- **WHEN** a side-effecting call returns invalid JSON, empty data or incomplete nominal success
- **THEN** the endpoint reports possible submission without fabricated success and retains independently verified earlier receipts

#### Scenario: Provider explicitly rejects the action
- **WHEN** a valid provider error envelope is returned
- **THEN** only redacted code/message/retry metadata and required typed effect evidence reach the caller

### Requirement: Bot-owned media and lookup calls stay behind wrappers
The owning installation SHALL perform authenticated media resolution/download, supported uploads, member normalization, evidence-bound reply lookups and background identity reads. Common actors SHALL receive typed profiles or bounded owned media bytes, not tokenized URLs, provider clients, transports or registry objects. Existing reviewed provider-only forward, file, emoji, poke or editing operations SHALL remain behind explicit extension adapters. Request/installed media bounds on actual SDK work, protocol and credential safety, and exact installation scope SHALL be enforced by the responsible component. File placement, mapping, mounts and business authorization SHALL remain caller-owned; SDK/core MUST NOT add shared-file registration or a uniform path-authorization installer. get_self itself MUST remain a cache read without provider I/O.

#### Scenario: Telegram media is fetched
- **WHEN** an authorized actor submits media.read with a Telegram MediaRef and explicit maximum_bytes
- **THEN** the owning adapter performs resolution/download and returns only bounded bytes and allowed metadata

#### Scenario: Telegram file exceeds its bound
- **WHEN** declared or streamed media exceeds the request/configured bound
- **THEN** reading stops and returns an explicit size failure without materializing an unbounded body

#### Scenario: OneBot provider-specific lookup runs
- **WHEN** a caller requests a reviewed OneBot extension
- **THEN** its adapter validates the native response and returns its namespaced typed result rather than exposing the transport

#### Scenario: Self cache misses
- **WHEN** bot.self.get has no valid cached snapshot
- **THEN** the dispatcher returns CacheUnavailable without initiating or waiting for a native identity request

### Requirement: Existing process lifecycle and compatibility remain authoritative
One shared process-owned routing backend SHALL serve all compatible actor generations through typed clients. Validation-only SHALL perform no network or persistent side effect and reload SHALL not recreate installations. Candidate preparation MUST NOT refresh or mutate the live self cache; approved lifecycle refresh happens only after successful publication. In-flight domain operations SHALL retain required process dependencies, inherit actor cancellation and one total operation budget, and MUST NOT spawn unowned detached work. The final ordinary SDK surface SHALL expose only data-only clients/events rather than legacy live bots or raw provider adapters.

#### Scenario: Actor generation reloads
- **WHEN** a compatible candidate generation is prepared
- **THEN** its typed clients use the existing routing backend and installation capabilities without recreating them

#### Scenario: Validation-only startup runs
- **WHEN** configuration and target references are validated
- **THEN** codec/recipe/capability checks occur without provider calls, downloads, sends or cache refresh

#### Scenario: Process shuts down
- **WHEN** shutdown races with an in-flight domain operation
- **THEN** required endpoint, protocol, transport and executor state remains valid through bounded completion/cancellation and confirmed output evidence is not intentionally discarded

#### Scenario: Legacy compatibility API is checked
- **WHEN** installed SDK and production consumers are inspected after migration
- **THEN** ordinary services expose neither live-bot registries nor native payload construction as the required path for common operations

## ADDED Requirements

### Requirement: Platform adapters plan bounded ordered domain delivery
The common dispatcher SHALL validate the exact route and finite request before invoking a registered platform adapter. The adapter SHALL validate all known content/source/reply constraints and construct a bounded execution plan before the first message-producing call. It SHALL enforce the explicitly selected multiplicity policy, retain full encoded conversation scope, use one logical deadline and stop after the first failed step. Platform method selection and native address decoding SHALL remain outside actor code and generic platform-independent dispatch.

#### Scenario: Unsupported block follows valid text
- **WHEN** planning finds a known unsupported later block
- **THEN** no valid prefix is sent and the request is definitely not submitted

#### Scenario: Logical deadline expires during a split
- **WHEN** the operation exhausts its total budget after earlier output
- **THEN** no new per-step deadline extends execution and the failure retains known receipts plus any uncertain in-flight effect

### Requirement: Confirmed partial outputs have separate bounded observations
Verified physical outputs SHALL be observable through a typed confirmation path independently of whether their enclosing logical send completes. Confirmations SHALL retain the originating operation/attempt correlation, exact output reference, input provenance and available reusable media so consumers can reconcile them with SendOutcome without duplicate logical admission. The existing logical success handler SHALL remain success-only and MUST NOT treat a partial failure as complete success. Registered message-producing provider extensions SHALL use the same outcome-aware normalization and observation path without generic-core provider/action switches. An outer successful wrapper containing SendFailure MUST NOT imply logical send success, including for a single-output operation; extensions without a common MessageBody MUST NOT fabricate common ContentSlice associations. Both notification paths SHALL retain the established process-owned executor, synchronized admission closure and callback-drain rules. Notification scheduling or handler failure MUST NOT modify provider outcomes, fabricate receipts or resend operations. The typed client SHALL permit an explicit bounded OperationCorrelation attempt_id alongside a send without changing its body or outcome layout. Dispatch SHALL retain a caller-supplied attempt_id in physical and logical send notifications and generate a process-unique identifier only when it is absent. Correlation MUST NOT authorize an operation, promise provider idempotency, or replace the caller's persisted attempt admission. Effect recovery SHALL reserve the actual complete serialized reply structure, including typed extension wrappers and the outer OperationReply envelope, before retaining the initial checkpoint. Checkpoint expansion, optional metadata and fallback SHALL consume the same bounded allowance; later wrapper encoding MUST NOT silently remove already-retained caller effects while keeping only observer evidence. Standalone SDK outcome codecs SHALL retain their standalone representation and limits.

#### Scenario: Partial send has confirmed messages
- **WHEN** a send returns SendFailure containing validated confirmed MessageRefs
- **THEN** confirmation observers can receive those facts without a logical-success notification for the failed request

#### Scenario: Single-output provider extension preserves a failed send
- **WHEN** a registered sticker or forward operation has a verified output but its typed result contains SendFailure
- **THEN** the caller and physical observer retain that output and operation correlation without a logical-success notification
- **AND** provider-specific node provenance remains typed extension metadata rather than fabricated common body indices

#### Scenario: A message-producing result approaches the shared wire bound
- **WHEN** primary effects fit a standalone outcome but the complete typed result and outer reply require additional structure or depth
- **THEN** recovery charges that actual structure before retaining its bounded checkpoint and reserves an encodable unknown fallback
- **AND** optional metadata repair cannot discard retained primary effects or introduce a second smaller arbitrary limit

#### Scenario: Actor has persisted an attempt before submission
- **WHEN** an actor supplies that scoped unique attempt_id through the typed send correlation overload
- **THEN** physical and logical notifications retain the same identifier, including for partial failure
- **AND** the actor need not guess the owning attempt by waiting for returned MessageRefs, while repeated correlation alone never authorizes another send

#### Scenario: Confirmation admission is already closed
- **WHEN** a late provider completion confirms an output after notification admission closure
- **THEN** its caller still receives the original typed outcome and no notification is enqueued on a retired executor

#### Scenario: Confirmation observer fails
- **WHEN** observation throws or cannot be scheduled
- **THEN** the send outcome remains unchanged and no provider operation is repeated
