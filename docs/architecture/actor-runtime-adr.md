# Actor Runtime Architecture Decision

Status: accepted and current (2026-07-30)

## Decision

OBCX has one extension and dispatch contract: ABI 2 actors inheriting
`ReflectedActor<Derived>`. The SDK generates the final `handle_message` entry
point from typed `handle` overloads and normalizes synchronous `ActorResult`
and asynchronous `ActorTask<ActorResult>` handlers. The runtime executes the task on the native
work-stealing scheduler. Boost.Asio remains the networking, timer, and I/O
substrate; actor code crosses that boundary explicitly through
`ActorContext::await_asio` or `ActorContext::asio_token`. Synchronous blocking
and CPU-heavy work crosses a separate boundary through
`ActorContext::run_blocking`.

Runtime construction does not select among scheduler engines. Configuration
may tune the native worker policy and budget, but cannot select another
implementation.

## Runtime model

- `MessageEnvelope` is the normalized input and emitted-message unit.
- `ActorInvocation` binds an actor id, partition key, DB routing, and message.
- `ActorTask<T>` is the move-only coroutine owned by the actor scheduler.
- `ActorContext` exposes cancellation, cooperative yield, Asio interop, and
  bot-independent blocking execution backed by typed runtime services.
- `ActorManager` validates ABI 2 symbols and the mandatory generated input
  contract before actor construction, then owns library lifetime.
- `Orchestrator` validates and executes configured pipeline stages.
- `NativeActorScheduler` owns mailboxes, runnable queues, workers,
  backpressure, and shutdown.
- `RuntimeGeneration` owns one immutable configuration snapshot, actor graph,
  scheduler, Asio executor, services, staged libraries, and generation id.
- `ActorRuntimeReloadController` owns the active-generation pointer, root
  ingress gate, and single-flight reload transaction.
- The process owns one fixed-size `BlockingExecutor`; startup and every reload
  generation reference the exact same service, while validation-only builds
  create none.

An actor/partition mailbox is exclusive: at most one invocation is active,
queued messages remain FIFO, and suspension retains mailbox ownership. An I/O
callback only republishes a continuation; it never resumes actor code inline.
These rules preserve ordering while allowing workers to steal runnable work
from unrelated mailboxes.

Actor execution is cooperative. CPU-heavy or blocking work belongs behind
`ActorContext::run_blocking`; bot event coroutines themselves run on the bot's
non-blocking I/O executor. Actor code must observe cancellation at suitable
boundaries and must not retain references to an invocation context after the
task completes.

## Pipelines and services

Dependencies are expressed in actor and pipeline configuration:

```toml
[actors.message_store]
library = "message_store"
enabled = true
partition = "source_platform:conversation_id"
db = "main"
db_namespace = "message_store"

[actors.bridge]
library = "bridge"
enabled = true
requires = ["message_store"]

[pipelines.message]
source = "obcx::core::events::RawMessageEvent"

[[pipelines.message.stages]]
name = "persist"
actor = "message_store"
input = "obcx::core::events::RawMessageEvent"
output = "obcx::message_store::events::MessageStored"
mode = "await"

[[pipelines.message.stages]]
name = "forward"
actor = "bridge"
input = "obcx::message_store::events::MessageStored"
output = ["bridge::events::MessageForwarded", "bridge::events::MessageForwardFailed"]
after = ["persist"]
mode = "await"
```

Actor-visible shared services such as `DbManager`, `BotOperationGateway`, and
blocking executors are registered in `ActorServices` and resolved through the
context. Process bot components and their capability directory are not actor
services. Concrete actor packages stay outside the core library.

## Runtime generations and reload

Startup and reload candidates use the same generation builder and validation
order. A candidate is parsed, staged, contract-checked, constructed, and
activated without mutating the active generation. The controller then closes
root ingress, waits for all routes already admitted to the old generation,
atomically publishes the candidate, and reopens ingress. A timeout abandons
only the candidate and reopens the old generation.

Route admission is also the lifetime guard. The admission reference is copied
through downstream routing and terminal asynchronous work, so a suspended
coroutine retains the complete old generation. Destruction of its actors,
scheduler, DSO handles, and staging directory occurs automatically after the
last route descendant releases that reference; explicit early `dlclose` is
not part of reload.

Each actor main DSO is staged under its generation. Actor-private shared
dependencies receive content-versioned filenames and ELF `DT_SONAME`
identities, and every staged `DT_NEEDED` edge is rewritten and verified before
the actor is opened. A distinct directory alone is insufficient because the
ELF loader may reuse an already loaded dependency with the same SONAME.
Process-owned runtime/SDK dependencies may be shared only when their content
identity matches the active generation.

`BotInstallationDirectory`, the shared `BotOperationDispatcher`, and
`DbManager` are process-owned. Every generation receives the same dispatcher
and weak installation capability directory, so a bridge mapping reload
reconstructs actor state while existing installations and transports continue
running. Bot definitions, database instances, and resolved scheduler budgets
are fingerprinted and require restart when changed.

## Package boundary

Every standalone package owns one canonical `actor.toml`, uses
`OBCXActor.cmake`, and exports the symbols emitted by
`OBCX_ACTOR_EXPORT_V2`. The manager requires an explicit numeric ABI value of
2 before looking up the V2 factory, destructor, name, version, and input
contract symbols. GCC 16.1+, C++26 reflection, and Linux x86_64/arm64 are the
only supported authoring and deployment baseline.
Installed SDK conformance tests prove configure, compile, link, install, load,
invoke, and unload behavior.

## Scope

The runtime is in-process message orchestration. It does not provide durable
broker queues, offsets, replay, consumer groups, or cross-process routing.
Those semantics require a separately designed transport and persistence
layer.
