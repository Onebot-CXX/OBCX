# bot-operation-dispatch Specification

## Purpose
Define exact-installation dispatch and process-owned wrappers for the closed set of currently supported OneBot 11/QQ and Telegram operations.

## Requirements
### Requirement: The process registers exact QQ and Telegram operation wrappers
The process SHALL register one native operation endpoint capability for each enabled composed installation using its configured installation id and exact `onebot11.qq` or `telegram.bot_api` surface. Registration MUST reject duplicate installation ids and unsupported surfaces. Endpoint registration MUST consume installation capability values and MUST NOT receive, wrap, retain, or cast a live universal/provider bot object.

#### Scenario: Duplicate id is registered
- **WHEN** two endpoint capabilities attempt to use the same installation id
- **THEN** startup rejects the duplicate before actor operation dispatch begins

#### Scenario: Official QQ is requested
- **WHEN** registration is attempted with `qq.official`
- **THEN** registration fails rather than exposing the OneBot endpoint under that surface

#### Scenario: Composed installations publish endpoints
- **WHEN** one enabled OneBot installation and one enabled Telegram installation complete component assembly
- **THEN** both native endpoint capabilities are addressable by their configured installation ids and exact surfaces

#### Scenario: Legacy live bot is supplied
- **WHEN** production code is checked after migration
- **THEN** no operation registration API accepts `IBot`, `IQQBot`, `ITelegramBot`, a concrete bot, or a live-bot registry entry

### Requirement: Supported actions match the closed implementation matrix
Each operation endpoint capability SHALL publish a data-only supported-action set containing only operations implemented by its installed components and listed in `qq-telegram-bot-contract`, including `message.send_private` when private messaging is installed. Optional Telegram multipart media-group upload MUST be advertised only when the same installation publishes its uploader capability. Static support reporting MUST NOT infer actions from class inheritance, RTTI, method presence, or another installation.

#### Scenario: Telegram endpoint is inspected
- **WHEN** a caller queries a composed Telegram installation
- **THEN** it sees the listed common group/private actions and `telegram.*` actions provided by that installation's components

#### Scenario: OneBot endpoint is inspected
- **WHEN** a caller queries a composed OneBot installation
- **THEN** it sees common group/private send and delete plus the listed `onebot11.*` actions and never sees Telegram or official-QQ actions

#### Scenario: Multipart uploader capability is absent
- **WHEN** a Telegram test recipe omits the optional uploader capability
- **THEN** `telegram.media.send_group_uploads` is absent and an attempted call fails before provider I/O

### Requirement: Actors call one data-only operation client
The runtime SHALL expose `BotOperationClient` as an actor service and SHALL reuse the same process-owned gateway for generation-owned command services. The operation boundary accepts only finite request values and returns typed results. It MUST NOT expose `BotRegistry`, `IBot`, provider interfaces, concrete bots, connection managers, credentials, or provider executors through its public API.

#### Scenario: Actor sends to a configured group
- **WHEN** an actor submits a valid request with exact installation and group target
- **THEN** the matching process wrapper performs the call and the actor receives a typed result without resolving a bot

#### Scenario: Command service replies to a private user
- **WHEN** generation-owned `/help` processing submits a valid private-message request with an exact installation/user target
- **THEN** the matching process wrapper performs the call through the shared gateway without exposing a provider object to command runtime

#### Scenario: Request supplies only a platform
- **WHEN** a caller omits the installation id or supplies only `qq`, `telegram`, or a surface
- **THEN** dispatch returns a definitely-not-submitted validation error and selects no bot

#### Scenario: Target belongs to another installation
- **WHEN** a request combines one installation with a group, user, or message reference scoped to another
- **THEN** dispatch rejects it before invoking the wrapper

### Requirement: Wrappers parse current provider responses conservatively
The Telegram operation component SHALL parse current `{ok,result}` success and Telegram error envelopes. The OneBot operation component SHALL parse current `status/retcode/data/echo` envelopes. Successful group/private side-effect results MUST contain required scoped message ids; empty, malformed, and synthetic responses MUST NOT be reported as success. Side-effect exceptions after invocation begins SHALL use conservative submission safety. Parsing SHALL remain process-side behind the operation endpoint capability.

#### Scenario: Telegram group, topic, or private send succeeds
- **WHEN** the Telegram operation and transport components receive a valid result message
- **THEN** the endpoint returns its correctly typed scoped chat/user and message reference

#### Scenario: OneBot group send succeeds
- **WHEN** the OneBot group operation and transport components receive successful data with `message_id`
- **THEN** the endpoint returns its scoped group/message reference

#### Scenario: OneBot private send succeeds
- **WHEN** the OneBot private operation and transport components receive successful data with `message_id`
- **THEN** the endpoint returns its scoped user/message reference

#### Scenario: Provider response is malformed
- **WHEN** a side-effecting provider call returns an empty body, invalid JSON, or nominal success without required fields
- **THEN** the endpoint returns a typed possibly-submitted error and does not fabricate success

#### Scenario: Provider explicitly rejects the action
- **WHEN** a valid provider error envelope is returned
- **THEN** the endpoint preserves only redacted code/message/retry metadata needed by current callers

### Requirement: Bot-owned media and lookup calls stay behind wrappers
Telegram authenticated file resolution/download and Telegram media sends SHALL execute inside Telegram installation components. Current OneBot member, forwarded-message, file-resolution, and poke calls SHALL execute inside OneBot installation components. The endpoint capabilities MUST enforce existing configured byte bounds and MUST NOT expose Telegram tokenized URLs, provider clients, transports, or component-registry objects to actors.

#### Scenario: Telegram media is fetched
- **WHEN** Bridge requests a Telegram file within its current byte limit
- **THEN** the Telegram endpoint uses installation capabilities to resolve and download it and returns bounded bytes and metadata

#### Scenario: Telegram file exceeds its bound
- **WHEN** declared or downloaded media exceeds the request/configured maximum
- **THEN** the endpoint returns a definitely classified size failure and discards the oversized value

#### Scenario: OneBot provider-specific lookup runs
- **WHEN** Bridge requests a listed OneBot member, forward, or file operation
- **THEN** the OneBot endpoint validates the provider response and returns the corresponding provider-namespaced value

### Requirement: Existing process lifecycle and compatibility remain authoritative
One shared operation dispatcher/client SHALL be reused across actor generations and SHALL route to process-owned installation endpoint capabilities. Validation-only MUST cause no network or persistent side effect, and reload MUST NOT recreate active installations. The final runtime and installed SDK MUST remove compatibility `BotRegistry`, live-bot wrappers, oversized bot interfaces, and provider bot interfaces; only the data-only operation client remains actor-visible.

#### Scenario: Actor generation reloads
- **WHEN** a candidate actor generation is built
- **THEN** it receives the same process operation client and validates routes against active installation capabilities without recreating installations

#### Scenario: Validation-only startup runs
- **WHEN** configuration is validated without normal startup
- **THEN** installation references, surfaces, recipes, and supported actions are checked without constructing a transport, calling a provider, or creating operation state

#### Scenario: Process shuts down
- **WHEN** runtime shutdown begins while an operation is in flight
- **THEN** installation component lifetime rules retain required endpoint, protocol, transport, and executor state through bounded completion or cancellation

#### Scenario: Legacy compatibility API is checked
- **WHEN** installed SDK and production source architecture tests run
- **THEN** no actor service or operation path exposes `BotRegistry`, universal/provider bot interfaces, or concrete bot classes

### Requirement: Dispatcher coverage is limited to testable QQ and Telegram paths
Automated tests SHALL cover every advertised action with fake operation components or mock transports, including exact routing, wrong surface, unsupported action, valid success, explicit provider failure, malformed response, and uncertain side-effect exception. Tests SHALL also prove endpoint registration requires composed capabilities and MUST NOT require inherited fake bots, credentials, or an implementation branch for an unsupported platform.

#### Scenario: Advertised-action conformance runs
- **WHEN** the Telegram and OneBot endpoint suites enumerate supported actions
- **THEN** every advertised action has an executable success test and no action outside the closed matrix is present

#### Scenario: Dispatcher architecture is checked
- **WHEN** production dispatcher and endpoint sources are scanned and compiled
- **THEN** they contain no live-bot registry lookup, provider bot cast, or dependency on removed bot headers
