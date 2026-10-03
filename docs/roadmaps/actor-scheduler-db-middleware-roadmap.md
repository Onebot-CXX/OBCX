# Actor Scheduler And DB Middleware Roadmap

> Historical SDK note: this document records the earlier migration. References
> to `BotOperationClient` or schema-1 input contracts describe the retired SDK;
> see the [current modular SDK migration](../architecture/modular-bot-sdk-migration.md) for schema 2.

> Historical implementation roadmap. Use the current actor architecture,
> operations, and database guides for supported configuration and package
> paths.

> Status options: `TODO`, `DOING`, `DONE`, `BLOCKED`, `DEFERRED`.
> Use checkboxes for accepted completion. This roadmap supersedes the older
> assumption that `message_store` owns bridge message mappings.

## Goal

Split OBCX into clear execution and ownership domains:

- Asio remains the high-throughput IO substrate.
- OBCX owns actor scheduling, mailbox ordering, pipeline routing, and
  backpressure.
- Middleware services such as `DbManager` provide infrastructure capabilities
  without becoming actors.
- Standalone actor repositories own their business repositories and tables.

The first target flow is:

```text
platform bot -> obcx::core::events::RawMessageEvent -> message_store actor -> obcx::message_store::events::MessageStored
obcx::message_store::events::MessageStored -> bridge actor -> bridge::events::MessageForwarded / bridge::events::MessageForwardFailed
```

## Non-Goals

- No Kafka-style durable broker, offsets, replay, or consumer groups.
- No DB reads or writes through actor mailbox.
- No `DbManager` actor.
- No bridge mapping tables inside `message_store`.
- No requirement that every actor use the same physical DB.
- No removal of legacy plugin compatibility in this phase.

## Architecture Lock

```text
OBCX core runtime repository
  - Actor ABI and ActorManager
  - Router / Orchestrator
  - ActorScheduler
  - Middleware registry
  - DbManager middleware interface and default SQLite provider
  - Legacy plugin compatibility

message_store actor repository
  - message_store actor dynamic library
  - Received-message repository
  - Platform message tables such as qq_messages and telegram_messages
  - Emits obcx::message_store::events::MessageStored

bridge actor repository
  - bridge actor dynamic library
  - Bridge mapping and state repository
  - Bridge-owned tables for mappings, media groups, retry state, and recall state
  - Emits bridge::events::MessageForwarded / bridge::events::MessageForwardFailed
```

## Execution Domains

```text
Asio IO executor
  Network, timers, HTTP/WebSocket, file IO, coroutine await substrate.

ActorScheduler
  Runtime-owned scheduler for actor mailbox, partition, pipeline continuation,
  and backpressure.

Middleware executors
  Infrastructure-owned executors for DB, media processing, CPU-heavy work, and
  other blocking or slow operations.
```

Rules:

- Business events enter actor mailbox.
- Infrastructure calls use middleware services from `ActorContext`.
- Actor workers must not block on slow IO or CPU-heavy work.
- DB work uses `DbManager` and its internal DB executor or connection pool.
- Actor ordering is guaranteed by `actor_id + partition_key` mailbox.

## Default Runtime Flow

```text
platform adapter
  -> creates obcx::core::events::RawMessageEvent

Router / Orchestrator
  -> resolves pipeline stages
  -> enqueues ActorInvocation(message_store, source_platform:conversation_id)

ActorScheduler
  -> runs message_store mailbox task
  -> actor calls MessageStoreRepository
  -> repository calls DbManager
  -> actor emits obcx::message_store::events::MessageStored

Router / Orchestrator
  -> enqueues ActorInvocation(bridge, source_platform:conversation_id)

ActorScheduler
  -> runs bridge mailbox task
  -> bridge forwards message
  -> bridge repository writes mapping/state through DbManager
  -> bridge emits bridge::events::MessageForwarded or bridge::events::MessageForwardFailed
```

## Default Config Shape

```toml
[db.instances.main]
type = "sqlite"
path = "data/obcx.sqlite3"

[actors.message_store]
library = "message_store"
db = "main"
db_namespace = "message_store"
partition = "source_platform:conversation_id"

[actors.bridge]
library = "bridge"
db = "main"
db_namespace = "bridge"
partition = "source_platform:conversation_id"

[pipelines.received_message]
source = "obcx::core::events::RawMessageEvent"

[[pipelines.received_message.stages]]
name = "persist_received"
actor = "message_store"
input = "obcx::core::events::RawMessageEvent"
output = "obcx::message_store::events::MessageStored"
mode = "await"

[[pipelines.received_message.stages]]
name = "forward"
actor = "bridge"
input = "obcx::message_store::events::MessageStored"
output = ["bridge::events::MessageForwarded", "bridge::events::MessageForwardFailed"]
after = ["persist_received"]
mode = "await"
```

Default DB policy:

- `message_store` and `bridge` default to the same physical `main` DB.
- They use separate namespaces and separate actor-owned tables.
- Operators may later move either actor to another DB instance by config.

## Completion Tracker

| Item | Status | Done | Owner | Evidence |
| --- | --- | --- | --- | --- |
| Runtime execution domains documented | DONE | [x] | Core | This roadmap |
| DbManager classified as middleware, not actor | DONE | [x] | Core | This roadmap |
| message_store mapping responsibility removed | DONE | [x] | Storage | This roadmap and bridge boundary docs |
| Bridge mapping persistence remains required | DONE | [x] | Bridge | Bridge owns mapping DB tables |
| Runtime service registry designed | DONE | [x] | Core | `ActorApiTest` and `OrchestratorTest` service injection tests pass |
| DbManager interface and SQLite provider implemented | DONE | [x] | Core | `DbManagerTest` read/write, writer-thread, and migration-lock tests pass |
| ActorScheduler sharded mailbox implemented | DONE | [x] | Runtime | `ActorSchedulerTest` ordering/concurrency/shard/backpressure tests pass |
| Orchestrator enqueues actor tasks instead of executing actors directly | DONE | [x] | Runtime | `OrchestratorTest` scheduler/routing/terminal async tests pass |
| Config parses `[db.instances]` and actor DB binding | DONE | [x] | Config | `ActorConfigTest` DB parsing and validation tests pass |
| message_store moved to standalone actor repo | DONE | [x] | Storage | `local_plugin/obcx-message-store` builds against installed SDK and `message_store_smoke` passes |
| bridge stores mappings in bridge-owned DB tables | DONE | [x] | Bridge | Bridge repository tests cover its actor and database schema |
| bridge message retry manager uses bridge-owned DB state | DONE | [x] | Bridge | `RetryQueueManagerTest` restores persisted retries and removes successful retries |
| legacy bridge handlers use bridge state repository for mapping behavior | DONE | [x] | Bridge | `BridgeHandlerRepositoryTest` and bridge plugin build prove reply mapping lookup uses `BridgeStateRepository`; QQ/TG handlers are wired to repository for mapping, media-group, reply, and recall paths |
| legacy bridge handlers no longer write raw received messages directly | DONE | [x] | Bridge | `BridgeHandlerRepositoryTest` guards against raw-message writes in forwarding handlers; `ReceivedMessageRepositoryTest` proves bridge reads message_store-owned raw messages |
| platform message events enter actor pipeline | DONE | [x] | Runtime | `MessageEventIngressTest` proves `MessageEvent -> obcx::core::events::RawMessageEvent`; main runtime registers actor ingress when actors/pipelines are configured |
| actor-mode bridge forwarding uses `obcx::message_store::events::MessageStored` | DONE | [x] | Bridge | `BridgeActorTest` proves forwarding runtime service consumption; bridge actor constructs `BridgeForwardingRuntime` with `BotOperationClient` and exact installation pairs |

## Phase 0: Boundary Correction

Status: DONE

- [x] Define `DbManager` as middleware/service, not actor.
- [x] Define actor mailbox as business-event scheduling only.
- [x] Define DB read/write as actor-internal infrastructure calls.
- [x] Define `message_store` as standalone received-message actor.
- [x] Define bridge mappings as bridge-owned DB state.
- [x] Default bridge and message_store to the same physical `main` DB with
  separate namespaces.

Exit criteria:

- [x] Roadmap states that `message_store` does not own bridge mappings.
- [x] Roadmap states that bridge mappings must still be persisted in DB.

## Phase 1: Runtime Middleware Registry

Status: DONE

- [x] Add runtime-level service registration that is shared by actor
  invocations.
- [x] Keep `ActorContext` as the per-invocation service lookup surface.
- [x] Inject middleware services into every orchestrated actor context.
- [x] Keep actor-local services available for tests and actor-specific state.

Exit criteria:

- [x] A test actor can retrieve runtime middleware from `ActorContext`.
- [x] Missing DB middleware binding produces startup validation errors.
- [x] Legacy actor context behavior still passes.

## Phase 2: DbManager Middleware

Status: DONE

- [x] Add `[db.instances]` config parsing.
- [x] Add actor DB binding fields:
  - `db`
  - `db_namespace`
- [x] Add `DbManager` interface for DB instance lookup and read/write execution.
- [x] Add default SQLite provider.
- [x] Enable SQLite WAL, busy timeout, and foreign keys.
- [x] Add migration locking.
- [x] Support a single-writer SQLite execution path so actor workers do not
  block on DB writes.
- [x] Keep DB migrations owned by each actor repository.

Exit criteria:

- [x] Multiple actors can use the same physical `main` DB.
- [x] Each actor can resolve its configured namespace.
- [x] SQLite write work is isolated from actor scheduler workers.
- [x] DB errors are surfaced to the caller.

## Phase 3: ActorScheduler

Status: DONE

- [x] Add `ActorInvocation` (renamed from the V1 `ActorTask` payload) with:
  - actor id
  - partition key
  - input envelope
  - pipeline context
- [x] Add mailbox key: `actor_id + ":" + partition_key`.
- [x] Add sharded runnable queues based on mailbox key hash.
- [x] Drain one task at a time per mailbox.
- [x] Requeue mailbox when it still has pending tasks.
- [x] Add global backpressure limit.
- [x] Add per-actor and per-partition backpressure limits.
- [x] Replace timer-based mailbox waiting with explicit mailbox queues.

Exit criteria:

- [x] Same actor partition runs serially.
- [x] Different partitions run concurrently.
- [x] Slow actor tasks do not block unrelated partitions.
- [x] Backpressure rejects or delays new tasks with observable failure data.

## Phase 4: Orchestrator And Router Integration

Status: DONE

- [x] Keep Orchestrator responsible for pipeline matching and dependency
  resolution.
- [x] Change Orchestrator from direct actor execution to task enqueue.
- [x] Route emitted actor messages back through the Router / Orchestrator.
- [x] Preserve `await` dependencies by awaiting dependent stage completion.
- [x] Preserve terminal `async` stage behavior by enqueueing without blocking
  upstream completion.
- [x] Preserve failure envelope emission for actor failures.

Exit criteria:

- [x] `obcx::core::events::RawMessageEvent -> obcx::message_store::events::MessageStored -> bridge::events::MessageForwarded` still works.
- [x] Existing pipeline validation remains unchanged.
- [x] Actor failure still emits `ActorFailed`.
- [x] Scheduler and orchestrator tests cover await and non-blocking terminal
  async stage behavior.

## Phase 5: Standalone message_store Actor Repository

Status: DONE

- [x] Move `message_store` into its own actor repo:
  `local_plugin/obcx-message-store`.
- [x] Store received platform messages only.
- [x] Use per-platform or namespaced tables such as:
  - `message_store_qq_messages`
  - `message_store_telegram_messages`
- [x] Keep idempotency per `source_platform`, `source_bot`, and platform
  message id.
- [x] Emit `obcx::message_store::events::MessageStored` after successful persistence.
- [x] Expose received-message query APIs for other actors.
- [x] Remove message mapping storage from `message_store`.
- [x] Use `DbManager` from `ActorContext`.

Implementation note:

- OBCX exposes actor and DB middleware contracts only. The standalone actor
  repository owns the implementation and its tests.

Exit criteria:

- [x] QQ and Telegram received messages are persisted once per platform.
- [x] `obcx::message_store::events::MessageStored` includes enough payload for bridge forwarding.
- [x] `message_store` has no bridge mapping table or mapping API.
- [x] External actor repo builds against OBCX actor SDK.

## Phase 6: Bridge Actor Repository DB State

Status: DONE

The bridge repository now has a DbManager-backed bridge state repository and a
`bridge` actor contract. Repository-level persistence exists for message
mappings, media-group mappings, reply/recall lookup mappings, and message retry
state. The legacy retry queue manager now restores, updates, removes message
retry rows, and writes successful retry mappings through that repository.
Legacy QQ/TG handlers use the bridge state repository for mapping-backed
media-group, reply, and recall behavior. In actor mode, platform
`MessageEvent`s are converted to `obcx::core::events::RawMessageEvent`, persisted by
`message_store`, emitted as `obcx::message_store::events::MessageStored`, reconstructed into the old handler
`MessageEvent` shape, and forwarded through `BridgeForwardingRuntime`. Raw
received-message persistence has moved out of the legacy forwarding handlers;
bridge behavior that still needs original message details reads
message_store-owned tables through `ReceivedMessageRepository`. The old raw
callback path remains only as a compatibility fallback when the actor pipeline
is not configured.

- [x] Keep bridge mapping persistence in the bridge actor repo.
- [x] Store bridge-owned tables through `DbManager`, defaulting to the same
  physical `main` DB and namespace `bridge`.
- [x] Persist cross-platform message mappings.
- [x] Persist media-group mappings in `bridge_media_group_mappings`.
- [x] Persist recall/reply lookup state required by bridge behavior through
  restart-safe `bridge_message_mappings`.
- [x] Persist message retry state that must survive process restart in
  `bridge_message_retry_queue`.
- [x] Wire legacy message retry manager to the DbManager-backed bridge state
  repository.
- [x] Wire QQ/TG forwarding handlers to the DbManager-backed bridge state
  repository for media-group, reply, and recall mapping behavior.
- [x] Persist bridge mapping when a retry eventually sends successfully.
- [x] Remove direct raw-message persistence from legacy QQ/TG forwarding
  handlers.
- [x] Add a bridge-side received-message lookup that reads message_store-owned
  raw message tables for recall/poke compatibility during migration.
- [x] Convert QQ/TG forwarding handlers from raw callback ingestion to
  `obcx::message_store::events::MessageStored` actor ingestion.
- [x] Do not write bridge mappings through `message_store`.
- [x] Build a bridge actor that consumes `obcx::message_store::events::MessageStored`, writes
  `bridge_message_mappings`, and emits `bridge::events::MessageForwarded` /
  `bridge::events::MessageForwardFailed` contract events.

Exit criteria:

- [x] Repository-level reply and recall lookup resolves cross-platform mappings
  after restart.
- [x] Repository-level media-group mapping maps all source messages to target
  output state.
- [x] Bridge mapping tables are owned and migrated by bridge repo.
- [x] Bridge emits `bridge::events::MessageForwarded` and `bridge::events::MessageForwardFailed`.
- [x] Legacy retry manager restores, updates, and removes restart-safe message
  retry rows through the bridge state repository.
- [x] Legacy QQ/TG handlers use the bridge state repository for media-group,
  reply, and recall mapping behavior.
- [x] Legacy QQ/TG forwarding handlers no longer directly persist raw received
  messages.
- [x] Legacy QQ/TG forwarding handlers consume `obcx::message_store::events::MessageStored` instead of raw
  bot callbacks.

## Phase 7: Compatibility And Migration

Status: DONE

OBCX core verifies the actor ABI, ingress envelopes, mailbox scheduling, DB
middleware, and task dispatch with fixture actors. It does not build or test
concrete actors. The standalone `message_store` smoke test and bridge
persistence tests run in their owning repositories; any combined actor
integration belongs in an explicit external integration harness.

- [x] Keep legacy plugin loading and plugin topology behavior working.
- [x] Keep actor ABI stable for standalone actor repositories.
- [x] Provide migration guidance from the historical in-core `message_store`
  prototype to the standalone actor repo.
- [x] Provide bridge repo integration guidance for `DbManager`.
- [x] Prove the standalone `message_store` actor repo builds against the
  installed OBCX actor SDK.
- [x] Keep `message_store` implementation and persistence smoke coverage in
  `local_plugin/obcx-message-store`.
- [x] Keep bridge actor and mapping persistence coverage in the bridge
  repository.

Exit criteria:

- [x] Existing plugin tests pass.
- [x] New actor scheduler tests pass.
- [x] DB middleware tests pass.
- [x] The standalone actor repositories independently prove message
  persistence and bridge mapping persistence against the installed OBCX SDK.

## Test Plan

- Config tests:
  - Parse `[db.instances]`.
  - Parse `db` and `db_namespace` on actors.
  - Reject actors that reference missing DB instances.
- DbManager tests:
  - Create SQLite `main` DB.
  - Run read and write operations.
  - Serialize SQLite writes.
  - Surface DB failures without crashing actor scheduler.
- Scheduler tests:
  - Same mailbox order is serial.
  - Different partitions run concurrently.
  - Backpressure limit is enforced.
  - Actor exception emits failure envelope.
- Concrete actor repository tests (outside the OBCX core suite):
  - `obcx::core::events::RawMessageEvent` reaches message_store.
  - `obcx::message_store::events::MessageStored` reaches bridge.
  - message_store writes only received-message tables.
  - bridge writes bridge-owned mapping tables.
  - actor-mode platform callbacks create `obcx::core::events::RawMessageEvent` without calling
    legacy bridge raw forwarding callbacks.

## Review Checklist

- [x] Does this avoid turning DB into an actor?
- [x] Does this keep Asio focused on IO?
- [x] Does this define OBCX-owned actor scheduling?
- [x] Does this keep bridge mappings in DB?
- [x] Does this keep bridge mappings out of `message_store`?
- [x] Does this allow same physical DB with separate actor ownership?
- [x] Does this allow future split DB instances by config?
