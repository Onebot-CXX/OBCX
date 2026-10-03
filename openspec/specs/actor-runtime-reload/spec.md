# actor-runtime-reload Specification

## Purpose
TBD - created by archiving change restore-actor-runtime-reload. Update Purpose after archive.
## Requirements
### Requirement: Operators can request an actor-runtime reload
OBCX SHALL expose a `reload` command through both the TUI and `--no-tui`
operator command interfaces. The command MUST reload the root configuration
path selected at process startup and MUST use the same reload coordinator in
both interfaces.

#### Scenario: Reload from the TUI
- **WHEN** an operator submits `reload` in the TUI command input
- **THEN** OBCX starts an actor-runtime reload for the active root configuration and reports its result in the TUI log

#### Scenario: Reload without the TUI
- **WHEN** an operator submits `reload` on standard input while OBCX runs with `--no-tui`
- **THEN** OBCX starts the same actor-runtime reload operation and reports its result on the normal logging surface

#### Scenario: Reload is already running
- **WHEN** another `reload` command is submitted while a reload transaction is active
- **THEN** OBCX rejects the duplicate request with a reload-busy diagnostic and does not start a second transaction

### Requirement: Candidate preparation does not mutate the active runtime
OBCX SHALL parse a candidate configuration, stage candidate actor libraries,
discover their V2 contracts, and run the same actor-aware validation used at
startup before quiescing ingress or changing active configuration. Candidate
preparation MUST NOT mutate the active generation or its configuration
snapshot.

#### Scenario: Candidate TOML is invalid
- **WHEN** the active configuration file has a syntax or configuration error when `reload` is requested
- **THEN** reload fails during preparation and the current generation continues accepting and processing messages with its prior snapshot

#### Scenario: Candidate actor graph is invalid
- **WHEN** a candidate actor is unavailable, exports an invalid contract, cannot be constructed, or does not accept a configured pipeline input
- **THEN** reload fails before ingress is quiesced and the current actor graph remains active

#### Scenario: Candidate preparation succeeds
- **WHEN** the candidate configuration, V2 actor contracts, actor construction, and actor-aware pipeline validation all succeed
- **THEN** OBCX marks the candidate generation ready before beginning the cutover

### Requirement: Actor-owned configuration is generation-scoped
Each runtime generation SHALL own an immutable configuration snapshot. Actors
MUST obtain configuration from their generation rather than a process-global
mutable TOML object, and actor instances in different generations MUST NOT
observe one another's snapshots.

#### Scenario: Bridge mapping changes
- **WHEN** a valid reload changes a bridge group mapping
- **THEN** messages admitted after the cutover use the new mapping without restarting bot connections

#### Scenario: Work predating the cutover
- **WHEN** an actor invocation was admitted before a successful reload cutover
- **THEN** that invocation and all of its routed descendants complete against the old generation and its old configuration snapshot

#### Scenario: Candidate is abandoned
- **WHEN** a prepared candidate does not complete cutover
- **THEN** no actor in the current generation observes any value from the candidate snapshot

### Requirement: Reload has one lossless ingress cutover boundary
OBCX SHALL close a root-ingress gate, allow all work accepted by the old
generation and its routed descendants to drain, atomically publish the
candidate generation, and then reopen ingress. Every root message accepted by
the reload gate MUST be submitted to exactly one generation.

#### Scenario: Message arrives before the boundary
- **WHEN** a root message passes the ingress gate before it is closed for reload
- **THEN** the complete route executes exactly once on the old generation before that generation is retired

#### Scenario: Message arrives during cutover
- **WHEN** a root message reaches the closed ingress gate during a reload cutover
- **THEN** its submission waits without entering either actor scheduler and proceeds exactly once on the generation selected when the gate reopens

#### Scenario: Cutover completes
- **WHEN** the old generation drains before the configured reload deadline
- **THEN** OBCX publishes the candidate as one atomic generation change and routes all subsequently admitted messages to it

#### Scenario: Drain deadline expires
- **WHEN** the old generation does not drain before the configured reload deadline
- **THEN** OBCX aborts the cutover, reopens ingress on the old generation, discards the candidate, and reports a reload-drain-timeout failure

### Requirement: Actor library generations have safe lifetimes
Candidate V2 actor libraries SHALL be loaded as generation-specific images.
OBCX MUST retain each retired actor object and dynamic-library handle until no
scheduled, suspended, routed, or service-held reference can execute code from
that generation. Before loading a candidate, OBCX MUST give every
actor-private shared dependency a content-versioned dynamic-link identity and
rewrite its staged consumers to request that identity; placing an unchanged
SONAME in a different directory MUST NOT be treated as isolation. A candidate
whose dependency closure cannot be assigned and verified unique identities
MUST be rejected without changing the active generation.

#### Scenario: Actor binary was rebuilt
- **WHEN** a valid reload resolves an actor library whose contents differ from the active generation
- **THEN** the candidate uses the staged new image while the active generation continues using its existing image until cutover and retirement

#### Scenario: Actor-private dependency was rebuilt under the same original SONAME
- **WHEN** the candidate contains an actor-private dependency whose content differs from the active generation but whose source package supplied the same original SONAME
- **THEN** staging assigns the dependency and all closure edges a content-versioned identity, the active actor continues executing the old dependency, and the candidate actor executes the rebuilt dependency

#### Scenario: Unique dependency identity cannot be established
- **WHEN** candidate staging cannot rewrite or verify a changed actor-private dependency and every consumer edge before loading it
- **THEN** reload fails with `reload_dependency_identity_conflict` before opening the candidate actor and leaves the active generation untouched

#### Scenario: Candidate actor activation fails
- **WHEN** construction or activation of an actor from a staged image fails
- **THEN** OBCX unloads only candidate resources and continues running the active actor image

#### Scenario: Suspended actor work still references old code
- **WHEN** an old-generation actor task is suspended on Asio during reload
- **THEN** OBCX does not unload that actor library until the task reaches a terminal state and releases its generation reference

### Requirement: Process-owned configuration changes require restart
The initial reload contract SHALL support actor entries, actor-owned
configuration, pipelines, and routing policy. It MUST reject changes to bot
identity or transport, database instance definitions, and resolved scheduler
thread budgets before cutover with a stable restart-required diagnostic; it
MUST NOT apply a supported subset of an otherwise rejected candidate.

#### Scenario: Only actor-owned configuration changes
- **WHEN** a candidate changes bridge mappings or other actor-owned values without changing a restart-required field
- **THEN** the candidate remains eligible for reload

#### Scenario: Bot transport changes
- **WHEN** a candidate changes a bot endpoint, token, proxy, connection type, enabled state, or identity
- **THEN** reload is rejected as restart-required before ingress is quiesced

#### Scenario: Database or thread budget changes
- **WHEN** a candidate changes database instance definitions or the resolved actor, actor-I/O, or blocking worker counts
- **THEN** reload is rejected as restart-required and no candidate values become active

### Requirement: Live bot lookup remains process-owned across generations
OBCX SHALL keep live bot connections and their populated `BotRegistry`
process-owned. Startup and every candidate generation MUST register the same
process-owned registry reference in actor services; a generation builder MUST
NOT create, clone, clear, or repopulate a generation-local registry during
reload.

#### Scenario: Candidate generation becomes active
- **WHEN** a reload cuts over after bots have already been started and registered
- **THEN** actors in the new generation resolve and forward through the same still-running bot instances without re-registering or reconnecting them

#### Scenario: Candidate preparation inspects bot availability
- **WHEN** the candidate builder validates a configured bot identity before readiness
- **THEN** it resolves the identity against the process-owned registry without mutating the registry or the active generation

### Requirement: Reload state is observable without exposing contents
OBCX SHALL report a monotonically increasing reload attempt identifier, active
generation identifier, phase, outcome, duration, drain duration, and stable
failure code. Reload diagnostics and metrics MUST NOT include message payloads,
bot tokens, proxy credentials, or complete configuration values.

#### Scenario: Reload succeeds
- **WHEN** a reload completes successfully
- **THEN** diagnostics identify the attempt, old generation, new generation, changed runtime domains, and bounded phase timings

#### Scenario: Reload fails
- **WHEN** a reload fails during parse, validation, staging, activation, immutable-field comparison, or drain
- **THEN** diagnostics identify the attempt and stable failure phase/code without logging secret configuration values

### Requirement: Reload remains actor-only
The reload implementation SHALL discover, construct, swap, and retire
extensions only through the supported V2 actor contract. Production and SDK
artifacts MUST NOT reintroduce `IPlugin`, `PluginManager`, plugin lifecycle
callbacks, plugin manifests, or a plugin loader/reload branch.

#### Scenario: Reload implementation is audited
- **WHEN** runtime sources, public headers, binaries, and command handlers are inspected
- **THEN** `reload` is implemented by the V2 actor runtime and no retired plugin surface is present

### Requirement: Reload gate wakeups survive waiter registration races

Opening or aborting the reload ingress gate SHALL wake every published waiter
even when the wake operation executes immediately before that waiter's
asynchronous timer wait is registered. Waiting ingress MUST recheck the
authoritative gate and shutdown state after every wakeup.

#### Scenario: Gate opens before asynchronous wait registration

- **WHEN** ingress publishes its waiter, the reload gate opens, and the posted wake executes before ingress initiates the asynchronous wait
- **THEN** the subsequent wait completes immediately and the message proceeds against the generation selected by the reopened gate

#### Scenario: Gate opens after asynchronous wait registration

- **WHEN** ingress is already suspended on its waiter when the reload gate opens
- **THEN** the waiter is woken and the message proceeds exactly once after rechecking gate state

### Requirement: Repeated reload preserves observable bot ingress

Actor-runtime reload SHALL preserve real bot event ingress across repeated
staged actor generations. A message submitted before gate closure, while the
gate is closed, or after cutover MUST produce a terminal orchestrator result,
and a non-success result MUST be reported with structured routing and actor
failure details rather than discarded.

#### Scenario: Messages straddle a successful repeated reload

- **WHEN** bot messages arrive during candidate preparation, while the ingress gate is closed for drain, and after a later-generation cutover
- **THEN** all messages complete exactly once on the generation selected at their admission boundary and supported reflected inputs reach that generation's handlers

#### Scenario: Actor routing returns a failure

- **WHEN** bot ingress completes without throwing but its orchestrator result contains one or more actor failures
- **THEN** OBCX logs the platform, bot identity, failure count, first failing pipeline, stage, actor, stable code, retryability, and safe message without logging the message payload

### Requirement: Generation lifecycle gates actor-owned background work
OBCX SHALL provide each runtime generation with a lifecycle service that accepts actor-owned start and stop callbacks during candidate preparation without starting background work. The service SHALL create a fresh validity token only after the generation becomes active, invalidate that token before invoking stop callbacks, and admit valid-token work through leases that participate in the generation's existing drain accounting. Core MUST NOT own actor scheduling policy or timers.

#### Scenario: Prepared candidate remains inactive
- **WHEN** an actor subscribes lifecycle callbacks while its candidate generation is being prepared or validated
- **THEN** no start callback runs and the candidate cannot acquire a background-work lease before active publication

#### Scenario: Published generation activates
- **WHEN** a prepared generation is atomically published as active
- **THEN** its lifecycle start callbacks receive a fresh valid token and may arrange actor-owned background work

#### Scenario: Reload begins draining
- **WHEN** reload closes ingress and starts draining the active generation
- **THEN** OBCX invalidates the generation token before invoking stop callbacks, rejects new leases for that token, and waits for existing work leases to retire

#### Scenario: Drain timeout retains the old generation
- **WHEN** reload aborts because the old generation does not drain before its deadline
- **THEN** OBCX reactivates the retained generation with a fresh token rather than making the invalid token valid again

#### Scenario: Candidate cutover succeeds
- **WHEN** the old generation drains and the candidate is published
- **THEN** OBCX retires the old lifecycle callbacks before unloading old actor code and activates background work only for the published candidate

#### Scenario: Process shutdown begins
- **WHEN** OBCX begins runtime shutdown
- **THEN** it invalidates active background work, retires lifecycle callback closures, and retains generation code until admitted callbacks have completed

### Requirement: Restart constraints remain process-owned across generations
OBCX SHALL expose one process-owned restart-constraint registry to runtime generations. The registry SHALL compare candidate values by exact actor and key identity without mutating the published value and SHALL retain explicitly published values across generation replacement. It MUST NOT embed actor-specific policy, configuration defaults, or credentials.

#### Scenario: Candidate checks an unchanged value
- **WHEN** candidate preparation compares an actor/key value equal to the value retained by the registry
- **THEN** the registry reports compatibility without changing its stored value

#### Scenario: Candidate checks a changed value
- **WHEN** candidate preparation compares a different value for an existing exact actor/key pair
- **THEN** the registry reports incompatibility so the caller can require process restart

#### Scenario: Actor and key boundaries differ
- **WHEN** two constraints have text that could collide under naive concatenation but different actor or key boundaries
- **THEN** the registry treats them as distinct exact identities
