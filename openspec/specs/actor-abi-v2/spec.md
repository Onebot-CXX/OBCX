# actor-abi-v2 Specification

## Purpose
TBD - created by archiving change native-actor-coroutine-scheduler. Update Purpose after archive.
## Requirements
### Requirement: Explicit actor ABI generation
Every actor library SHALL expose the supported numeric V2 ABI generation,
independent from the actor's semantic version. ActorManager MUST determine the
generation before interpreting factory results or dispatching messages and
SHALL implement no loader path for earlier actor or plugin ABIs.

#### Scenario: Supported V2 generation loads
- **WHEN** an actor library reports the supported V2 ABI generation and provides all required V2 symbols
- **THEN** ActorManager constructs and registers it as an IActorV2

#### Scenario: Library does not provide the V2 contract
- **WHEN** a dynamic library lacks the supported V2 generation or required V2 symbols
- **THEN** ActorManager does not construct or register an actor from that library

### Requirement: V2 actor interface uses native ActorTask
The V2 actor interface SHALL return `ActorTask<ActorResult>` from
`handle_message` and SHALL NOT expose `boost::asio::awaitable` in its actor
dispatch contract. The SDK SHALL provide a V2 export helper and installed
headers required to build standalone V2 actors.

#### Scenario: Standalone V2 actor builds against installed SDK
- **WHEN** an external actor implements IActorV2 and uses the V2 export helper
- **THEN** it compiles, links, loads, and handles a message using only the installed OBCX actor SDK contract

#### Scenario: V2 actor suspends natively
- **WHEN** a V2 actor yields or awaits an ActorContext asynchronous operation
- **THEN** ActorScheduler retains ownership of its native ActorTask continuation

### Requirement: Public scheduling payload is renamed deliberately
The scheduler submission payload SHALL be named `ActorInvocation`, and
`ActorTask<T>` SHALL be reserved for the native coroutine type. Migration
documentation MUST identify the source-level rename for scheduler API users.

#### Scenario: Scheduler client migrates payload construction
- **WHEN** source code using the old non-coroutine ActorTask payload is rebuilt against V2 headers
- **THEN** documented replacement with ActorInvocation restores equivalent submission behavior

### Requirement: V2 actor library exports a generated input contract
Every V2 actor library SHALL export `extern "C" const char *obcx_get_actor_contract()` through the V2 actor export helper. The returned pointer MUST have static lifetime, and the JSON document MUST contain a supported `schema_version`, the exported actor identity, and the unique canonical `accepted_inputs` generated from the same reflected handler set used by dispatch. The contract MAY contain deterministic command registrations generated from an actor's command declarations; each registration MUST contain a canonical command name, description, and request type that is also present in `accepted_inputs`.

The contract MAY also contain deterministic scalar configuration constraints, `bot_installation_collections`, and `collection_identity_references`. Each bot-installation collection constraint MUST identify an actor-config collection, minimum cardinality, item identity field, repeated bot-installation fields and their allowed root bot types, and any scalar/collection alternative group. An identity reference MUST data-declaratively connect an actor scalar or fields in named root table arrays to an existing collection identity. These constraints MUST contain values only and MUST NOT expose a validator callable, live bot, credentials, provider object, connection manager, or executor. The contract SHALL NOT declare handler names, callables, member-function pointers, platform implementations, completion functions, or general actor outputs.

#### Scenario: Reflected actor exports its contract
- **WHEN** an actor library is built with the V2 export helper
- **THEN** the contract symbol returns deterministic JSON whose accepted inputs exactly match the actor's discovered handlers

#### Scenario: Actor exports command registrations
- **WHEN** an actor declares command observations for named reflected request message types
- **THEN** the same contract document contains deterministic command registrations mapping each command name to its description and canonical accepted request type without callable metadata

#### Scenario: Actor author adds or removes a handler
- **WHEN** the actor's valid reflected handler set changes and the library is rebuilt
- **THEN** both generated dispatch and exported accepted inputs change from the same compile-time source

#### Scenario: Actor author changes a command declaration
- **WHEN** an actor adds, removes, renames, or remaps a command declaration and the library is rebuilt
- **THEN** the exported command registrations change deterministically while reflected dispatch remains derived from typed handlers

#### Scenario: Actor exports repeated bot references
- **WHEN** an actor declares a configuration collection containing named bot-installation fields
- **THEN** the same static contract contains deterministic finite collection constraints with field names and expected bot types but no runtime bot value or callable

#### Scenario: Actor has no commands or configuration constraints
- **WHEN** an actor declares neither commands nor actor-owned configuration constraints
- **THEN** its generated contract remains valid and represents empty optional command/configuration sets

#### Scenario: Actor author changes a collection constraint
- **WHEN** an actor changes required collection fields, allowed bot types, cardinality, identity, or alternative form
- **THEN** the exported configuration contract changes deterministically without adding executable ABI metadata

### Requirement: ActorManager validates input contracts before registration
ActorManager SHALL load and parse the required input contract before constructing or registering an actor. It MUST reject a library whose symbol is missing, whose pointer/document is invalid, whose schema version is unsupported, whose actor identity disagrees with the exported actor name, or whose accepted inputs are malformed or duplicated. When command registrations are present, ActorManager MUST also reject malformed or nondeterministically ordered entries, duplicate command names, invalid canonical names, empty descriptions, and request types absent from the same contract's accepted input set.

When configuration constraints are present, ActorManager MUST reject unknown members, malformed scalar constraints, malformed installation collections or identity references, duplicate collection/item/reference names, empty identity/field names, references to unknown collection identities, unsupported bot types, invalid cardinality, and inconsistent alternative-group declarations. Parsing the contract MUST NOT inspect runtime credentials or start a bot.

#### Scenario: Valid contract loads
- **WHEN** a V2 actor library reports the supported ABI generation and exposes a valid supported input contract
- **THEN** ActorManager stores the parsed contract with the loaded actor and may register the actor

#### Scenario: Valid command registrations load
- **WHEN** a contract contains sorted unique command registrations whose request types are accepted inputs
- **THEN** ActorManager stores those registrations with the actor contract for later generation validation

#### Scenario: Command request type is not accepted
- **WHEN** a command registration names a request type absent from the contract's accepted inputs
- **THEN** ActorManager rejects the library before actor construction or registration

#### Scenario: Command registration contains callable metadata
- **WHEN** a command registration contains a handler, function, callable, or member-pointer field
- **THEN** ActorManager rejects the unsupported member instead of treating it as dispatch metadata

#### Scenario: Old actor library lacks the contract
- **WHEN** a V2 actor library does not export `obcx_get_actor_contract`
- **THEN** ActorManager rejects it cleanly before actor construction or registration and reports the missing symbol

#### Scenario: Contract is malformed or unsupported
- **WHEN** the contract is invalid JSON or violates the supported contract schema
- **THEN** ActorManager rejects the library, releases its dynamic-library resources, and reports the contract error

#### Scenario: Valid installation collection loads
- **WHEN** a contract contains a deterministic collection constraint with unique required fields and supported bot types
- **THEN** ActorManager stores that data-only constraint for runtime-generation configuration validation

#### Scenario: Collection constraint is malformed
- **WHEN** a configuration collection has no identity field, repeats a bot field, supplies an empty expected-type set, or declares contradictory alternatives
- **THEN** ActorManager rejects the library before actor construction or registration

#### Scenario: Configuration contract contains executable metadata
- **WHEN** configuration metadata contains a validator symbol, function, bot object, credential, connection, or executor field
- **THEN** ActorManager rejects the unsupported member rather than loading it as configuration data

### Requirement: Repeated bot-installation configuration is validated before activation
Runtime-generation validation SHALL apply each actor's scalar and collection bot-installation constraints to its configured values before actor activation. Every referenced installation MUST name an enabled root bot whose type is allowed by the declared field. Collection identity values MUST be non-empty and unique, required fields/cardinality MUST be satisfied, mutually exclusive scalar/collection forms MUST not be mixed, and configured scalar/root-table references MUST resolve to the selected collection identity. A pair reference MAY be omitted only when its declaration is optional and the selected collection does not have multiple items.

#### Scenario: Several valid installation pairs are configured
- **WHEN** an actor collection contains unique items whose Telegram and QQ fields name enabled bots of the declared types
- **THEN** startup, validation-only mode, and candidate preparation accept all references without starting provider I/O

#### Scenario: Nested installation is missing
- **WHEN** any collection item names a root bot that does not exist
- **THEN** generation validation fails before actor activation and identifies the actor, collection item, and field without exposing configuration secrets

#### Scenario: Nested installation has the wrong type
- **WHEN** a collection field declared for Telegram names an enabled QQ bot or vice versa
- **THEN** generation validation fails before actor activation with the expected and actual non-secret types

#### Scenario: Installation is disabled
- **WHEN** a collection item names a configured but disabled bot
- **THEN** generation validation rejects the actor configuration before activation

#### Scenario: Collection identities are duplicated
- **WHEN** two collection items use the same declared identity value
- **THEN** validation rejects the collection deterministically before actor activation

#### Scenario: Alternative configuration forms are mixed
- **WHEN** actor configuration supplies both scalar and collection forms declared as mutually exclusive alternatives
- **THEN** validation rejects the configuration rather than merging or preferring one form

#### Scenario: Root mapping references an unknown pair
- **WHEN** an actor's declared root table-array reference is missing in multi-item mode or names an identity absent from the selected collection
- **THEN** validation rejects the actor configuration before activation without actor-specific core code

#### Scenario: Optional legacy selector is present
- **WHEN** an optional actor scalar such as `legacy_state_pair` is configured
- **THEN** its value must resolve to one identity in the selected collection

#### Scenario: Validation-only mode checks collections
- **WHEN** `--validate-config` receives valid or invalid repeated installation references
- **THEN** it returns the same validation result as normal startup without constructing bots, workers, ingress, or actor-owned state

#### Scenario: Reload candidate has an invalid nested reference
- **WHEN** an active generation exists and a candidate changes a collection to a missing, disabled, or wrong-type installation
- **THEN** candidate preparation fails before cutover and leaves the active generation unchanged

### Requirement: V2 actors may prepare a generation before route activation
The V2 export helper SHALL provide an additive generation-preparation entry point. Actors MAY implement a typed `prepare_generation(ActorContext&)` hook; actors without that hook and previously built V2 libraries without the additive symbol MUST remain loadable and SHALL be treated as ready. The runtime MUST invoke preparation after validated configuration and generation services are available but before registering the actor for scheduler, command, or pipeline ingress. Preparation MUST return one of ready, failed, or restart-required without exposing actor callables through the input contract.

#### Scenario: Actor prepares startup-owned state
- **WHEN** a startup generation constructs an actor with a generation-preparation hook
- **THEN** the runtime supplies its exact actor id, database scope, configuration view, generation purpose, and runtime services before any route can invoke that actor

#### Scenario: Preparation fails
- **WHEN** an actor returns a failed preparation result
- **THEN** generation construction fails with the bounded actor diagnostic and no route is published to that generation

#### Scenario: Preparation requires process restart
- **WHEN** a reload candidate reports that actor-owned state requires a startup-only migration
- **THEN** candidate construction returns typed `reload_restart_required`, leaves the active generation authoritative, and does not publish the candidate

#### Scenario: Existing V2 actor has no preparation export
- **WHEN** ActorManager loads a valid previously built V2 actor that lacks the additive preparation symbol
- **THEN** it preserves existing construction and dispatch behavior by treating preparation as ready

#### Scenario: Validation-only preparation runs
- **WHEN** validation-only generation construction invokes an actor preparation hook
- **THEN** the actor can validate actor-specific configuration from `ActorGenerationPurpose::ValidationOnly` without starting ingress or mutating actor-owned state
