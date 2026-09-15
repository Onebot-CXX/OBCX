## ADDED Requirements

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
