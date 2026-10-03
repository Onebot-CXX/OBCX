# bot-installation-configuration Specification

## Purpose
Define exact installation configuration, typed connection validation, and secret-safe side-effect-free preparation.

## Requirements
### Requirement: Bot configuration identifies exact installations and surfaces
Each `bots.<installation_id>` table SHALL define `enabled`, an exact `surface`, and a `transport`. The table key SHALL be the installation id used by ingress and `BotOperationClient`; supported surfaces in this change are exactly `onebot11.qq` and `telegram.bot_api`.

#### Scenario: Valid OneBot installation is parsed
- **WHEN** configuration declares an enabled installation with surface `onebot11.qq` and transport `websocket` or `http`
- **THEN** parsing produces a typed OneBot installation configuration with the table key as its installation id

#### Scenario: Valid Telegram installation is parsed
- **WHEN** configuration declares an enabled installation with surface `telegram.bot_api` and transport `http`
- **THEN** parsing produces a typed Telegram HTTP installation configuration with the table key as its installation id

#### Scenario: Unsupported surface is configured
- **WHEN** a bot declares `qq.official`, an unknown surface, or a platform alias without an implemented provider
- **THEN** validation rejects the exact configuration path before any installation or network resource is constructed

### Requirement: Configuration produces a closed typed connection variant
After parsing, runtime code SHALL consume a typed `BotInstallationConfig` containing one of the supported OneBot WebSocket, OneBot HTTP, or Telegram HTTP connection variants. Runtime assembly, components, and transports MUST NOT read a raw `toml::table`, reinterpret a bot type string, or apply a default provider/transport fallback.

#### Scenario: OneBot WebSocket configuration reaches assembly
- **WHEN** a valid OneBot WebSocket table is parsed
- **THEN** the assembler receives the OneBot WebSocket variant and cannot accidentally select OneBot HTTP or Telegram HTTP

#### Scenario: Unknown transport is configured
- **WHEN** transport is missing or not valid for the declared surface
- **THEN** validation fails instead of returning a default connection enum

### Requirement: Only implemented surface and transport combinations are accepted
The supported matrix SHALL be `onebot11.qq` with `websocket`, `onebot11.qq` with `http`, and `telegram.bot_api` with `http`. Telegram WebSocket and every other combination MUST be rejected during parsing and MUST NOT have an enum value, fallback branch, or runtime delayed failure.

#### Scenario: Telegram WebSocket is configured
- **WHEN** a Telegram installation selects `websocket`
- **THEN** validation reports that the surface/transport combination is unsupported before provider I/O

#### Scenario: OneBot HTTP is configured
- **WHEN** an OneBot installation selects `http` with valid connection fields
- **THEN** the reviewed OneBot HTTP component recipe is selected

### Requirement: Connection keys and units are explicit and validated
The parser SHALL maintain a closed key set per connection variant, require every mandatory option explicitly, use `_ms` suffixes for millisecond durations, and validate ports, positive and bounded timeouts, TLS/proxy combinations, required credentials, and provider-specific polling fields. Unknown, misspelled, ignored, misplaced, or missing mandatory keys MUST cause a path-specific secret-safe diagnostic. Disabled installations SHALL receive the same validation. The optional proxy group MAY be absent as a whole; when supplied, every required proxy field MUST be explicit. No connection-option defaults SHALL be supplied.

#### Scenario: Legacy timeout key is present
- **WHEN** a connection contains `timeout` rather than a supported explicit `_ms` field
- **THEN** validation rejects the key and identifies its full configuration path

#### Scenario: Provider-specific field is misplaced
- **WHEN** an OneBot WebSocket connection contains a Telegram polling field
- **THEN** validation rejects that field instead of ignoring it

#### Scenario: Mandatory field is omitted
- **WHEN** a connection omits a mandatory option such as `action_timeout_ms`, or a configured proxy omits `proxy_username` or `proxy_password`
- **THEN** parsing rejects the exact missing path instead of supplying a default, including for disabled installations

#### Scenario: Proxy group is absent
- **WHEN** a valid Telegram connection supplies no proxy fields
- **THEN** parsing represents no proxy rather than inventing proxy option values

### Requirement: Legacy bot schema is a hard migration error
The canonical parser SHALL reject legacy `type = "qq"`, `type = "telegram"`, bot-level `plugins`, legacy `timeout`, and other keys that were previously ignored or ambiguously mapped. Diagnostics SHALL identify the replacement surface, transport, or explicit field where one exists.

#### Scenario: Legacy bot type is validated
- **WHEN** configuration uses `type = "qq"`
- **THEN** validation fails with guidance to use surface `onebot11.qq` and an explicit transport

#### Scenario: Ignored plugin list remains in bot table
- **WHEN** configuration contains the removed bot-level `plugins` key
- **THEN** validation fails instead of claiming the unused value was accepted

### Requirement: Credentials and secrets remain confidential
Access tokens, secrets, proxy passwords, tokenized URLs, and other credential-bearing values SHALL be available only to the owning process components. Configuration diagnostics, logs, fingerprints, operator summaries, and test snapshots MUST redact or omit their values.

#### Scenario: Configuration containing a token fails validation
- **WHEN** another field in the same installation is invalid
- **THEN** the diagnostic identifies the invalid path without printing the token or secret

#### Scenario: Installation starts successfully
- **WHEN** components log their selected surface, transport, endpoint, and proxy mode
- **THEN** logs contain no credential-bearing value or tokenized provider URL

### Requirement: Validation-only and reload are side-effect free and component aware
Validation-only SHALL parse typed configurations, select recipes, and validate component dependencies and advertised operation surfaces without opening sockets, starting threads, publishing command catalogs, or calling providers. Reload candidates SHALL validate configured installation identity and required capabilities against the unchanged process-owned active installations and MUST NOT recreate them.

#### Scenario: Validation-only checks a valid configuration
- **WHEN** the process runs configuration validation
- **THEN** all installation recipes and actor installation references are checked with no network or persistent side effect

#### Scenario: Reload changes an active installation recipe
- **WHEN** a candidate reload changes surface, transport, or connection ownership for an already running installation
- **THEN** reload rejects the unsupported process-level change and leaves the active installation unchanged

#### Scenario: Reload references a missing capability
- **WHEN** an actor candidate requires an operation not advertised by its configured active installation
- **THEN** candidate validation fails before activation

### Requirement: Repository configuration and migration documentation are complete
All root-owned tracked examples, development fixtures, packaging smoke configurations, and tests SHALL use the canonical schema with placeholder credentials. Documentation SHALL provide an old-to-new key mapping and SHALL state that binary rollback requires restoring the prior configuration format. Root validation MUST NOT inspect configuration files owned by independent `local_actor/` repositories.

#### Scenario: Root configuration examples are checked
- **WHEN** validation scans tracked TOML files owned by the root repository
- **THEN** no legacy bot schema, ignored bot key, or credential-shaped production value remains
