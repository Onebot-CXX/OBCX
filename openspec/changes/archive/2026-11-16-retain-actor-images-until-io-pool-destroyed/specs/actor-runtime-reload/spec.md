## MODIFIED Requirements

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

#### Scenario: Actor I/O services retain actor code
- **WHEN** an actor created Asio I/O objects on the generation's actor I/O executor
- **THEN** OBCX MUST keep that actor's image loaded until the actor I/O execution context and its services have been destroyed, on every platform
