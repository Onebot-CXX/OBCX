# qq-telegram-bot-contract Specification

## Purpose
Define the finite data-only request, result, and error contract for current OneBot 11/QQ and Telegram actor operations.

## Requirements
### Requirement: Bot references are scoped to an implemented installation
The bot-operation contract SHALL identify a configured bot by `BotInstallationRef` containing an exact installation id and either `telegram.bot_api` or `onebot11.qq`. Group, private-user, and message references MUST contain that installation. Group and private targets MUST remain distinct even when their native IDs have equal text. Telegram message references MUST carry chat id and message id as separate fields. The contract MUST NOT represent OneBot 11 as `qq.official`.

#### Scenario: Equal ids belong to different installations
- **WHEN** two configured bots report the same native group, private user, or message id
- **THEN** their scoped references compare as different resources

#### Scenario: Group and private IDs have equal text
- **WHEN** one installation has a group and private user with the same native ID text
- **THEN** `GroupTarget` and `PrivateTarget` represent distinct destinations and cannot be substituted for each other

#### Scenario: Telegram delete is represented
- **WHEN** Telegram message `42` in chat `-1001` is targeted for deletion
- **THEN** the request contains installation, chat `-1001`, and message `42` as separate values rather than a colon-encoded string

#### Scenario: Current QQ adapter is referenced
- **WHEN** a request targets the implemented QQ-compatible bot
- **THEN** its exact surface is `onebot11.qq` and no field implies official QQ support

### Requirement: The first operation set is closed to current QQ and Telegram calls
The contract SHALL define only `message.send_group`, `message.send_private`, `message.delete`, `telegram.message.send_topic`, `telegram.message.edit_text`, `telegram.media.send_photo`, `telegram.media.send_group_urls`, `telegram.media.send_group_uploads`, `telegram.media.fetch_file`, `onebot11.group_member.get`, `onebot11.forward_message.get`, `onebot11.group_file.resolve`, `onebot11.private_file.resolve`, and `onebot11.group.poke`. It MUST NOT define portable history, contact, moderation, reaction, poll, membership-list, request-handling, credential, or unsupported-platform operations in this change.

#### Scenario: Current actor operation is represented
- **WHEN** an actor or generation-owned command service needs one of the listed QQ or Telegram calls
- **THEN** it can construct the corresponding typed request without a live bot object

#### Scenario: Private text reply is requested
- **WHEN** an authorized command service needs to reply to an exact Telegram or OneBot private user
- **THEN** it can construct `message.send_private` with an exact `PrivateTarget` and serializable message segments

#### Scenario: Unused legacy method is requested
- **WHEN** a caller attempts to express an `IBot` action outside the closed list
- **THEN** the contract provides no matching request type and the dispatcher cannot advertise it

#### Scenario: Another platform is considered
- **WHEN** code attempts to construct an operation for Discord, official QQ, WeChat, WeCom, Lark, DingTalk, Matrix, X, or another unsupported surface
- **THEN** validation rejects the surface and no compatibility alias maps it to Telegram or OneBot

### Requirement: Requests are data-only and preserve current payload semantics
Every request SHALL contain value data only and MUST NOT contain `IBot`, provider interfaces, connection managers, executors, credentials, streams, or unrestricted filesystem paths. Group and private sends SHALL reuse the existing serializable `common::Message` segment payload with distinct exact target types. Telegram topic/entity/media and OneBot lookup/poke fields SHALL use explicitly provider-namespaced request values rather than speculative portable abstractions.

#### Scenario: Actor sends existing message segments
- **WHEN** an actor submits a current group or private message
- **THEN** text, reply, image, and other existing segments round-trip without conversion to a new universal content model

#### Scenario: Private target is incomplete
- **WHEN** a private request omits installation identity or native user ID
- **THEN** value validation rejects it before dispatcher or provider I/O

#### Scenario: Telegram file is fetched
- **WHEN** Bridge submits `telegram.media.fetch_file`
- **THEN** it supplies Telegram file metadata and a byte limit and receives bounded file data without a bot token, tokenized URL, HTTP client, or local path

#### Scenario: Telegram upload is submitted
- **WHEN** Bridge sends its current multipart media-group fallback
- **THEN** each upload is a bounded filename, MIME type, kind, and byte value with optional topic, reply, caption, and entity values

### Requirement: Successful results contain the values current actors consume
A successful group send SHALL return one or more scoped `BotMessageRef` values; a successful private send SHALL return one or more scoped `PrivateMessageRef` values. Neither result may require callers to extract IDs from provider JSON. Delete and edit SHALL return typed mutation status. OneBot member and file lookup results SHALL expose the fields current Bridge code consumes. A forwarded-message result MAY contain validated `onebot11.qq` node JSON, but MUST identify it as provider-specific rather than portable content.

#### Scenario: Telegram group send succeeds
- **WHEN** Telegram returns a valid group message object
- **THEN** the typed result contains its exact installation, chat id, and message id

#### Scenario: Telegram private send succeeds
- **WHEN** Telegram returns a valid private message object
- **THEN** the typed result contains the requested exact installation/user target and returned message ID

#### Scenario: OneBot group send succeeds
- **WHEN** OneBot returns successful `status/retcode/data` with a message id
- **THEN** the typed result contains a `BotMessageRef` for the requested OneBot group

#### Scenario: OneBot private send succeeds
- **WHEN** OneBot returns successful private-send data with a message id
- **THEN** the typed result contains a `PrivateMessageRef` for the requested exact OneBot user

#### Scenario: OneBot member lookup succeeds
- **WHEN** OneBot returns group-member data used for Bridge formatting
- **THEN** the result exposes the user id and available card, nickname, and title values without exposing the complete response envelope

### Requirement: Errors distinguish safe retry from possible submission
`BotOperationError` SHALL contain a stable error code, redacted message, optional provider code and retry delay, a retryable flag, and submission safety of `DefinitelyNotSubmitted` or `PossiblySubmitted`. Route, validation, and unsupported failures MUST be definitely not submitted. A malformed success, timeout, disconnect, or exception after a side-effecting call begins MUST default to possibly submitted unless the wrapper can prove no provider write occurred.

#### Scenario: Exact route does not exist
- **WHEN** a request names an unknown installation
- **THEN** it returns a definitely-not-submitted route error and performs no provider call

#### Scenario: Transport fails before request writing
- **WHEN** DNS resolution, TCP connection, proxy-tunnel setup, or TLS handshake fails before an HTTP request write begins
- **THEN** the result is retryable and definitely not submitted

#### Scenario: Provider explicitly rejects a send
- **WHEN** Telegram `{ok:false}` or a failed OneBot `status/retcode` proves the send was rejected
- **THEN** the result carries the provider failure and only marks it retryable when the parsed provider response supports that classification

#### Scenario: Nominal success lacks message identity
- **WHEN** a side-effecting send returns a malformed nominal success without the required message id
- **THEN** the result is possibly submitted and no message id is fabricated

### Requirement: Public values serialize deterministically
Every public installation, target, message reference, request, success value, and error SHALL provide deterministic JSON conversion and validation suitable for independently built actor packages. Deserialization MUST reject missing routing fields, an invalid surface/action pairing, non-positive Telegram topic ids where a topic is required, and media values over their declared bound.

#### Scenario: Value crosses an installed SDK boundary
- **WHEN** a valid request or result is serialized and deserialized by a standalone actor fixture
- **THEN** all routing, payload, result, error, and provider-specific fields are preserved

#### Scenario: Surface and action disagree
- **WHEN** a OneBot installation is paired with a `telegram.*` request
- **THEN** validation fails before dispatcher or provider I/O
