## Context

The actor-facing migration introduced a closed, typed `BotOperationClient` and exact-installation dispatcher, but the process side still constructs `QQBot` and `TGBot` objects that inherit a large `IBot` interface plus provider-specific interfaces. `ComponentManager` is currently a string-based bot factory/config mapper rather than a component system. Operation endpoints recover provider capabilities with RTTI, command-catalog publication still receives a live bot, and `BotRegistry` retains `shared_ptr<IBot>` objects.

The legacy hierarchy is OneBot-shaped: Telegram must implement unsupported contacts, moderation, cookie, CSRF, and request methods as stubs. It also represents the same installation through configuration strings, connection enums, concrete bot classes, interface casts, `BotSurface`, and operation wrappers. Invalid combinations are not consistently rejected: Telegram WebSocket is represented but not implemented, and unknown connection types can fall back to OneBot HTTP.

The target follows the previously discussed Koishi-style principle: a bot installation is a context and lifecycle boundary assembled from components; capabilities are installed values, not base classes on a platform god object. Actor packages continue to see only the data-only `BotOperationClient`. Component and provider objects remain process-local and do not become a cross-DSO actor ABI.

## Goals / Non-Goals

**Goals:**

- Make `BotInstallation` the process-owned composition root for one configured installation id.
- Assemble OneBot 11/QQ and Telegram Bot API installations from explicit protocol, transport, ingress, command-catalog, and operation components.
- Publish capabilities through an installation-scoped registry with validated uniqueness and dependencies.
- Preserve the current closed actor-facing operation matrix and exact-installation semantics.
- Remove `QQBot`, `TGBot`, the oversized `IBot`, provider capability inheritance, live-bot RTTI, `BotRegistry`, and misleading factory-style `ComponentManager` from the final architecture.
- Parse bot configuration into a closed typed variant and fail before network activity on unknown keys or unsupported combinations.
- Preserve event conversion, command publication, provider response parsing, timeout/proxy behavior, reload generation reuse, and safe shutdown.
- Provide architecture and conformance tests that prevent reintroducing inherited bot capabilities or opaque provider access in actors.

**Non-Goals:**

- Expanding the 13-action QQ/Telegram operation matrix.
- Supporting official QQ, Discord, WeChat, Matrix, or another provider.
- Implementing Telegram WebSocket, a new network stack, outbox, rate governor, blob gateway, or provider probe service.
- Replacing current raw message/notice ingress DTOs or the existing `common::Message` segment model.
- Making runtime components dynamically loadable plugins or exposing the component registry to actor packages.
- Preserving source compatibility for external code that directly subclasses or calls legacy bot interfaces.

## Decisions

### 1. Separate installation, component, and capability

The runtime will use three distinct concepts:

```text
BotInstallation
  owns io_context, identity, lifecycle, and CapabilityRegistry
       |
       +-- Protocol component
       +-- Transport component
       +-- Event ingress component
       +-- Operation component
       +-- optional command-catalog/media component

Component
  declares id + required capability ids
  installs one or more process-local capability objects
  participates in prepare/start/stop lifecycle

Capability
  is a narrow callable process-local contract or value
  is registered under one stable capability id
  is consumed by components, never inherited by BotInstallation
```

`BotInstallation` will not derive from provider, transport, or action interfaces. A component may internally implement a narrow process-local contract, but no class representing “the bot” accumulates multiple capability bases. This preserves normal C++ substitutability inside one component without recreating the legacy god object.

The capability registry will use stable internal capability identifiers plus checked typed access. It will not rely solely on `std::type_index` across DSOs, and it will not be installed as an actor service. Registration rejects duplicate providers for a single-valued capability. If a future capability needs multiplicity, that requires a separately defined collection contract rather than silent last-writer wins.

**Rejected alternatives:** retaining `QQBot`/`TGBot` as facades would preserve the inheritance seam; a service locator exposed to actors would create an unstable C++ ABI; one component per individual method would produce excessive lifecycle and dependency granularity.

### 2. Use closed installation recipes over arbitrary user-authored component graphs

Configuration selects an exact surface and supported transport. A process `BotInstallationAssembler` maps that typed variant to a reviewed component recipe:

```text
onebot11.qq + websocket
  -> OneBot11Protocol
  -> OneBot11WebSocketTransport
  -> OneBot11EventIngress
  -> OneBot11Operations

onebot11.qq + http
  -> OneBot11Protocol
  -> OneBot11HttpTransport
  -> OneBot11EventIngress
  -> OneBot11Operations

telegram.bot_api + http
  -> TelegramProtocol
  -> TelegramHttpTransport
  -> TelegramEventIngress
  -> TelegramOperations
  -> TelegramCommandCatalog
```

Users do not supply arbitrary component class names or dependency graphs. Components remain the implementation and extension unit, while recipes guarantee that production assemblies are testable. Optional capabilities, such as Telegram multipart upload, are explicitly installed by the recipe and therefore appear in supported actions only when present.

**Rejected alternatives:** exposing `components = [...]` in TOML would permit untested combinations and duplicate dependencies; preserving `type = "qq"` as the canonical surface would continue conflating QQ as a platform with OneBot 11 as a provider protocol.

### 3. Adopt one typed and fail-closed bot configuration model

The canonical configuration will use the table key as installation id and exact surface/transport identifiers:

```toml
[bots.qq_bot]
enabled = true
surface = "onebot11.qq"
transport = "websocket"

[bots.qq_bot.connection]
host = "127.0.0.1"
port = 3001
access_token = "..."
connect_timeout_ms = 5000
action_timeout_ms = 30000

[bots.telegram_bot]
enabled = true
surface = "telegram.bot_api"
transport = "http"

[bots.telegram_bot.connection]
host = "api.telegram.org"
port = 443
use_tls = true
poll_timeout_ms = 25000
```

Parsing produces `BotInstallationConfig` containing a `std::variant` of the three supported connection configurations. Surface, transport, and all connection keys are validated once; runtime code does not read `toml::table`. Durations use `_ms` suffixes. Unknown keys, legacy `plugins`, legacy `timeout`, `type = "qq"`, `type = "telegram"`, and invalid transport/surface combinations fail with a path-specific diagnostic. Repository examples and development configuration are migrated atomically with the parser.

Sensitive values participate in runtime setup but never appear in diagnostics, logs, fingerprints, or validation output. Validation-only parses and assembles descriptors without constructing sockets, starting threads, or calling providers.

**Rejected alternatives:** accepting old and new schemas indefinitely would retain ambiguity; silently ignoring unknown keys repeats the current failure mode; deriving surface from transport makes future provider distinctions unsafe.

### 4. Model component lifecycle as a validated DAG with transactional startup

Each component declares required capability ids. Assembly performs all registration and dependency checks before startup, detects missing dependencies and cycles, and computes a deterministic topological order using recipe order as the stable tie-breaker.

Lifecycle phases are:

1. construct components without provider I/O;
2. install capabilities;
3. validate uniqueness, dependencies, and operation declarations;
4. prepare callbacks and event subscriptions;
5. start components in topological order;
6. run the installation event loop;
7. stop admission and components in reverse order;
8. drain/cancel pending work and destroy components before the installation `io_context`.

If prepare or start fails, already prepared/started components roll back in reverse order. Stop is idempotent; destructor paths catch and report failures without throwing. A transport cannot emit ingress before subscriptions and operation capabilities are installed.

**Rejected alternatives:** source-order startup without dependency validation is fragile; allowing components to spawn unmanaged threads would recreate current shutdown hazards; global component ownership would break installation isolation.

### 5. Make typed operation endpoints native capabilities

OneBot and Telegram operation components will implement `BotOperationEndpoint` directly and consume concrete protocol/transport capabilities. The process dispatcher registers these endpoint capabilities by installation id and exact surface. It no longer receives `shared_ptr<IBot>`, wraps a live bot, or performs `dynamic_pointer_cast` to provider interfaces.

Provider JSON parsing remains process-side. Existing serialization and transport code may be reused internally, but operations return the existing typed values and conservative submission safety. The dispatcher remains shared across actor generations and continues to be the only actor-visible bot service.

Supported actions come from the installed operation component and optional capability presence, not from inherited methods. Architecture tests will reject dependencies from dispatcher/endpoint production code to removed bot interfaces or concrete bot classes.

**Rejected alternatives:** retaining wrappers over legacy bots would not complete the migration; moving provider parsing into actors would reverse the actor-safe boundary; widening `BotOperationClient` is outside this change.

### 6. Move ingress and command catalog to explicit capabilities

An installation event capability provides typed subscriptions for `MessageEvent`, `NoticeEvent`, and other process-consumed events. Main registers actor ingress callbacks against that capability before transport startup. Event callbacks carry the installation id and exact surface from the owning installation; callers do not infer platform through RTTI.

Telegram command publication becomes a process-local command-catalog capability installed only by the Telegram recipe. `CommandPlatformAdapter` receives that capability or an installation capability lookup, not `IBot&`, and fails explicitly when the configured installation does not publish it.

This keeps ingress and command publication compositional while preserving the current actor-facing command and raw event contracts.

### 7. Remove compatibility infrastructure at the end of the cutover

After all root and local-actor production consumers use `BotOperationClient` and explicit ingress data, the implementation will remove:

- `IBot`, `IQQBot`, `ITelegramBot`, and `ITelegramMediaGroupUploader`;
- `QQBot` and `TGBot`;
- `BotRegistry` and `RegisteredBot`;
- live-bot operation wrappers and RTTI casts;
- `ComponentManager` and `ConnectionManagerFactory` branches that select by bot type;
- unsupported `TelegramWebsocket` and fallback-to-OneBot behavior;
- Telegram legacy stubs for unsupported OneBot-shaped methods.

Connection/protocol abstractions may remain as narrow component contracts where they represent real substitutable behavior, but they cannot be reachable through a universal bot base class.

This is a hard cutover. Installed-SDK compile tests will prove actor packages use operation contracts rather than compatibility bot headers.

## Risks / Trade-offs

- **[Large cross-cutting cutover can regress provider behavior]** → Freeze current transport, ingress, command, and 13-action conformance tests before moving code; migrate one recipe at a time behind the same typed endpoint contract.
- **[Lifecycle reordering can expose races or dangling coroutine captures]** → Specify transactional startup and reverse shutdown, add failure-injection tests at every phase, and preserve `io_context` destruction-last ownership.
- **[Strict configuration breaks existing deployments]** → Provide path-specific validation errors, update all repository configs/examples, document the old-to-new key mapping, and require operators to validate before deployment.
- **[External actors may still include legacy bot headers]** → Treat removal as an explicit SDK break, document `BotOperationClient` migration, and verify all known installed actor repositories before deleting headers.
- **[An overly generic registry can become another service locator]** → Keep it process-local, installation-scoped, closed to arbitrary actor access, and require stable declared capability ids and dependency tests.
- **[Component fragmentation can increase indirection]** → Use coarse behavior-focused components and reviewed recipes; do not make each provider method its own component.
- **[Current config files contain credential-shaped values]** → Tests and examples must use placeholders; production-like credentials must be rotated and removed from tracked configuration/history independently of runtime migration.

## Migration Plan

1. Add architecture/source inventory tests that freeze current behavior and enumerate every legacy interface, cast, registry use, config form, event path, and supported operation.
2. Introduce process-local installation/component/capability primitives and lifecycle failure-injection tests without changing provider startup.
3. Add the typed configuration parser, descriptor-only validation, migration diagnostics, and new repository configuration examples.
4. Implement OneBot WebSocket and HTTP component recipes using existing protocol/transport behavior; bind a native OneBot operation endpoint and event ingress.
5. Implement the Telegram HTTP recipe, operation endpoint, authenticated media capability, event ingress, and command-catalog capability.
6. Switch main startup, actor ingress, runtime reload validation, and the shared dispatcher to `BotInstallation` objects; run old and new conformance suites during this intermediate step.
7. Migrate command publication and remaining process consumers away from `IBot`, provider interfaces, `BotRegistry`, and RTTI.
8. Delete concrete legacy bots, god interfaces, wrappers, fake inherited bots, unsupported enum values, ignored config keys, and fallback factories; replace tests with fake components/capabilities.
9. Run formatting, root tests, sanitizers/concurrency suites where available, installed-SDK actor tests, validation-only tests, reload/shutdown tests, and strict OpenSpec validation.

Deployment requires the new binary and migrated bot configuration together. Rollback restores the previous binary and previous configuration; no database migration is introduced by this change.

## Open Questions

None. The component boundary is process-local, the supported recipes and actor action matrix are closed, and removal of the compatibility hierarchy is part of this change rather than deferred again.
