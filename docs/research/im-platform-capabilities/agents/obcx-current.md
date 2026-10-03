# OBCX Current IM Adapter Audit

## Lane metadata

- **Lane:** `obcx-current` / local implementation survey
- **Scope:** current QQ/OneBot 11 and Telegram bot abstractions, adapters, ingress, actor runtime boundary, registry, Bridge actor, lifecycle and migration implications
- **Inspection date:** 2026-08-17 UTC (brief date); repository working tree as locally available
- **Method:** read-only source inspection; no web claims and no repository modification other than this required audit artifact
- **Caveat:** `local_actor/obcx-actor-bridge/` is an embedded actor repository and may evolve independently. No runtime/API integration test was executed.

## Executive findings

1. `IBot` is not a small cross-platform contract: it combines lifecycle/event subscription with 30 OneBot-shaped operations, including friends, strangers, anonymous groups, honor, cookies and CSRF (`include/interfaces/bot.hpp:24-379`). Most return untyped JSON strings.
2. Telegram must implement that QQ-shaped interface and consequently has 14 explicit “not implemented” methods; most return `{}`, while status/version and can-send methods synthesize success (`src/core/tg_bot.cpp:286-504`). Unsupported behavior is therefore not discoverable and can look successful.
3. Some portable/common operations are real on both adapters (send private/group, delete, selected user/chat/member/moderation calls), but semantics leak: Telegram delete requires a compound `chat_id:message_id` string (`src/core/tg_bot.cpp:260-278`).
4. Platform capabilities already exist beside `IBot`: `IQQBot` has forward/file URL/poke methods (`include/interfaces/qq_bot.hpp:8-35`); Telegram has topic, photo, albums, edit, commands and download interfaces (`include/interfaces/telegram_bot.hpp:67-170`). Discovery is by `dynamic_cast`, not capability metadata.
5. The Bridge resolves live `IBot`s from process-owned `BotRegistry` using platform-only lookup, then handlers dynamically cast for Telegram/QQ features (`local_actor/obcx-actor-bridge/dependency/bridge_forwarding_runtime.cpp:36-112`). Multiple accounts make platform-only lookup deliberately fail (`include/core/bot_registry.hpp:71-95`).
6. Network transports and bot `io_context`s are process-owned and run on one thread per bot; actor generations are reloadable while bots and registry survive (`src/app/main.cpp:284-418`, `src/app/main.cpp:420-510`).
7. Ingress is adapter JSON -> `common::Event` -> per-bot `EventDispatcher` -> raw actor envelope. Only message and notice events are wired into actor runtime; request/meta/heartbeat/error remain dispatcher-only (`src/app/main.cpp:384-412`).
8. Canonical event DTOs remain OneBot-oriented (`post_type`, `message_type`, group/guild/channel optionals, message segments), while Telegram maps updates into those fields and retains raw Telegram message JSON (`include/common/message_type.hpp:44-178`; `src/telegram/adapter/protocol_adapter.cpp:51-173`).
9. Actor envelopes are serializable and carry source platform/account, conversation, correlation/causation, normalized payload and raw JSON (`include/core/actor/actor.hpp:81-110`; `src/core/runtime/message_event_ingress.cpp:109-174`), a good boundary for future typed capability requests/results.
10. Bridge egress currently crosses the actor boundary back into live C++ bot objects and awaits Asio via `ActorContext::await_asio`; it does not send serializable egress request messages (`local_actor/obcx-actor-bridge/actor/bridge_actor.cpp:129-206`).
11. `BotRegistry` stores weak bot references and returns a temporary strong reference; application `bots` owns live instances. This avoids registry ownership cycles but means operation lifetime and shutdown races require explicit admission/cancellation policy (`include/core/bot_registry.hpp:15-107`).
12. Component creation is closed over string types `qq` and `telegram`; Telegram websocket is selectable by config mapping but rejected by `TGBot::connect`, which accepts HTTP only (`src/common/component_manager.cpp:21-61`; `src/core/tg_bot.cpp:20-39`).

## Inspection action log

| Action | Result |
|---|---|
| Read research brief | Adopted evidence/register/gap/checklist structure, substituting local file evidence for web sources. |
| Inspected required bot interfaces and concrete bot headers/sources | Mapped full base surface, platform extensions, implementations and Telegram stubs. |
| Inspected OneBot/common event DTOs and QQ converter | Confirmed OneBot vocabulary and event variants. |
| Inspected Telegram protocol adapter and HTTP ingress | Confirmed recognized Telegram updates and mapping to common events. |
| Inspected `BotRegistry`, tests, component manager and app startup/shutdown | Confirmed multi-account ambiguity, weak ownership, process lifetime and transport constraints. |
| Inspected actor envelope/services/ingress and reload wiring | Confirmed serializable boundary and process service injection. |
| Inspected Bridge actor/config/runtime/handlers/tests | Confirmed service resolution, direct bot calls, dynamic casts, supported source platforms and test seam. |
| Searched for empty/unsupported implementations and Bridge bot API calls | Enumerated stubs and concrete current calls. |

## Source/file register

| ID | File and exact range | What it proves |
|---|---|---|
| F1 | `include/interfaces/bot.hpp:24-379` | Entire `IBot` contract and protected transport/runtime ownership. |
| F2 | `include/interfaces/qq_bot.hpp:8-35` | Stable QQ-specific extension. |
| F3 | `include/interfaces/telegram_bot.hpp:14-170` | Telegram DTOs and optional platform capability interfaces/default fallbacks. |
| F4 | `include/core/qq_bot.hpp:16-350` | QQ implements `IBot` + `IQQBot`; OneBot-centric full implementation declaration. |
| F5 | `include/core/tg_bot.hpp:17-421` | Telegram forced to implement base plus Telegram extensions. |
| F6 | `src/core/tg_bot.cpp:20-504` | HTTP-only connection, real methods, delete encoding, and unsupported/synthetic methods. |
| F7 | `src/core/qq_bot.cpp:10-178` | QQ connection callback and request/echo/response flow. |
| F8 | `include/common/message_type.hpp:44-178,180-219` | OneBot event hierarchy/variant and shared connection config. |
| F9 | `src/onebot11/adapter/event_converter.cpp:10-67` | OneBot post-type mapping. |
| F10 | `src/telegram/adapter/protocol_adapter.cpp:51-173` | Telegram recognized update types and message normalization/raw preservation. |
| F11 | `include/core/event_dispatcher.hpp:22-108` | Typed handlers, variant dispatch, detached coroutine scheduling on bot I/O executor. |
| F12 | `src/core/runtime/message_event_ingress.cpp:21-174` | Conversation identity and raw message/notice envelope construction. |
| F13 | `include/core/actor/actor.hpp:81-110,174-284` | Envelope/result/services/context and Asio/blocking crossings. |
| F14 | `include/core/bot_registry.hpp:15-107` | Account keys, weak ownership, ambiguity behavior. |
| F15 | `tests/cpp/bot_registry_test.cpp:9-58` | Multi-account, unambiguous, expired and unregister behavior tests. |
| F16 | `src/common/component_manager.cpp:21-61,68-170,173-190` | Bot factory, connection selection/config and private `connect` setup. |
| F17 | `src/interfaces/bot.cpp:7-57` | Per-bot I/O/dispatcher creation and strict destruction ordering. |
| F18 | `src/app/main.cpp:284-418,420-510` | Process registry, bot ownership/threads, ingress callbacks, reload and shutdown sequence. |
| F19 | `local_actor/actor-registry/actors/bridge.toml:1-20` | Bridge actor identity/dependency/publication metadata. |
| F20 | `local_actor/obcx-actor-bridge/actor/bridge_actor.cpp:104-250` | Runtime service resolution, Asio crossing, accepted message and failures. |
| F21 | `local_actor/obcx-actor-bridge/dependency/bridge_forwarding_runtime.cpp:13-112` | Platform routing, registry lookup, direct handler calls and mapping requirement. |
| F22 | `local_actor/obcx-actor-bridge/tests/bridge_actor_test.cpp:156-210` | Injected forwarder seam and emitted/persisted mapping behavior. |

## Findings with file evidence

### Current surface and coupling

`IBot` contains: subscription; `run/stop/error_notify/is_connected`; private/group send; delete/get message; friend/stranger/group/member queries; kick/ban/whole-ban/card/leave/name/admin/anonymous/portrait/honor; login/status/version; image/record/can-send; cookies/CSRF/credentials; friend/group request approval (F1). Names such as `set_group_anonymous_ban`, `get_group_honor_info`, `get_cookies`, `get_csrf_token`, `flag/sub_type/no_cache`, and universal JSON-string results are direct OneBot/QQ coupling.

The common message/event model repeats OneBot terminology (`post_type`, four `EventType`s, `MessageSegment {type,data}`), though it has guild/channel fields and raw `data` (F8). QQ requests serialize through adapter methods and correlate string responses via generated echo IDs (F7). Thus both API shape and result representation are transport-protocol-derived rather than capability/semantic-derived.

### Adapter asymmetry and unsupported behavior

Telegram implements meaningful send, topic, photo/media group, edit, commands, selected chat/user/member/moderation, login and download operations (F6), and exposes richer Telegram-specific interfaces (F3). Explicit no-op `{}` methods are:

`get_message`, `get_friend_list`, `get_group_list`, `set_group_card`, `set_group_anonymous_ban`, `set_group_anonymous`, `get_group_honor_info`, `get_cookies`, `get_csrf_token`, `get_credentials`, `set_friend_add_request`, `set_group_add_request` (`src/core/tg_bot.cpp:286-309,367-414,425-430,475-505`). `get_status` and `get_version_info` log “not implemented” but return fabricated success; `can_send_image/record` unconditionally return yes (`src/core/tg_bot.cpp:439-473`). This violates the brief guardrail that unsupported behavior be capability-discoverable.

Interface default methods also silently degrade: `ITelegramBot::set_commands` returns `{}` by default and entity-aware overloads discard entities before forwarding (`include/interfaces/telegram_bot.hpp:134-169`). `TGBot` overrides these today, but alternative implementations could falsely appear capable.

### Bridge acquisition/calls

`BridgeActor` gets `BotRegistry`, `DbManager`, and an Asio executor from `ActorContext`; it creates and caches `BridgeForwardingRuntime` (F20). Runtime accepts only `qq` and `telegram`, resolves both source and target using `find_bot(platform)`, converts `MessageStored` back to `MessageEvent`, invokes legacy handlers with `IBot&`, and requires a persisted target-message mapping (F21). This makes Bridge fail when either platform has multiple live accounts, despite envelopes carrying `source_bot`.

Handlers call base operations and dynamically cast for extensions. Current searched calls include:

- Base: `send_group_message`, `delete_message`, `get_group_member_info` (`local_actor/obcx-actor-bridge/dependency/{qq,telegram}/*.cpp`).
- Telegram extension: `send_topic_message`, `send_group_photo`, `send_media_group`, `get_media_download_url(s)` (`dependency/qq/handler.cpp:202-211`, `dependency/qq/media_processor/image.cpp:154-172`, `dependency/qq/message_formatter.cpp:260-560`, `dependency/telegram/media_processor/{dispatch,animation,sticker}.cpp`).
- QQ extension/concrete leak: `get_forward_msg` uses `dynamic_cast<obcx::core::QQBot&>` rather than `IQQBot` (`local_actor/obcx-actor-bridge/dependency/qq/message_formatter.cpp:120-130`).

### Ingress mapping

OneBot converts message/notice/request/meta and heartbeat JSON directly into the common variant (F9). Telegram recognizes message, edited message, channel post, edited channel post and callback query; unrecognized updates are dropped (`src/telegram/adapter/protocol_adapter.cpp:51-82`). Telegram message conversion sets placeholder `self_id = "0"`, normalizes chat type, sender/message IDs and segments, while preserving the raw message object (`src/telegram/adapter/protocol_adapter.cpp:85-173`).

Connection managers invoke the bot callback, bot dispatches the variant, and the app registers only `MessageEvent` and `NoticeEvent` handlers (F7, F11, F18). Envelope conversion determines Telegram conversation from raw `chat.id`, then guild/group/channel/private fallbacks; it emits `RawMessageEvent`/`RawNoticeEvent` payload plus complete raw JSON and source account metadata (F12). Request, callback semantics mapped as notices (where applicable), meta, heartbeat and errors do not have app actor-ingress registration.

### Ownership, reload and network lifecycle

Each bot owns a shared `io_context`, adapter, dispatcher and connection manager (F1/F17). Destruction must stop/drain I/O, destroy connection manager while context services remain alive, then destroy context (F17). The app strongly owns bots, registers weak references, starts each `run()` on a dedicated thread, and stops actors/blocking executor before bots during shutdown (F18). Reload replaces actor generations through the controller but retains live bots and the process registry.

Consequences: actors must not own/reconnect transports; egress needs process-owned executors/services. Detached dispatcher handlers have no returned completion and capture raw `IBot*` (`include/core/event_dispatcher.hpp:72-99`), so shutdown relies on ordering/draining rather than an explicit per-operation lease. Telegram configuration advertises websocket mapping, yet concrete connection rejects it (F6/F16).

## API call inventory

| Current API | Used by Bridge | Actual support | Result/semantic issue |
|---|---:|---|---|
| `send_private_message` | No searched call | QQ + TG | `awaitable<string>` opaque protocol JSON. |
| `send_group_message` | Yes | QQ + TG | Destination called “group” even for Telegram chat/channel semantics. |
| `delete_message(message_id)` | Yes | QQ + TG | TG overloads one string as `chat_id:message_id`; invalid format throws. |
| `get_message` | Indirect/legacy potential | QQ only | TG returns `{}`. |
| `get_friend_list`, `get_group_list` | No | QQ only | TG returns `{}`; social graph/listing is not portable. |
| user/chat/member queries | `get_group_member_info` | Both, adapter-dependent | Untyped JSON; `no_cache` leaks OneBot. |
| kick/ban/admin/name/leave/portrait | Some legacy potential | Declared both | Different permission and semantic models hidden by same signatures. |
| anonymous/card/honor | No searched call | QQ | TG `{}`. |
| status/version/can-send | No | QQ real; TG synthetic | Cannot reliably discover capability/health. |
| credentials/cookies/CSRF | No | QQ-oriented | TG `{}`; secrets should not be business-actor APIs. |
| friend/group requests | No | QQ-oriented | TG `{}`. |
| `IQQBot::get_forward_msg` | Yes (via concrete cast) | QQ | Should use namespaced typed capability; raw JSON today. |
| `IQQBot` file URLs/poke | Legacy paths possible | QQ | Platform extension appropriate. |
| Telegram topic/photo/albums/edit | Yes | TG | Correctly namespaced, but discovery is RTTI and results opaque. |
| Telegram media download/upload | Yes | TG | Some APIs expose raw bytes/URLs and concrete connection assumptions. |
| `on_event<T>` | App ingress | Both | In-process callback, not serializable subscription/event contract. |
| `run/stop/connect/is_connected` | Process | Both | Correctly process-owned in practice; should not be actor capability calls. |

## Gap table

| Gap | Status | Evidence | Risk / needed behavior |
|---|---|---|---|
| Capability discovery | **UNSUPPORTED** | F1, F3, F6 | Replace empty methods/RTTI assumptions with advertised operation descriptors. |
| Typed operation results/errors | **UNSUPPORTED** | F1, F6, F7 | JSON strings and `{}` cannot distinguish unsupported, rejected, transport failure or success. |
| Multi-account Bridge routing | **API_LIMITED** | F14, F15, F21 | Platform-only lookup fails when ambiguous; route by envelope/config target account. |
| Generic send/delete | **EMULATED/current** | F1, F6 | Normalize destination and composite message reference rather than string conventions. |
| Message retrieval/history | QQ **NATIVE-ish**, TG **UNSUPPORTED** | F6/F7 | Must be optional, never mandatory base API. |
| Friends/social graph | QQ extension candidate; TG **UNSUPPORTED** | F1/F6 | Remove from common contract. |
| Group moderation | **API_LIMITED** | F1/F6 | Split atomic capabilities with platform restrictions and typed errors. |
| Anonymous/honor/QQ credentials | **EXTENSION** | F1/F6 | `qq.*`; credentials excluded from actor business surface. |
| Telegram topics/albums/entities/edit | **EXTENSION**, some common candidates | F3/F6 | Typed optional features; preserve UTF-16 entity semantics in `telegram.*`. |
| Full inbound event coverage | **API_LIMITED** | F9/F10/F18 | Only message/notice enter actor pipeline; add typed subscriptions/events intentionally. |
| Event identity/idempotency | **API_LIMITED** | F10/F12 | Telegram update ID not retained canonically; notice IDs are process-local counters. |
| Transport lifecycle isolation | **NATIVE architecture boundary** | F17/F18 | Preserve process ownership; add operation admission/cancellation during shutdown/reload. |
| Telegram websocket config | **UNSUPPORTED/inconsistent** | F6/F16 | Reject at validation or implement; do not map then throw at setup. |

## Concrete current-call-to-future-capability migration map

| Current call/pattern | Future typed request/capability | Migration notes |
|---|---|---|
| `registry.find_bot(platform)` | `AccountRef{platform, account_id}` + `CapabilityDirectory::resolve` | Source uses `envelope.source_bot`; target account comes from Bridge config/mapping. No ambiguous fallback except explicit compatibility mode. |
| `send_private_message` / `send_group_message` | `message.create(SendMessage{account, ConversationRef, content, reply_to?, idempotency_key}) -> SendMessageResult` | One destination model; structured `MessageRef`; typed platform response/raw escape hatch. |
| TG `send_topic_message` | common `message.create` with optional `ThreadRef`, advertised `threads.send`; retain `telegram.topic` extension fields | Do not force topic into every adapter. |
| `delete_message(string)` | `message.delete(DeleteMessage{account, MessageRef{conversation,id}})` | Eliminates Telegram colon encoding. |
| TG `edit_message_text` | `message.edit(EditMessage{MessageRef, content})` | Capability-advertised; typed unsupported/permission/window errors. |
| `get_message` | optional `message.get(MessageRef)` | QQ adapter implements; TG advertises unsupported. |
| `get_group_member_info` | `members.get(ConversationRef, UserRef)` | Remove `no_cache` from common semantics or put cache directive in optional request metadata. |
| kick/ban/admin/name/etc. | separate `members.kick`, `members.restrict`, `roles.assign`, `conversation.update` | Never advertise a monolithic moderation capability. |
| anonymous/honor/poke/forward/file URL | `qq.anonymous.*`, `qq.honor.get`, `qq.poke`, `qq.forward.get`, `qq.file.resolve` | Replace concrete `QQBot` cast with `qq` capability client. |
| TG photo/media group/upload/entities | `media.send`/`media.album.send` plus `telegram.entities` and upload source variants | Capability indicates URL/file-id/byte upload support and limits. |
| TG media download methods | `media.resolve` then process-owned `media.download` request/result | Avoid giving business actors live connection-manager or arbitrary transport access. |
| `set_commands` | `commands.catalog.publish` | Typed result; no default `{}` implementation. |
| `get_status/is_connected` | `account.health.get` (process observability) | Keep transport liveness out of normal business actors unless authorized. |
| cookies/CSRF/credentials | no business capability; process secret provider only | Delete from common actor-visible surface. |
| handler `dynamic_cast` | typed capability client/discovery token | Unsupported is a typed discovery/result state, not cast failure. |
| callback ingress `MessageEvent` | serializable `im.message.created/edited`, `im.notice.*`, namespaced raw event | Preserve raw payload; include provider event/update ID and delivery metadata. |
| direct bot await in Bridge | send `OutboundOperationRequest` to process egress service/actor; receive correlated `OperationResult` | Actor package retains no live bot pointer; process owns network, retry/admission/idempotency. |

## Architecture

Current data flow is:

`OneBot WS/HTTP or Telegram HTTP polling -> protocol adapter parse -> common::Event variant -> bot EventDispatcher on bot io_context -> app message/notice callback -> MessageEnvelope -> reload controller/current actor generation -> message-store MessageStored -> BridgeActor -> BridgeForwardingRuntime -> BotRegistry weak lookup -> live IBot/platform cast -> connection manager action -> opaque JSON -> Bridge mapping persistence`.

The desirable seam already exists at `MessageEnvelope`/`ActorContext`: introduce serializable typed outbound request/result messages and a process-owned egress capability service. Keep adapters, credentials, connection managers and I/O contexts outside actor package ownership.

## Unknowns and open questions

- The exact actor pipeline configuration transforming `RawMessageEvent` to `MessageStored` was not exhaustively traced; Bridge explicitly accepts the literal type `MessageStored` while reflected core types may use canonical qualified names in other contexts (F13/F20).
- Telegram callback-query conversion details and whether it becomes `NoticeEvent` were not fully enumerated beyond the parser entry point.
- Several Bridge legacy files coexist with actor forwarding runtime; searched call inventory may include paths not active under the actor pipeline.
- No complete adapter conformance tests were found proving every declared QQ/TG operation, error schema, timeout or reconnect behavior.
- It is unclear whether target bot account selection exists in Bridge configuration elsewhere; runtime shown here ignores account IDs and uses platform-only lookup.
- Telegram polling’s canonical event `self_id` remains `"0"`; app-supplied configured bot name masks this for envelope `source_bot`, but provider bot identity correctness is unknown.
- Rate limits, idempotency, payload limits, permission scopes and retry-after semantics are not represented in current interfaces and were not derivable from this local-only survey.

## Claim-to-file checklist

| Claim | Evidence IDs | Checked |
|---|---|---:|
| Base interface is OneBot/QQ-coupled and oversized | F1, F8 | ✓ |
| Telegram has empty/synthetic unsupported implementations | F3, F6 | ✓ |
| Platform-specific capability interfaces already exist | F2, F3, F4, F5 | ✓ |
| Bridge resolves and directly calls process bots | F20, F21 | ✓ |
| Multi-account platform lookup is intentionally ambiguous | F14, F15, F21 | ✓ |
| Only message/notice enter actor runtime | F11, F18 | ✓ |
| Telegram normalization retains raw payload but uses placeholder self ID | F10 | ✓ |
| Envelope carries source/account/conversation/raw metadata | F12, F13 | ✓ |
| Bots/network survive actor reload and have strict shutdown ordering | F17, F18 | ✓ |
| Telegram transport configuration is inconsistent | F6, F16 | ✓ |
| Migration should use typed serializable operations and capability discovery | F1, F3, F6, F13, F20, F21 | ✓ |

# Code Context

## Files Retrieved

1. `include/interfaces/bot.hpp` (lines 24-379) — complete current common surface.
2. `include/interfaces/qq_bot.hpp` (lines 8-35) — QQ extension.
3. `include/interfaces/telegram_bot.hpp` (lines 14-170) — Telegram extension DTOs/capabilities.
4. `src/core/tg_bot.cpp` (lines 20-504) — asymmetry/stubs and HTTP lifecycle.
5. `include/core/bot_registry.hpp` (lines 15-107) — ownership/account resolution.
6. `src/core/runtime/message_event_ingress.cpp` (lines 21-174) — event-to-envelope mapping.
7. `include/core/actor/actor.hpp` (lines 81-110, 174-284) — actor envelope/services/runtime crossing.
8. `local_actor/obcx-actor-bridge/dependency/bridge_forwarding_runtime.cpp` (lines 13-112) — Bridge live bot calls.
9. `local_actor/obcx-actor-bridge/actor/bridge_actor.cpp` (lines 104-250) — Bridge actor service flow.
10. `src/app/main.cpp` (lines 284-418, 420-510) — process ownership, ingress and reload/shutdown.

## Key Code

- `IBot`: giant virtual API returning `asio::awaitable<std::string>`.
- `BotRegistry`: `(platform, bot_id) -> weak_ptr<IBot>`, with intentionally ambiguous platform-only lookup.
- `MessageEnvelope`: serializable routing/correlation/raw boundary.
- `BridgeForwardingRuntime::forward_message`: converts stored message, resolves two bots, calls handlers, requires persisted mapping.

## Start Here

Open `include/interfaces/bot.hpp:24-379` first: it exposes the central design problem and provides the baseline against which capability extraction and migration should be planned.
