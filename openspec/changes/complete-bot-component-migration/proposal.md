## Why

OBCX completed the actor-facing bot-operation boundary but deliberately retained the legacy `QQBot`/`TGBot` inheritance hierarchy, oversized `IBot`, provider-interface RTTI, and stringly typed bot construction as compatibility infrastructure. This leaves the runtime in a transitional state that contradicts the intended Koishi-style model in which an installation is assembled from independently owned components that publish explicit capabilities.

## What Changes

- Introduce a process-owned bot installation runtime that composes lifecycle, transport, protocol, ingress, command-catalog, and operation capability components instead of deriving one concrete bot class from platform interfaces.
- Add explicit component registration, capability publication and lookup, dependency validation, deterministic startup, reverse-order shutdown, and installation-scoped ownership.
- Replace `QQBot : IBot, IQQBot` and `TGBot : IBot, ITelegramBot, ITelegramMediaGroupUploader` with OneBot 11 and Telegram installation assemblies built from components.
- Route the existing `BotOperationClient` dispatcher through installed operation capabilities rather than wrapping live bots and using `dynamic_cast` against provider interfaces.
- Move process-only command-catalog publication and event ingress onto explicit components so no process path requires provider capability inheritance.
- Replace raw `toml::table` bot connection handling and string/enum fallback chains with validated typed installation/component configuration, including exact supported platform/transport combinations.
- Reject unknown bot types, unknown components, duplicate capabilities, missing component dependencies, and unsupported transports before any network activity; remove the advertised-but-unimplemented Telegram WebSocket path and the fallback to OneBot HTTP.
- Preserve exact installation routing, typed operation requests/results, current OneBot 11 and Telegram behavior, actor ingress metadata, reload behavior, and shutdown safety.
- **BREAKING**: Remove the oversized legacy `IBot` operation surface and the actor/runtime-visible `IQQBot`, `ITelegramBot`, and `ITelegramMediaGroupUploader` compatibility interfaces after all in-repository consumers and tests migrate.
- **BREAKING**: Bot configuration becomes schema-validated and no longer accepts ignored legacy keys or invalid platform/transport combinations.

## Capabilities

### New Capabilities

- `bot-component-runtime`: Installation-scoped component composition, capability publication, dependency validation, lifecycle ordering, and removal of provider capability inheritance.
- `bot-installation-configuration`: Typed, fail-closed configuration and assembly of supported OneBot 11/QQ and Telegram Bot API installations.

### Modified Capabilities

- `bot-operation-dispatch`: Resolve and invoke operation capabilities from composed bot installations without live-bot interfaces, RTTI, or provider-interface wrappers while preserving exact-installation dispatch semantics.

## Impact

- Core runtime ownership and startup wiring in `src/app`, `src/common`, `src/core`, `src/interfaces`, protocol adapters, and connection managers.
- Public and installed SDK headers that currently expose `IBot`, `IQQBot`, and `ITelegramBot`; external code using those compatibility interfaces must migrate to `BotOperationClient` or explicit process component contracts.
- Root bot configuration parsing, validation-only behavior, examples, documentation, runtime reload checks, and command-catalog wiring.
- Existing OneBot 11 and Telegram transports remain the supported provider implementations; actor-facing bot operation DTOs and the closed action matrix remain compatible.
- Tests must move from fake inherited bots to fake components/capabilities and add composition, lifecycle, invalid-assembly, and no-RTTI architecture coverage.
