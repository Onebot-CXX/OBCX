# actor-runtime-operations Specification

## Purpose
TBD - created by archiving change native-actor-coroutine-scheduler. Update Purpose after archive.
## Requirements
### Requirement: Runtime owns one thread-budget policy
The runtime SHALL size actor workers, Asio I/O executors, and blocking/CPU
executors from one explicit thread-budget policy. An automatic value MUST avoid
independently allocating `hardware_concurrency()` threads to every pool. The
resolved `blocking_workers` allocation MUST back exactly one process-owned
`BlockingExecutor` that is registered as a service in the startup generation
and shared unchanged with every reload candidate. A change to the resolved
blocking allocation MUST remain restart-required, and validation-only startup
MUST validate the allocation without creating blocking worker threads.

#### Scenario: Automatic worker sizing
- **WHEN** worker counts are configured as automatic
- **THEN** startup resolves and logs a bounded allocation for actor, I/O, and blocking workers from one runtime budget

#### Scenario: Invalid explicit sizing
- **WHEN** configured worker counts are zero where zero is not automatic or exceed supported runtime limits
- **THEN** startup fails validation with an actionable configuration error

#### Scenario: Startup registers the blocking service
- **WHEN** normal startup resolves a valid runtime thread budget
- **THEN** it creates one blocking executor with the resolved blocking worker count and makes that same service available to actor contexts before ingress

#### Scenario: Reload reuses the process blocking service
- **WHEN** a reload candidate preserves the active process-owned thread fingerprint
- **THEN** the candidate receives the exact active process blocking executor rather than constructing generation-local workers

#### Scenario: Reload changes blocking worker count
- **WHEN** a reload candidate resolves a different blocking worker count
- **THEN** preparation fails with `reload_restart_required` before cutover and leaves the active executor unchanged

#### Scenario: Validation-only startup has no blocking threads
- **WHEN** `--validate-config` validates a configuration with a valid blocking worker allocation
- **THEN** validation succeeds without constructing, starting, or stopping a blocking executor

### Requirement: Scheduler state is observable
The V2 runtime SHALL expose counters or histograms for submissions,
backpressure, runnable/running/suspended mailboxes, queue depths, resume
duration, yields, steal attempts and successes, failures, cancellations, late
completions, and worker sleep/wake activity.

#### Scenario: Forced stealing updates metrics
- **WHEN** a stress test creates skew and an idle worker successfully steals a continuation
- **THEN** steal-attempt, steal-success, and relevant queue-depth metrics reflect the event

#### Scenario: Suspended mailbox is visible
- **WHEN** an actor is awaiting I/O
- **THEN** operational state reports it as suspended rather than runnable or actively consuming an actor worker

### Requirement: Diagnostics protect message contents
Scheduler diagnostics SHALL include task id, actor id, partition key, mailbox
generation, worker id, and elapsed timing where applicable. They MUST NOT log
message payloads by default.

#### Scenario: Slow actor warning
- **WHEN** an actor resume exceeds the configured warning threshold
- **THEN** a diagnostic identifies the execution context and duration without including payload or raw message fields

### Requirement: Concurrency verification is deterministic and stressable
The test suite SHALL include deterministic forced-placement tests for stealing
and forced interleavings for enqueue, completion, cancellation, and shutdown.
Timing alone MUST NOT be the only condition used to prove a steal occurred.

#### Scenario: Forced same-home-worker skew
- **WHEN** a test assigns multiple runnable mailboxes to one worker and holds another worker idle
- **THEN** the test deterministically observes a successful steal and validates exactly-once execution

#### Scenario: Repeated completion race
- **WHEN** seeded stress repeatedly races I/O completion, cancellation, and scheduler shutdown
- **THEN** no accepted invocation is lost, completed twice, resumed after destruction, or left hanging

### Requirement: Rollout is gated by recorded baselines
Before the actor-only removal phase completes, native scheduling SHALL pass the
recorded correctness, balanced-load, skewed-load, I/O-heavy,
completion-storm, shutdown, CPU, and memory acceptance thresholds. The
balanced-load threshold previously missed by native-v2 MUST pass on the
designated reproducible benchmark environment. Once the actor-only release is
built, it SHALL contain no secondary engine for runtime rollback.

#### Scenario: Native runtime misses a cutover threshold
- **WHEN** any required correctness or performance threshold fails
- **THEN** the actor-only cutover remains incomplete and the removal phase is not eligible for release

#### Scenario: Native runtime passes every cutover threshold
- **WHEN** all recorded root-owned gates and generic installed-SDK actor integration tests pass
- **THEN** the removal phase may complete and the native scheduler becomes the only shipped engine

#### Scenario: Released runtime needs operational rollback
- **WHEN** an operator must roll back after deploying the actor-only release
- **THEN** recovery uses deployment of the preceding OBCX release rather than an alternate engine in the actor-only binary

### Requirement: Runtime uses one native actor engine
The actor-only runtime SHALL construct and use the native ActorTask scheduler
as its sole actor engine. Runtime configuration SHALL NOT expose an actor
engine selector, and the release SHALL contain no Asio-v1 scheduler or
in-process engine fallback.

#### Scenario: Runtime starts actor scheduling
- **WHEN** OBCX starts with valid actor and pipeline configuration
- **THEN** all actor invocations execute through the native scheduler without selecting an engine

#### Scenario: Runtime artifacts are inspected
- **WHEN** production binaries, installed libraries, and configuration schemas are inspected
- **THEN** no Asio-v1 engine implementation, selector, or rollback probe is present

### Requirement: Runtime extension lifecycle is actor-only
The runtime SHALL discover, construct, dispatch, reload, and shut down
extensions only through the supported V2 actor contract and actor pipelines.
Production and SDK artifacts SHALL contain no `IPlugin`, `PluginManager`,
plugin lifecycle callback, plugin export symbol, plugin reload command, or
plugin loader branch. Operator lifecycle entry points, including `reload`, MUST
operate exclusively through the V2 actor runtime and MUST NOT provide plugin
compatibility behavior.

#### Scenario: Runtime starts configured extensions
- **WHEN** OBCX starts with valid actor and pipeline configuration
- **THEN** it loads V2 actors without constructing or consulting a plugin manager

#### Scenario: Retired runtime surfaces are audited
- **WHEN** production sources, binaries, public headers, and CLI commands are inspected
- **THEN** no plugin lifecycle or plugin reload entry point is present and any actor-runtime reload entry point operates exclusively through V2 actor generations

### Requirement: Retired extension inputs receive no special handling
Runtime and build configuration SHALL consume only the supported actor
configuration and canonical actor package metadata. They SHALL contain no
plugin-specific parser, accessor, dependency extraction, detector, translator,
migration warning, or automatic rewrite for `[plugins]`, `plugins.toml`, or
`plugin.toml`.

#### Scenario: Supported actor input is processed
- **WHEN** actor runtime configuration or canonical actor package metadata is supplied
- **THEN** the owning runtime or build tool processes it without consulting a plugin input

#### Scenario: Retired plugin input is present
- **WHEN** an otherwise unused TOML key or file resembles a retired plugin input
- **THEN** only generic parser behavior applies and no plugin-specific diagnostic or migration path runs

### Requirement: Startup validates pipelines against loaded actor contracts
Runtime startup SHALL parse and syntactically validate configuration, load the
enabled actor libraries and input contracts, and perform actor-aware pipeline
validation before creating scheduler workers, services, bots, or ingress.
Actor-aware validation MUST reject a stage whose actor is unavailable or whose
configured input is absent from that actor's accepted-input contract.

#### Scenario: Stage input is supported
- **WHEN** every configured stage references a loadable enabled actor and one of its accepted canonical inputs
- **THEN** actor-aware validation succeeds and runtime activity may be created

#### Scenario: Actor does not accept stage input
- **WHEN** a stage configures a canonical input that is absent from its actor's input contract
- **THEN** startup fails before scheduler workers or ingress start and identifies the pipeline, stage, actor, and input

#### Scenario: Actor library cannot supply a contract
- **WHEN** an enabled actor library fails ABI or input-contract loading
- **THEN** startup fails before runtime activity starts and no actor is partially registered

### Requirement: Validation-only CLI has no runtime side effects
The CLI SHALL provide `obcx --validate-config <config>` and SHALL run the same
configuration parsing, actor-library loading, input-contract validation, and
pipeline validation used by normal startup. It MUST exit after validation
without constructing actor workers, starting services or bots, or opening
ingress.

#### Scenario: Configuration is valid
- **WHEN** validation-only mode receives a valid deployable configuration and actor library set
- **THEN** it exits successfully after unloading validation resources without starting runtime activity

#### Scenario: Configuration is invalid
- **WHEN** validation-only mode encounters an actor contract or pipeline validation error
- **THEN** it exits unsuccessfully with the same actionable diagnostic normal startup would produce

### Requirement: Pipeline validation is local and deterministic
Pipeline validation SHALL check unique stage names, existence of `after`
references, acyclicity of the explicit `after` dependency graph, actor
availability and accepted inputs, and existing scheduler/database/service
constraints. It SHALL NOT infer actor output sets, input/output pairs, global
message reachability, business-branch feasibility, or a static message-flow
graph from pipeline `output` declarations.

#### Scenario: Explicit dependency graph contains a cycle
- **WHEN** configured `after` edges form a directed cycle
- **THEN** validation rejects the pipeline with the participating stage path

#### Scenario: Output declaration cannot prove reachability
- **WHEN** stage inputs and explicit dependencies are locally valid but an `output` declaration does not prove a realizable business path
- **THEN** local validation does not claim or attempt a global dataflow proof

#### Scenario: Stage name or dependency is invalid
- **WHEN** a pipeline repeats a stage name or references a missing `after` stage
- **THEN** validation rejects the pipeline before runtime activity starts

### Requirement: Executed routes carry branch-local cycle context
Each executed message route SHALL carry an internal hop count and ancestor
sequence of `(pipeline, stage, message_type)`. Entering an ancestor node already
present in the same route MUST terminate that branch with
`message_routing_cycle`. Fan-out branches MUST receive independent trace state,
and terminal asynchronous routing MUST retain its trace across suspension.

#### Scenario: Actual feedback route repeats an ancestor
- **WHEN** emitted messages route back to the same pipeline, stage, and canonical message type within one branch
- **THEN** that branch terminates with `message_routing_cycle` and a bounded trace of the repeated route

#### Scenario: Sibling branch visits the same node
- **WHEN** two fan-out siblings independently visit the same route node without either repeating its own ancestor
- **THEN** neither sibling is rejected because of the other sibling's trace

#### Scenario: Terminal asynchronous route resumes
- **WHEN** a terminal asynchronous actor result suspends and later emits a message
- **THEN** descendant routing continues with the originating branch's hop and ancestor context

### Requirement: Executed routes fail explicitly at the hop limit
The runtime SHALL enforce a bounded routing hop limit with a default of 32.
Exceeding the effective limit MUST terminate the branch with
`message_routing_hop_limit`; the orchestrator MUST NOT silently return or drop
the message because of routing depth.

#### Scenario: Non-repeating route exceeds the limit
- **WHEN** an executed route traverses more nodes than its effective hop limit without repeating an ancestor tuple
- **THEN** the branch terminates with `message_routing_hop_limit` and identifies the route context

#### Scenario: Route reaches the last permitted hop
- **WHEN** a route completes at or below its effective hop limit
- **THEN** the runtime preserves normal result and failure behavior

### Requirement: Dispatch and routing diagnostics protect message contents
Dispatch and routing failures SHALL use stable codes including
`unsupported_message_type`, `invalid_message_payload`,
`message_routing_cycle`, and `message_routing_hop_limit`. Diagnostics SHALL
include available actor, pipeline, stage, canonical type, message or correlation
identity, and bounded route information, and MUST NOT include complete message
payloads by default.

#### Scenario: Routing cycle is reported
- **WHEN** an executed route terminates because it repeats an ancestor node
- **THEN** logs and failure state use `message_routing_cycle` with bounded routing identity and no complete payload

#### Scenario: Invalid payload is reported
- **WHEN** actor dispatch cannot deserialize a supported message payload
- **THEN** logs and failure state use `invalid_message_payload` with actor and message identity but no complete payload
