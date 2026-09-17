## 1. Freeze the migration boundary

- [x] 1.1 Add a production source inventory test covering every `IBot`, `IQQBot`, `ITelegramBot`, `ITelegramMediaGroupUploader`, `BotRegistry`, concrete bot, connection-factory, and live-bot RTTI use that must disappear
- [x] 1.2 Add baseline conformance tests for OneBot WebSocket, OneBot HTTP, and Telegram HTTP startup, ingress, command publication, and shutdown behavior
- [x] 1.3 Freeze the existing 13-action supported sets and provider success/error/submission-safety behavior before moving endpoint implementations
- [x] 1.4 Add installed-SDK fixtures proving in-repository actors use only `BotOperationClient` and data-only operation contracts
- [x] 1.5 Inventory tracked bot TOML files and add a credential/legacy-schema conformance test that can run without printing secret values

## 2. Introduce typed installation configuration

- [x] 2.1 Define exact bot surface and transport enums plus typed OneBot WebSocket, OneBot HTTP, and Telegram HTTP connection configuration structs
- [x] 2.2 Replace `BotConfig` raw connection storage with `BotInstallationConfig` and its closed connection `std::variant`
- [x] 2.3 Implement strict bot-table key validation, exact surface parsing, supported surface/transport matrix validation, and path-specific diagnostics
- [x] 2.4 Implement variant-specific connection key validation, `_ms` duration parsing and bounds, required credential checks, TLS rules, proxy rules, and documented defaults
- [x] 2.5 Reject legacy `type`, `plugins`, `timeout`, misspelled, ignored, and provider-misplaced keys with actionable migration diagnostics
- [x] 2.6 Add secret-safe diagnostics, logs, snapshots, and process fingerprints for typed installation configuration
- [x] 2.7 Add configuration tests for every valid variant, every invalid combination, unknown keys, redaction, defaults, and validation-only side-effect freedom

## 3. Build the component and capability runtime

- [x] 3.1 Define stable process-local component and capability identifiers without exposing the registry through the actor SDK
- [x] 3.2 Implement the installation-scoped typed capability registry with duplicate-provider rejection and checked type/id matching
- [x] 3.3 Define the component metadata and prepare/start/stop lifecycle contract with explicit provided and required capability ids
- [x] 3.4 Implement missing-dependency and dependency-cycle diagnostics plus deterministic topological lifecycle ordering
- [x] 3.5 Implement `BotInstallation` ownership of identity, surface, executor, components, capabilities, lifecycle state, and admission state without provider inheritance
- [x] 3.6 Implement transactional prepare/start rollback, reverse-order idempotent stop, bounded drain/cancellation, and executor-destruction-last behavior
- [x] 3.7 Add fake-component tests for duplicate capabilities, missing dependencies, cycles, deterministic order, partial-start rollback, repeated stop, concurrent stop, and destructor safety
- [x] 3.8 Implement descriptor-only recipe assembly used by validation-only without sockets, threads, provider calls, or persistent state

## 4. Define reviewed installation recipes

- [x] 4.1 Implement `BotInstallationAssembler` selection from the typed configuration variant with no string fallback or arbitrary user-authored component graph
- [x] 4.2 Define the reviewed OneBot WebSocket recipe and its protocol, transport, ingress, and operation capability dependencies
- [x] 4.3 Define the reviewed OneBot HTTP recipe and its protocol, transport, ingress, and operation capability dependencies
- [x] 4.4 Define the reviewed Telegram HTTP recipe and its protocol, transport, ingress, operation, media-upload, and command-catalog capability dependencies
- [x] 4.5 Add recipe descriptor tests proving exact component sets, deterministic order, exact surfaces, optional capabilities, and rejection of Telegram WebSocket and unsupported surfaces

## 5. Migrate OneBot 11 runtime behavior into components

- [x] 5.1 Extract OneBot protocol serialization/parsing behind a concrete process-local protocol capability without `BaseProtocolAdapter` downcasts
- [x] 5.2 Convert OneBot WebSocket connection ownership, action correlation, timeout, reconnect, and close behavior into the WebSocket transport component
- [x] 5.3 Convert OneBot HTTP polling and action submission behavior into the HTTP transport component
- [x] 5.4 Implement OneBot event ingress publication through the installation event capability with exact installation id and surface
- [x] 5.5 Move the supported OneBot group send/delete/member/forward/file/poke operations from `QQBot` and legacy wrappers into a native OneBot operation endpoint component
- [x] 5.6 Preserve OneBot provider envelope parsing, scoped result values, transport-phase classification, and conservative side-effect safety
- [x] 5.7 Add OneBot recipe tests for both transports, ingress identity, every advertised action, reconnect/poll cancellation, failure classification, and shutdown

## 6. Migrate Telegram runtime behavior into components

- [x] 6.1 Extract Telegram protocol serialization/parsing behind a concrete process-local protocol capability without `BaseProtocolAdapter` downcasts
- [x] 6.2 Convert Telegram HTTP polling, checkpoint/offset ownership, timeout, retry, force-close, proxy, TLS, and shutdown behavior into the HTTP transport component
- [x] 6.3 Implement Telegram event ingress publication through the installation event capability with exact installation id and surface
- [x] 6.4 Move Telegram group/topic send, delete, edit, photo, media-group URL, and bounded file-fetch operations from `TGBot` and legacy wrappers into a native Telegram operation endpoint component
- [x] 6.5 Implement multipart media-group upload as an optional installed capability and advertise its action only when present
- [x] 6.6 Move Telegram command-catalog publication into an explicit process-local capability
- [x] 6.7 Preserve Telegram provider envelope parsing, scoped chat/message results, authenticated URL confinement, byte bounds, and conservative side-effect safety
- [x] 6.8 Add Telegram recipe tests for polling ingress, topics, media, optional upload, command catalogs, proxy/TLS errors, timeout cancellation, and shutdown

## 7. Cut operation dispatch over to native capabilities

- [x] 7.1 Generalize/rename the QQ-Telegram dispatcher as needed so endpoint registration consumes native installation endpoint capabilities rather than live bot objects
- [x] 7.2 Register composed endpoint capabilities by exact installation id and surface with duplicate and wrong-surface rejection
- [x] 7.3 Preserve actor-facing `BotOperationClient`, all existing request/result DTOs, exact target validation, redaction, and the closed action matrix
- [x] 7.4 Replace optional-interface action inference with explicit same-installation capability checks
- [x] 7.5 Replace inherited fake bots in dispatcher tests with fake endpoint/protocol/transport components
- [x] 7.6 Add architecture gates proving dispatcher and endpoint production code has no live-bot registry lookup, provider bot cast, or removed bot-header dependency

## 8. Migrate process startup, ingress, commands, and reload

- [x] 8.1 Replace `ComponentManager` bot creation/setup in `main` with typed descriptor validation and `BotInstallationAssembler`
- [x] 8.2 Register actor message and notice ingress subscriptions against each installation event capability before starting its transport
- [x] 8.3 Replace per-`IBot` run/stop threads and ownership with installation lifecycle/run ownership and coordinated failure reporting
- [x] 8.4 Replace process command-catalog `IBot`/`ITelegramBot` casts with exact installation command-catalog capability lookup
- [x] 8.5 Replace runtime-generation `BotRegistry` availability checks with a data-only installation capability directory or dispatcher support query
- [x] 8.6 Reuse the same process dispatcher and active installations across actor generations without recreating components during reload
- [x] 8.7 Reject reload candidates that change process-owned installation surface, transport, credentials, or recipe while preserving the active runtime
- [x] 8.8 Add startup, validation-only, reload, in-flight operation, partial installation failure, and coordinated shutdown integration tests

## 9. Remove the legacy inheritance architecture

- [x] 9.1 Migrate every remaining root and local-actor production consumer away from `IBot`, provider bot interfaces, concrete bots, and `BotRegistry`
- [x] 9.2 Delete `QQBot`, `TGBot`, their headers/implementations, and Telegram's unsupported OneBot-shaped stub methods
- [x] 9.3 Delete `IBot`, `IQQBot`, `ITelegramBot`, `ITelegramMediaGroupUploader`, and inherited fake/test implementations
- [x] 9.4 Delete `BotRegistry`, `RegisteredBot`, live-bot operation wrapper construction, and all provider-interface RTTI paths
- [x] 9.5 Delete or replace the misleading singleton `ComponentManager`, `ConnectionManagerFactory`, unsupported `TelegramWebsocket`, and unknown-type fallback behavior
- [x] 9.6 Remove obsolete universal protocol/connection base interfaces where they only exist to support bot-type downcasting, retaining only narrow component contracts with real substitution tests
- [x] 9.7 Update CMake install/export lists and standalone SDK fixtures so removed compatibility headers are no longer shipped or linked
- [x] 9.8 Strengthen the source inventory gate to fail on any reintroduction of god-bot inheritance, live-bot casts, registry access, or provider objects in actors

## 10. Migrate configuration and documentation

- [x] 10.1 Convert every tracked root, development, test, packaging, and local-actor bot configuration to exact `surface`, explicit `transport`, `_ms` duration keys, and placeholder credentials
- [x] 10.2 Remove tracked production-like bot tokens and access tokens, document that exposed credentials require operator-side rotation, and verify tests/logs never echo them
- [x] 10.3 Document the component/capability/installation model, reviewed recipes, lifecycle ordering, actor boundary, and supported action matrix
- [x] 10.4 Add an explicit legacy-to-canonical bot configuration migration table and binary/config rollback procedure
- [x] 10.5 Update architecture docs that still instruct actors to resolve `IQQBot` or `ITelegramBot` so `BotOperationClient` is the sole supported actor bot API
- [x] 10.6 Update changelog and breaking-change guidance for removal of legacy bot interfaces and strict configuration parsing

## 11. Validate the completed cutover

- [x] 11.1 Run the full root build and test suite, including all recipe, operation, ingress, command, validation-only, reload, and shutdown tests
- [x] 11.2 Run installed-SDK and every repository under `local_actor/` from clean builds and verify no removed compatibility dependency remains
- [x] 11.3 Run ASan/TSan or the repository's equivalent concurrency/lifetime suites for component startup rollback, in-flight shutdown, reconnect, polling, and executor destruction
- [x] 11.4 Run clang-tidy on the changed core component/configuration code and resolve actionable analyzer, lifetime, cast, optional-access, and special-member diagnostics
- [x] 11.5 Run `nix fmt` from the repository root and verify the root plus every `local_actor/` repository is formatted
- [x] 11.6 Run strict OpenSpec validation and verify all proposal, design, specification, architecture, source-inventory, and credential checks pass
