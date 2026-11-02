# package-release-closures Specification

## Purpose
Record implemented runtime dependency isolation and development registry
boundaries. Inventory-driven packaging, final provenance receipts, published
assets and isolated release/load/rollback acceptance remain deferred.

## Requirements

### Requirement: Private dynamic dependencies retain generation isolation
Runtime staging SHALL content-version package-private dynamic dependencies,
rewrite/check required dynamic-link identities and retain them through the
owning actor generation's in-flight lifetime. Libraries SHALL NOT be loaded as
actor instances or upgraded by overwriting a global shared object independently
of the actor package. Incomplete or escaping private closures SHALL fail staging.

#### Scenario: An old invocation overlaps a library upgrade
- **WHEN** an old invocation remains suspended while a new candidate is published
- **THEN** each invocation retains its own generation's private dependency identities until completion

#### Scenario: An operator rolls back a faulty package
- **WHEN** an approved package rollback is prepared
- **THEN** it uses the complete matching artifact/configuration set rather than replacing a shared library under a running generation

### Requirement: Process-owned dependencies remain outside actor-private substitution
SDK, C++ runtime and registered process-owned identities SHALL remain subject
to process identity/ABI gates. They MUST NOT be disguised as private multi-version
libraries to make an incompatible hot reload appear valid. Matching labels and
successful links SHALL NOT be treated as complete C++ ABI proof.

#### Scenario: An update changes a process-owned runtime identity
- **WHEN** a candidate conflicts with the running SDK/runtime identity
- **THEN** reload rejects it and requires a separately coordinated process operation

### Requirement: Registry metadata is shared with build validation
Local registry checking SHALL use canonical core package tooling with actor/
library kind validation and deterministic metadata digests. The development
index SHALL NOT advertise verified binaries, fabricate platform assets or solve
workspace versions. Remote tooling pins, publication and full release inventory
acceptance SHALL remain separately authorized work.

#### Scenario: A library entry has an actor factory field
- **WHEN** library metadata declares an actor entrypoint or wrong artifact kind
- **THEN** canonical validation rejects it consistently with build validation

#### Scenario: Registry tooling is incompatible
- **WHEN** an explicit registry wrapper is given an unsupported tooling protocol
- **THEN** it fails rather than invoking a copied validator or ignoring unknown fields
