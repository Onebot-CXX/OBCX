# OBCX Actor Runtime Roadmap

> Historical roadmap from the compatibility phase. Paths, extension APIs, and
> rollout choices below are preserved as evidence and are not current user
> guidance.

> Status options: `TODO`, `DOING`, `DONE`, `BLOCKED`, `DEFERRED`.
> Use the checkbox when the item is accepted as complete, and keep the status
> value current while work is in progress.

## Goal

Move OBCX from a plugin callback framework toward an in-process actor-style
message orchestration runtime. The new architecture should express flows such
as `bot -> message_store -> bridge` as runtime-managed actor pipelines instead
of relying on plugin load order or concurrent event callbacks.

For the scheduler, middleware, DB manager, standalone `message_store`, and
bridge-owned mapping plan, see
`docs/roadmaps/actor-scheduler-db-middleware-roadmap.md`.

## Non-Goals For V1

- No cross-process broker.
- No durable queue, ack/replay, consumer group, or offset management.
- No full arbitrary workflow language.
- No immediate removal of legacy plugin compatibility.

## Completion Tracker

| Item | Status | Done | Owner | Evidence |
| --- | --- | --- | --- | --- |
| Actor naming and compatibility boundary approved | DONE | [x] | Core | `docs/architecture/actor-runtime-adr.md` |
| Actor public interfaces added | DONE | [x] | Core | `actor_api_test` passes |
| ActorManager loads actor dynamic libraries | DONE | [x] | Core | `actor_manager_test` passes |
| Orchestrator executes pipeline stages in order | DONE | [x] | Runtime | `orchestrator_test` passes |
| ConfigLoader parses and validates `[actors]` / `[pipelines]` | DONE | [x] | Config | `actor_config_test` and `plugin_topology_test` pass |
| Actor build helper added | DONE | [x] | Build | `OBCXActor.cmake` adds `obcx_add_actor(...)` |
| message_store actor stores received platform messages only | DONE | [x] | Storage | Standalone `message_store_smoke` proves DbManager-backed platform tables and no bridge mappings |
| message_store standalone actor repo builds against SDK | DONE | [x] | Storage | `local_plugin/obcx-message-store` builds with `obcx-sdk` and passes smoke test |
| bridge consumes `obcx::message_store::events::MessageStored` instead of raw callbacks | DONE | [x] | Bridge | `BridgeActorTest`, `BridgeMessageEventAdapterTest`, and `BridgeHandlerRepositoryTest` cover actor-mode forwarding runtime ingestion and legacy raw callback gating |
| Legacy plugin path still works | DONE | [x] | Compatibility | `obcx` builds and `plugin_topology_test` passes |
| Documentation updated for actor-first model | DONE | [x] | Docs | README/config examples present actor runtime as the primary model |

## Phase 0: Architecture Lock

Status: DONE

- [x] Decide that new public terminology is `actor`, not `plugin`.
- [x] Keep `plugin` only as a deprecated compatibility layer for one migration window.
- [x] Define V1 as in-process actor orchestration, not distributed messaging.
- [x] Record the minimum actor concepts:
  - `ActorId`
  - `MessageEnvelope`
  - `IActor`
  - `ActorContext`
  - `ActorResult`
  - `ActorManager`
  - `Orchestrator`
  - mailbox and partition key

Exit criteria:

- [x] The team agrees that `plugin` is legacy naming.
- [x] The team agrees that distributed broker semantics are out of V1.

## Phase 1: Core Actor API

Status: DONE

- [x] Add `MessageEnvelope` with:
  - `id`
  - `type`
  - `source_platform`
  - `source_bot`
  - `correlation_id`
  - `causation_id`
  - `timestamp`
  - `payload`
  - `raw`
  - `headers`
- [x] Add `IActor` with one async message handling method.
- [x] Add `ActorResult` with emitted envelopes and failure information.
- [x] Add `ActorContext` for service lookup, logging context, and emit helpers.
- [x] Add optional actor provider/export path that does not break existing `IPlugin` ABI.
- [x] Add `ActorManager` dynamic library loading with `obcx_create_actor` symbols.

Exit criteria:

- [x] A test actor can be loaded and invoked.
- [x] Existing plugin loading still compiles.

## Phase 2: Orchestrator Runtime

Status: DONE

- [x] Add `Orchestrator` that receives `MessageEnvelope`.
- [x] Resolve pipeline stages by `source`, `input`, `after`, and `mode`.
- [x] Execute `mode = "await"` stages before dependent stages.
- [x] Execute terminal `mode = "async"` stages after required await/dependent chains in the V1 awaitable runtime.
- [x] Add mailbox ordering per actor partition.
- [x] Default partition is `global`.
- [x] Support configurable partition expressions such as
  `source_platform:conversation_id`.
- [x] Emit failure envelopes when an actor fails.

Exit criteria:

- [x] `obcx::core::events::RawMessageEvent -> obcx::message_store::events::MessageStored -> bridge::events::MessageForwarded` order is proven by tests.
- [x] Same partition messages are processed in order.
- [x] Different partitions can process concurrently.

## Phase 3: Config Model

Status: DONE

- [x] Add actor config parsing:

```toml
[actors.message_store]
library = "message_store"
enabled = true
```

- [x] Add pipeline config parsing:

```toml
[pipelines.message]
source = "obcx::core::events::RawMessageEvent"

[[pipelines.message.stages]]
name = "persist"
actor = "message_store"
input = "obcx::core::events::RawMessageEvent"
output = "obcx::message_store::events::MessageStored"
mode = "await"
```

- [x] Validate missing actors.
- [x] Validate missing stage dependencies.
- [x] Validate dependency cycles.
- [x] Preserve legacy config behavior when no `[pipelines]` section exists.

Exit criteria:

- [x] Invalid actor pipeline configs fail with clear errors.
- [x] Old config files still start through the legacy path.

## Phase 4: Build And Naming Migration

Status: DONE

- [x] Add `cmake/OBCXActor.cmake`.
- [x] Add `obcx_add_actor(...)`.
- [x] Add `OBCX_ACTOR_EXPORT(ActorClass)`.
- [x] Standardize actor dynamic library symbols:
  - `obcx_create_actor`
  - `obcx_destroy_actor`
  - `obcx_get_actor_name`
  - `obcx_get_actor_version`
- [x] Keep `OBCXPlugin.cmake` and `OBCX_PLUGIN_EXPORT(...)` as deprecated compatibility.
- [x] Update example metadata from `plugin.toml` toward actor naming.

Exit criteria:

- [x] A sample actor builds with the new helper.
- [x] Existing plugin builds continue to work during the migration window.

## Phase 5: message_store Actor

Status: DONE

Superseded by: `docs/roadmaps/actor-scheduler-db-middleware-roadmap.md`.
The received-message actor now exists as a standalone actor repository. The
OBCX repository contains only the actor framework and dispatch tests.

- [x] Move `message_store` into a standalone actor repository:
  `local_plugin/obcx-message-store`.
- [x] Store received platform messages only.
- [x] Use `DbManager` middleware instead of owning raw SQLite directly.
- [x] Use actor-owned tables such as `message_store_qq_messages` and
  `message_store_telegram_messages`.
- [x] Keep duplicate raw message writes idempotent by platform, bot,
  conversation, and platform message id.
- [x] Handle `obcx::core::events::RawMessageEvent` input and emit `obcx::message_store::events::MessageStored` output.
- [x] Emit failure output: `MessageStoreFailed`.
- [x] Expose direct store/query APIs for received-message lookup.
- [x] Do not store bridge message mappings.
- [x] Add schema versioning for future migrations.

Exit criteria:

- [x] QQ and Telegram message events are saved once per
  platform/bot/conversation/message identity.
- [x] Received-message lookup works through the actor service.
- [x] `message_store` has no bridge mapping tables or mapping API.

## Phase 6: Bridge Actor Migration

Status: DOING

The separate `local_plugin/obcx-plugin-bridge` repository now contains a
bridge actor contract, DbManager-backed bridge state repository, and
`BridgeForwardingRuntime`. In actor mode, QQ/TG platform callbacks enter core
as `obcx::core::events::RawMessageEvent`, `message_store` emits `obcx::message_store::events::MessageStored`, and the bridge
actor reconstructs the old handler `MessageEvent` shape before invoking the
existing QQ/TG forwarding handlers. The legacy message retry manager already
uses the bridge state repository for restart-safe retry rows and successful
retry mapping writes. The legacy handlers now use the bridge state repository
for mapping-backed media-group, reply, and recall behavior. Direct raw-message
persistence has moved out of the handlers; bridge behavior that needs original
raw message details reads the message_store-owned tables through a bridge-side
lookup during migration. The raw callback path remains as a legacy fallback
only when actor pipeline config is absent.

- [x] Convert `qq_to_tg` to consume `obcx::message_store::events::MessageStored`.
- [x] Convert `tg_to_qq` to consume `obcx::message_store::events::MessageStored`.
- [x] Remove direct raw-message persistence from bridge forwarding code.
- [x] Make bridge query received-message data through message_store-owned
  tables when needed by recall/poke compatibility paths.
- [x] On successful forward, write mapping state through bridge-owned
  repositories backed by `DbManager`.
- [x] Emit:
  - `bridge::events::MessageForwarded`
  - `bridge::events::MessageForwardFailed`
- [x] Persist bridge-owned media-group mappings, message retry state, and
  recall/reply lookup mappings in DB repository APIs.
- [x] Persist bridge-owned cross-platform message mappings in
  `bridge_message_mappings`.
- [x] Wire legacy message retry manager to the DbManager-backed bridge state
  repository.
- [x] Wire QQ/TG forwarding handlers to the DbManager-backed bridge state
  repository for media-group, reply, and recall mapping behavior.
- [x] Move raw-message actor flow fully to `message_store -> bridge` by making
  legacy QQ/TG forwarding handlers consume `obcx::message_store::events::MessageStored`.
- [ ] Keep media-group, reply, recall, poke, and current retry behavior intact.

Exit criteria:

- [x] bridge no longer depends on event callback ordering in actor mode.
- [x] repository-level reply and recall lookup resolves cross-platform mappings
  after restart.
- [x] repository-level media-group mapping maps all source messages to the
  target output state.
- [x] legacy retry manager restores, updates, and removes restart-safe message
  retry rows.
- [x] legacy QQ/TG handler media-group, reply, and recall behavior uses those
  repository-level mappings.
- [x] legacy QQ/TG forwarding handlers no longer directly persist raw received
  messages.
- [x] legacy QQ/TG forwarding handlers consume `obcx::message_store::events::MessageStored` instead of raw
  bot callbacks.
- [ ] bridge mapping state survives process restart.

## Phase 7: Verification And Compatibility

Status: DOING

Bridge contract tests now run against the separate
`local_plugin/obcx-plugin-bridge` repository. Actor-mode ingress and
`obcx::message_store::events::MessageStored` forwarding runtime coverage exists; full behavior migration
coverage still needs QQ/TG handler tests with realistic bot responses.

- [x] Add tests for actor loading.
- [x] Add tests for orchestrator stage order.
- [x] Add tests for mailbox partition ordering.
- [x] Add tests for config validation.
- [x] Add message_store DbManager-backed SQLite tests.
- [x] Add bridge contract tests for DbManager-backed mapping, media-group,
  reply/recall lookup, and retry persistence.
- [x] Add retry manager tests for persisted message retry restore and
  successful retry cleanup.
- [x] Add handler repository tests for legacy reply mapping lookup.
- [x] Add received-message repository tests for bridge reads from
  message_store-owned tables.
- [x] Add guard test that legacy forwarding handlers do not directly persist
  raw received messages.
- [x] Add actor-mode ingress and bridge forwarding runtime tests.
- [ ] Add full bridge migration tests for QQ/TG handler behavior.
- [x] Run relevant build and test commands.
- [x] Update README/config examples to present actors as the primary model.

Exit criteria:

- [x] New actor runtime tests pass.
- [x] Existing plugin topology tests pass.
- [x] Bridge actor integration smoke test passes for mapping persistence.
- [ ] Bridge actor integration smoke covers full forwarding behavior.

## Suggested Parallel Work Slices

| Slice | Status | Done | Scope | Writes |
| --- | --- | --- | --- | --- |
| A. Core actor API | DONE | [x] | `IActor`, envelope, actor manager | `include/core`, `src/common`, `tests/*actor*` |
| B. Orchestrator runtime | DONE | [x] | pipeline execution, mailbox, partition | `include/core`, `src/core`, `tests/*orchestrator*` |
| C. Config/build naming | DONE | [x] | `[actors]`, `[pipelines]`, CMake actor helper | `include/common`, `src/common`, `cmake` |
| D. message_store actor | DONE | [x] | standalone received-message actor and tests | `local_plugin/obcx-message-store` |
| E. bridge actor migration | DOING | [ ] | actor-mode ingestion and mapping persistence done; full QQ/TG forwarding behavior tests remain | separate `local_plugin/obcx-plugin-bridge` repository |
| F. Docs/examples | DONE | [x] | actor-first docs and examples | `README.md`, example config docs |

## Review Checklist

- [x] Does the roadmap keep V1 small enough to implement?
- [x] Does it fully replace new `plugin` terminology with `actor`?
- [x] Does it preserve a legacy compatibility path?
- [x] Does it give `bot -> message_store -> bridge` explicit runtime semantics?
- [x] Does it avoid accidentally promising distributed broker behavior?
- [x] Are the completion options clear enough to track implementation progress?
