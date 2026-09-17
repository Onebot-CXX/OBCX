# bot-component-runtime Specification

## Purpose
Define installation-scoped component composition, capabilities, and safe process lifecycle.

## Requirements
### Requirement: A bot installation is a composition root
The process SHALL represent each enabled configured bot as one installation-scoped runtime that owns its identity, executor, component instances, capability registry, and lifecycle. The installation MUST NOT inherit provider, protocol, transport, messaging, media, moderation, command, or operation capability interfaces.

#### Scenario: OneBot installation is assembled
- **WHEN** a valid `onebot11.qq` installation is created
- **THEN** the process assembles its reviewed protocol, transport, ingress, and operation components under one installation owner without constructing a `QQBot` god object

#### Scenario: Telegram installation is assembled
- **WHEN** a valid `telegram.bot_api` installation is created
- **THEN** the process assembles its reviewed protocol, HTTP transport, ingress, operation, and command-catalog components without constructing a `TGBot` god object

### Requirement: Components publish explicit installation-scoped capabilities
Each component SHALL declare a stable component id, the capability ids it provides, and the capability ids it requires. Capabilities SHALL be process-local typed contracts or values installed in the owning installation registry; they MUST NOT be discovered from installation inheritance, live-bot casts, method presence, or global mutable state.

#### Scenario: Required capability is available
- **WHEN** an operation component declares a protocol and transport capability supplied by components in the same recipe
- **THEN** assembly resolves the exact typed capability instances from that installation

#### Scenario: Duplicate single-valued capability is provided
- **WHEN** two components in one installation provide the same single-valued capability id
- **THEN** assembly fails before any component starts and does not choose a provider by registration order

#### Scenario: Capability is requested across installations
- **WHEN** a component belonging to one installation attempts to resolve a capability from another installation
- **THEN** resolution fails and no cross-installation object is returned

### Requirement: Component dependencies form a validated deterministic graph
The runtime SHALL validate all component dependencies and detect missing capabilities and dependency cycles before provider I/O. It SHALL derive a deterministic topological prepare/start order, using reviewed recipe order as the stable tie-breaker, and SHALL use reverse order for stop and destruction.

#### Scenario: Dependency is missing
- **WHEN** a recipe omits a capability required by one of its components
- **THEN** installation assembly reports the component and missing capability and starts nothing

#### Scenario: Dependency cycle exists
- **WHEN** selected components form a capability dependency cycle
- **THEN** installation assembly reports the cycle and starts nothing

#### Scenario: Independent components are assembled repeatedly
- **WHEN** the same valid recipe is assembled more than once
- **THEN** each installation computes the same lifecycle order

### Requirement: Component startup is transactional and shutdown is safe
The runtime SHALL construct and validate all components before startup, prepare subscriptions before transport ingress begins, start components in dependency order, and roll back started or prepared components in reverse order after any failure. Stop SHALL be idempotent, SHALL close new admission before draining or cancelling work, and destructor paths MUST NOT emit exceptions or destroy the installation executor before executor-dependent components.

#### Scenario: Component start fails
- **WHEN** a component throws or returns failure after earlier components started
- **THEN** the runtime stops the already-started components exactly once in reverse order and reports installation startup failure

#### Scenario: Stop is requested twice
- **WHEN** process shutdown and destruction both request installation stop
- **THEN** every component observes at most one effective stop transition and no operation or ingress is accepted afterward

#### Scenario: Operation is in flight during shutdown
- **WHEN** shutdown starts while provider work is awaiting transport completion
- **THEN** existing bounded completion or cancellation policy runs while required components and the installation executor remain alive

### Requirement: Event ingress is an installed capability
Message and notice event publication SHALL be provided by an installation event capability. Process subscribers SHALL attach before the transport starts, and every actor ingress envelope SHALL receive the owning installation id and exact surface as data without inspecting a bot class or using RTTI.

#### Scenario: Event arrives from an installation
- **WHEN** a protocol/transport component publishes a valid message event
- **THEN** registered process ingress receives it with the exact configured installation id and surface

#### Scenario: Transport starts without event dependency
- **WHEN** a recipe would start a transport before its required ingress subscription capability is prepared
- **THEN** dependency validation or lifecycle ordering prevents transport startup

### Requirement: Process-only platform features are capabilities
Process consumers such as command-catalog publication SHALL obtain an explicit installation capability and MUST NOT accept a universal bot reference or cast to a provider interface. Absence of an optional capability SHALL produce a typed unsupported-capability result before provider I/O.

#### Scenario: Telegram command catalog is published
- **WHEN** command reconciliation targets a Telegram installation that publishes the command-catalog capability
- **THEN** the process invokes that capability without resolving or casting a `TGBot`

#### Scenario: Command catalog targets OneBot
- **WHEN** command reconciliation requests Telegram command publication from an OneBot installation
- **THEN** it receives an unsupported-capability result and no provider request is sent

### Requirement: Legacy bot inheritance infrastructure is removed
The completed runtime and installed SDK SHALL NOT define or depend on the oversized `IBot`, provider bot interfaces, concrete `QQBot`/`TGBot` classes, or `BotRegistry`. Production component, dispatcher, command, and ingress code MUST contain no live-bot `dynamic_cast` or `dynamic_pointer_cast` path.

#### Scenario: Architecture boundary is checked
- **WHEN** architecture tests scan and compile production runtime and installed actor interfaces
- **THEN** no removed bot interface, concrete bot, live-bot registry, or provider capability cast is reachable

#### Scenario: Actor package sends a bot operation
- **WHEN** an installed actor package needs a supported QQ or Telegram action
- **THEN** it compiles against `BotOperationClient` and data-only operation contracts without including process component headers

### Requirement: Installation recipes are covered by executable conformance tests
Every supported recipe SHALL have executable tests for assembly, capability publication, ingress, all advertised operations, lifecycle rollback, repeated stop, and executor-safe destruction. Tests MUST use placeholders and fakes and MUST NOT require production credentials or external provider access.

#### Scenario: Recipe conformance runs
- **WHEN** the OneBot WebSocket, OneBot HTTP, and Telegram HTTP recipe suites execute
- **THEN** each recipe publishes only its declared capabilities and completes startup, operation, ingress, and shutdown tests
