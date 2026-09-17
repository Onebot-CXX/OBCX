# Actor ABI 2 Author Guide

Status: current standalone actor guide (2026-07-30)

## Package layout

A standalone package contains one canonical metadata document, a CMake entry,
and actor sources:

```text
actor.toml
CMakeLists.txt
src/example_actor.hpp
src/example_actor.cpp
tests/...
```

Start from `local_actor/obcx-actor-template`. `actor.toml` declares identity,
semantic version, ABI 2, artifact/target names, package and actor
dependencies, supported OBCX range, repository, license, description, and only
the release platforms whose binaries were built and verified. Unknown or
missing fields fail validation; an undeclared platform cannot resolve from the
actor registry.

## CMake contract

```cmake
cmake_minimum_required(VERSION 3.30)
project(example_actor VERSION 0.1.0 LANGUAGES CXX)

find_package(obcx-sdk CONFIG REQUIRED)
include(OBCXActor)

obcx_add_actor(example
  SOURCES src/example_actor.cpp
  OUTPUT_NAME example)
```

`artifact.target` must equal `example_actor`, and `artifact.name` must equal
`example`. Installation places the library under `lib/obcx/actors` and
metadata under `share/obcx/actors/<actor-id>/actor.toml`.

## C++ contract

```cpp
#include <core/actor/reflected_actor.hpp>

namespace example::events {
struct Requested { std::string text; };
struct Handled { std::string text; };
void from_json(const obcx::common::json&, Requested&);
void to_json(obcx::common::json&, const Handled&);
}

class ExampleActor final
    : public obcx::core::ReflectedActor<ExampleActor> {
public:
  static constexpr std::string_view actor_name = "example";
  static constexpr std::string_view actor_version = "0.1.0";

  auto handle(const example::events::Requested &request,
              const obcx::core::MessageEnvelope &message,
              obcx::core::ActorContext &context)
      -> obcx::core::ActorResult {
    context.throw_if_cancelled();
    auto result = obcx::core::ActorResult::success();
    result.emit(example::events::Handled{request.text}, message);
    return result;
  }
};

OBCX_ACTOR_EXPORT_V2(ExampleActor)
```

The export macro supplies the numeric ABI, factory, destructor, name, version,
and generated schema-2 input contract. The compiler rejects inherited,
non-public, malformed, duplicate, or JSON-inconvertible handler inputs. Wire
identity is exactly the fully qualified C++ type name; aliases are removed.
Do not export a second factory or hand-written contract from the same library.

## Asio interop

Use `await_asio` when an existing service returns `boost::asio::awaitable<T>`:

```cpp
auto response = co_await context.await_asio(
    io_executor,
    [&service]() -> boost::asio::awaitable<std::string> {
      co_return co_await service.read();
    });
```

The factory must own or safely reference everything needed until completion.
Cancellation can destroy the actor frame before the nested I/O operation
finishes, so callbacks must not retain raw references to frame-local state.
For completion-token APIs, pass `context.asio_token(executor)`.

## Blocking and CPU-heavy work

Use `ActorContext::run_blocking` for a synchronous database, filesystem, HTTP,
or CPU-heavy call:

```cpp
auto records = co_await context.run_blocking(
    [repository, query] { return repository->fetch_context(query); });
```

The callable runs on the process `BlockingExecutor`; the actor continuation is
republished through `NativeActorScheduler`. It must return a value or `void`,
not a reference or another awaitable. Exceptions are rethrown at the
`co_await` expression.

If a generation-tracked `await_asio` coroutine contains nested synchronous
work, resolve the service before entering the nested graph:

```cpp
auto blocking = context.get_service<obcx::core::BlockingExecutor>();
co_await context.await_asio(
    io_executor,
    [blocking]() -> boost::asio::awaitable<void> {
      co_await blocking->run([] { perform_synchronous_cleanup(); });
    });
```

Do not retain a raw `ActorContext` reference in the nested coroutine and do not
detach work from the tracked actor operation. A missing service fails with
`BlockingExecutorUnavailable`; the runtime never creates a fallback pool.

Suspension releases an actor worker but retains exclusive ownership of the
current `actor_id + partition_key` mailbox. Split independent conversations or
state domains into different partition keys; same-partition work remains FIFO.
Cancellation suppresses a late actor resume but cannot terminate a synchronous
function already running, so external operations still need bounded domain
timeouts.

## Services and protocol capabilities

Resolve shared runtime services with `context.get_service<T>()`. Bot egress
uses the installed `BotOperationGateway` plus data-only operation contracts;
actor packages must not include process component, provider, transport, or
connection-manager implementation headers.

Database-aware actors read `context.db_instance()` and
`context.db_namespace()`. Pipeline partition keys determine the mailbox
ordering boundary and should be stable for the business entity being
serialized.

## Generation-scoped configuration

Read actor-owned settings through the context passed to a handler:

```cpp
auto view = context.config();
auto endpoint = view.get_value<std::string>("endpoint").value_or("local");
auto rules = view.get_section("rules");
```

`get_value("endpoint")` resolves
`actors.<actor_name>.config.endpoint` from the immutable snapshot that built
the current actor generation. A running old generation keeps its old view
while a reload candidate receives the candidate snapshot. If an actor needs a
derived configuration object, construct it on first handler activation and
store it on that actor instance.

Reloadable actor code must not call `ConfigLoader::instance()`, cache mutable
configuration in namespace globals or function statics, read the process
environment on each message, or initialize configuration in its no-context
factory constructor. Process-owned bot connections and database instances are
provided as services and require a process restart when changed.

## Reload-safe actor design

A reload constructs a new actor instance; it does not migrate arbitrary
in-memory fields from the old instance. Put state that must survive in a
configured persistent service, and make construction/activation free of
external side effects that cannot be discarded with a failed candidate.

All asynchronous work launched for a message must remain attached to its
`ActorTask`, an emitted descendant, or a terminal stage tracked by the
orchestrator. Do not detach callbacks that capture the actor, its DSO code,
`ActorContext`, or generation-owned services. The runtime retains an old
generation while tracked work is suspended, but it cannot make an untracked
raw callback safe.

Actor-private shared libraries may keep a conventional source SONAME. Package
the complete relative closure with the actor; the runtime assigns each staged
private dependency a content-versioned dynamic-link identity and rewrites all
consumer edges. Do not load private dependencies manually, store process-wide
function pointers into them, or assume an in-place library overwrite is a
deployment boundary.

To test reloadability, keep bot/database/thread configuration identical,
change only actor-owned tables or actor binaries, hold one old invocation at a
deterministic Asio gate, prepare the candidate, then prove the old invocation
finishes with old behavior and post-cutover invocations use only new behavior.
Run the same test against a clean installed SDK and installed actor artifacts.

## Package verification

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/obcx/install
cmake --build build --parallel
ctest --test-dir build --output-on-failure
cmake --install build --prefix /tmp/example-package
```

Core CI additionally loads the installed library through `ActorManager`,
invokes it, and unloads it. Registry publication runs the same canonical
metadata validator before generating an index entry.

Deploy actors and their private dependencies as an immutable, complete set.
If a new set passes preparation but has incorrect business behavior, restore
the preceding set and its matching actor-owned configuration and reload again.
Changes to live bot connections, database definitions, or runtime thread
budgets require a process restart rather than actor reload.
