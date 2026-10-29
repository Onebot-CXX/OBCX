## ADDED Requirements

### Requirement: Domain Bot SDK has an explicit contract compatibility gate
The domain Bot SDK SHALL advance the actor input contract schema from the modular-SDK baseline 2 to 3 while retaining numeric scheduler ABI generation V2 and ActorTask dispatch. New core loaders SHALL reject preceding or unknown schema versions before actor factory execution, preparation or registration, and generated schema 3 artifacts SHALL be rejected by preceding loaders. SDK identity/version metadata and installed package requirements SHALL identify the same compatibility boundary. JSON format stability, a V2 ABI label or a matching actor name MUST NOT be treated as proof of compatible C++ layouts or service types.

#### Scenario: Old SDK actor is offered to the new core
- **WHEN** a library has numeric V2 ABI but the preceding input-contract schema
- **THEN** loading fails before its actor factory or preparation hook runs and the diagnostic requests a matching SDK rebuild

#### Scenario: New actor is offered to an old core
- **WHEN** a schema 3 actor is loaded by a core that only accepts the preceding schema
- **THEN** the unsupported contract is rejected rather than interpreting new reference or SelfInfo layouts through old services

#### Scenario: Matching standalone actor loads
- **WHEN** a standalone actor is built against the matching installed domain SDK and exports a valid schema 3 contract
- **THEN** the loader preserves existing reflected inputs, commands, configuration constraints, preparation and availability behavior under the new gate

### Requirement: Domain SDK rebuild and rollback preserve runtime ownership
The release SHALL provide a matching core, installed SDK and rebuilt actor set, including actors that do not directly send bot messages. Actor builds SHALL use a frozen installed SDK with scripts/actor_build.py and MUST NOT build actor targets in the live core workspace. Core replacement/restart SHALL remain a separately authorized operation; actor-only reload MUST NOT be used to activate incompatible core/service changes. Deployment and rollback documentation SHALL preserve legacy persistent identity/deduplication evidence and identify any separately authorized data migration.

#### Scenario: Operator requests an actor-only reload across SDK generations
- **WHEN** the requested actor requires the new domain SDK but the live core provides the old contract
- **THEN** validation rejects the reload and leaves the active generation unchanged

#### Scenario: Actor is rebuilt for the new release
- **WHEN** the release build compiles a standalone actor
- **THEN** it uses the frozen matching installed SDK in an isolated actor workspace and does not replace the running core library through transitive rebuilds

#### Scenario: Release is rolled back
- **WHEN** an operator restores the prior deployment
- **THEN** matching core/SDK/actor/config artifacts and any explicitly required data backup are restored together without deleting sessions or replaying uncertain sends

### Requirement: Installed common SDK does not require provider contracts
The installed domain SDK SHALL make common references, messages, events, BotClient and self/member/media query contracts usable without including or linking either production platform SDK as a public dependency. OneBot-only and Telegram-only extension SDKs SHALL depend on the common contract without importing the other platform. Internal gateway serialization SHALL not require ordinary actor authors to construct JSON action envelopes.

#### Scenario: Portable actor uses only common operations
- **WHEN** an actor includes the common domain client and event headers
- **THEN** it can express send/delete/get_self and consume their typed results without QQ/TG DTO headers or native field construction

#### Scenario: One platform extension is selected
- **WHEN** an actor needs only an explicit OneBot extension in addition to the common API
- **THEN** its installed header dependency closure does not require Telegram contracts
