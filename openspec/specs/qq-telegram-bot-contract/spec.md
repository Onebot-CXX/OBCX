# qq-telegram-bot-contract Specification

## Purpose
Define current scoped, data-only bot requests, complete outcomes and reviewed
OneBot/Telegram extensions supplied by the matching installed SDK/core.

## Requirements

### Requirement: Bot references are scoped to an implemented installation
The contract SHALL identify configured bots by exact BotInstallationRef and
registered surface, currently telegram.bot_api or onebot11.qq, never an alias
to qq.official. ConversationRef SHALL contain installation and one opaque
canonical ConversationId encoding complete kind/sub-conversation scope. UserRef
SHALL contain installation and UserId; MessageRef SHALL contain ConversationRef
and a separate MessageId. Ordinary callers MUST NOT assemble native topic/thread
routes or parse opaque components. The codec SHALL bound and validate canonical,
versioned, unambiguous scope.

#### Scenario: Equal ids belong to different installations
- **WHEN** two installations report equal native conversation, user or message IDs
- **THEN** their full scoped references identify different resources

#### Scenario: Group and private IDs have equal text
- **WHEN** a group and private user share native ID text
- **THEN** their ConversationIds remain distinct and cannot substitute for each other

#### Scenario: Telegram delete is represented
- **WHEN** Telegram message 42 in a particular chat/topic is deleted
- **THEN** the request supplies its full ConversationRef and separate MessageId without actor-side topic arguments or string splitting

#### Scenario: Current QQ adapter is referenced
- **WHEN** the implemented QQ-compatible adapter is targeted
- **THEN** the surface is exactly onebot11.qq and implies no official QQ support

#### Scenario: Two topics share one native chat
- **WHEN** different Telegram topics are observed
- **THEN** their complete references retain their destinations without exposing an ordinary thread field

#### Scenario: Address components include delimiter-like data
- **WHEN** native components contain codec delimiter-like characters
- **THEN** validated encoding remains unambiguous and collision-free

### Requirement: The first operation set is closed to current QQ and Telegram calls
The common production contract SHALL admit registered message.send,
message.delete, bot.self.get, member.get, message.reply.resolve, media.read and
bot.capabilities.get plus explicitly reviewed separately namespaced provider
extensions. Implementations SHALL remain endpoint-local/capability-dependent.
Old group/private messaging facades and ordinary native topic sends SHALL NOT
be exposed. Portable arbitrary-user/history/contact/moderation/reaction/poll/
membership/request/credential APIs and unsupported platforms remain outside the
common set. Extensions MUST NOT enable arbitrary native dispatch.

#### Scenario: Current actor operation is represented
- **WHEN** an actor needs an approved common operation or retained extension
- **THEN** it constructs the typed request without a live bot or arbitrary provider method

#### Scenario: Private text reply is requested
- **WHEN** a command replies to an exact private conversation
- **THEN** it uses SendMessageRequest and MessageRef just as group processing does

#### Scenario: Unused legacy method is requested
- **WHEN** an unregistered action or native method is submitted
- **THEN** dispatch rejects it before I/O without raw-dispatch fallback

#### Scenario: Another platform is considered
- **WHEN** an unregistered surface is targeted
- **THEN** validation rejects it rather than aliasing it to an implemented platform

#### Scenario: Self query is requested
- **WHEN** get_self targets either implemented platform
- **THEN** bot.self.get returns shared cache-only SelfInfo or explicit unavailability

### Requirement: Requests are data-only and preserve current payload semantics
Public requests SHALL contain strongly typed owning data, not live provider
interfaces, executors, credentials or streams. Ordinary sends SHALL use typed
MessageBody and ConversationRef rather than native segment JSON. Final LocalFile
paths SHALL remain caller-owned strings, not filesystem authority or SDK path
mapping. Adapters SHALL preserve supported text/reply/media meaning or explicitly
reject unsupported content rather than degrade it. Native-only lookup/edit/
forward/management fields SHALL remain provider-namespaced. SDK work SHALL obey
request/configured bounds; business authorization and deployment mapping remain
caller-owned.

#### Scenario: Actor sends existing message segments
- **WHEN** existing text/reply/image sending is migrated
- **THEN** equivalent typed content retains its meaning through adapter-owned conversion
- **AND** unknown native segments require an explicit reviewed extension or rejection

#### Scenario: Private target is incomplete
- **WHEN** installation or complete ConversationId is absent/invalid
- **THEN** validation rejects the request before I/O

#### Scenario: Telegram file is fetched
- **WHEN** authorized media.read supplies a Telegram MediaRef and explicit byte limit
- **THEN** bounded owned bytes are returned without credentials, tokenized URLs or provider-local paths

#### Scenario: A live QQ market face is observed
- **WHEN** admitted QQ media has a bounded market-face resource observation
- **THEN** its typed projection retains only installation/account/ingress-bound read eligibility
- **AND** a pure view, restored value or changed reference kind cannot mint read authority or assert an unchecked animation encoding

#### Scenario: Telegram upload is submitted
- **WHEN** supported bytes and explicit caption/reply/delivery limits are supplied
- **THEN** the adapter owns multipart construction and extracts topic scope from the target

### Requirement: Successful results contain the values current actors consume
A complete send SHALL retain every validated scoped MessageRef, typed input
provenance and available reusable media in output_details. Actors MUST NOT
extract IDs from provider JSON. Delete/edit results SHALL be typed; common
member results SHALL expose normalized available fields. Provider forwards SHALL
retain bounded typed nodes/authors/ordered content/scoped media rather than raw
native-node JSON. Partial/uncertain output SHALL use SendFailure retaining known
receipts, not complete success or an evidence-free error.

#### Scenario: Telegram group send succeeds
- **WHEN** Telegram returns a valid group result
- **THEN** its exact installation, opaque conversation and MessageId are retained

#### Scenario: Telegram private send succeeds
- **WHEN** Telegram returns a valid private result
- **THEN** the same MessageRef type identifies the exact private conversation

#### Scenario: OneBot group send succeeds
- **WHEN** OneBot returns valid successful message identity
- **THEN** the common MessageRef is scoped to the requested group

#### Scenario: OneBot private send succeeds
- **WHEN** OneBot returns a valid private message identity
- **THEN** its common MessageRef can be supplied directly to delete_message

#### Scenario: OneBot member lookup succeeds
- **WHEN** a common member query obtains card/nickname/profile evidence
- **THEN** normalized common fields require no actor-side native-field selection
- **AND** retained native-only fields remain in explicit extensions

#### Scenario: Only a send prefix succeeds
- **WHEN** later sending fails after confirmed outputs
- **THEN** SendFailure retains those references and any unknown submission

#### Scenario: Existing authorless provider forward is explicitly requested
- **WHEN** an approved explicitly authorless extension node is submitted
- **THEN** native author/display fields are omitted rather than fabricated
- **AND** bounded preparation, one deadline and complete physical-output accounting still apply; ordinary authored nodes keep their author requirements
- **AND** success/confirmation does not prove an author and rejection triggers no fallback

#### Scenario: Bridge renders queried forward CQ-string content
- **WHEN** scoped forward lookup returns standard CQ-string content
- **THEN** Bridge consumes parsed ordered typed text, mentions and media
- **AND** acquisition remains bounded/SDK-owned without private locator export, fabricated node MessageRefs or whole-request retry after possible submission
- **AND** inline-node literal observation behavior remains separate

#### Scenario: A received forward node has an empty display name
- **WHEN** lookup observes an empty nickname
- **THEN** an engaged empty display_name remains distinct from an absent name
- **AND** bounded validation rejects malformed/oversized present names without inventing authors

#### Scenario: Prepared forward content is checked without provider I/O
- **WHEN** prepared literal text and LocalFile/BytesSource nodes are preflighted
- **THEN** actual native identity widths, separators, JSON escapes and maximum uint64 echo width count toward the explicit envelope ceiling
- **AND** native IDs/node JSON stay SDK-private and unknown remote content needs preparation/final bound checks

#### Scenario: A direct plan retains independent message budgets
- **WHEN** ExHentai preflights an independent-forward plan
- **THEN** each node retains its one-message budget and the plan retains envelope/node-count limits
- **AND** caller cumulative disk/batch limits remain independent; a real group-forward cumulative budget is not relaxed

### Requirement: Errors distinguish safe retry from possible submission
BotOperationError SHALL retain stable code, redacted message, optional provider
code/retry delay, retryability and DefinitelyNotSubmitted/PossiblySubmitted
safety. Pre-call route/validation/unsupported failures SHALL be definitely not
submitted. After an effect begins, malformed success/timeout/disconnect/error
SHALL be possibly submitted unless absence is proven. SendFailure SHALL retain
confirmed references and unknown-output evidence. Any confirmed/possible output
SHALL prohibit whole-request automatic retry regardless of a later rejection.

#### Scenario: Exact route does not exist
- **WHEN** installation routing is unknown
- **THEN** a definitely-not-submitted route error occurs without provider I/O

#### Scenario: Transport fails before request writing
- **WHEN** connection setup fails before the first message-producing write
- **THEN** transport evidence may permit safe retry
- **AND** any earlier logical-request output still prohibits whole-request retry

#### Scenario: Provider explicitly rejects a send
- **WHEN** provider evidence proves rejection of one step
- **THEN** aggregate safety also accounts for prior confirmed/uncertain outputs

#### Scenario: Nominal success lacks message identity
- **WHEN** a side-effecting nominal success lacks verifiable identity
- **THEN** the result is possibly submitted, fabricates no ID and retains other receipts

### Requirement: The installed SDK includes its matching core
The installed SDK SHALL include its matching imported core, transitively linked
by common/platform SDK targets. Platform implementations remain in core, not
new implementation libraries/plugins. Public headers MUST NOT expose private
codecs, registries or transports. Isolated actor builds SHALL consume a frozen
SDK without rebuilding/replacing the live core.

#### Scenario: Consumer selects a platform SDK target
- **WHEN** the installed OneBot or Telegram SDK target is linked
- **THEN** its closure includes matching imported core without manual core linkage or a standalone platform implementation library

### Requirement: Public values serialize deterministically
Public references, requests, outcomes, events and errors SHALL provide bounded
SDK-owned deterministic versioned serialization. Missing scope, malformed/
noncanonical encodings, incompatible action/surface and exceeded bounds SHALL
fail before extraction or I/O. Private platform codecs additionally validate
native scope at ingress/configuration/admission; structural decoding alone is
not native validity or authorization. Current DTO layouts SHALL NOT masquerade
as the preceding SelfInfo/reference contract. Internal binary media SHALL avoid
per-byte JSON arrays. Current configuration resolution SHALL be pure and
platform-owned, not a historical import service. Ordinary SDKs SHALL expose no
generic legacy reference conversion, archive restoration or compatibility
projection. Views SHALL consume current named observations. Restore SHALL
perform no I/O/admission or substitute now/sent-time/zero for required receive
time. Configuration ambiguity MUST fail closed.

#### Scenario: Value crosses an installed SDK boundary
- **WHEN** matching SDKs encode/decode a current request/result
- **THEN** full routing, content, outcome and uncertainty evidence survives

#### Scenario: Surface and action disagree
- **WHEN** a OneBot installation receives a Telegram-only request
- **THEN** validation fails before I/O

#### Scenario: Encoded conversation is invalid
- **WHEN** restored scope is malformed, noncanonical or has invalid native topic identity
- **THEN** the owning codec rejects it without guessing a parent

#### Scenario: Actor resolves a configured destination
- **WHEN** explicit current installation/kind/native ID/topic is supplied
- **THEN** the owning binding resolves without I/O, actor codecs, persisted-key rewriting or new admission

#### Scenario: Required typed receive time is unavailable
- **WHEN** serialized MessageReceived lacks recorded receive time
- **THEN** decoding rejects it without substituting current/sent/zero time

#### Scenario: Old self DTO is read
- **WHEN** the preceding installation/user_id SelfInfo shape is supplied
- **THEN** it is rejected rather than translated or accepted through an old contract gate

### Requirement: Final media paths and file lifetime remain caller owned
Callers SHALL supply final paths after caller-owned mapping. SDK/core MUST NOT
integrate a mapper, require shared-file registration or impose OBCX-host-only
path semantics. Path-capable OneBot adapters SHALL own native file-URI encoding
for ordinary/constructed-forward content. Actors retain mounts, accessibility,
leases, quotas and cleanup through possible submission. Structural validation
SHALL NOT claim filesystem existence/size verification.

#### Scenario: Actor sends a shared image
- **WHEN** a final provider-visible path is supplied
- **THEN** the adapter serializes it without registration/SDK mapping or releasing the actor's lease

#### Scenario: Provider container path is not visible to OBCX
- **WHEN** a path-capable adapter gets a provider-only path
- **THEN** inability to stat it locally does not reject it or imply verified provider existence/size

#### Scenario: Shared media send may have been submitted
- **WHEN** path-based sending has possible submission
- **THEN** the outcome cannot authorize treating the file as unused or automatically resending

#### Scenario: Caller restores a pending file send after restart
- **WHEN** a pending request/file-lifetime record is restored
- **THEN** accessibility, lease recovery and retry permission remain caller-owned without SDK registration
