# actor-package-ecosystem Specification

## Purpose
Define the current schema-v2 package metadata, installed actor tooling and
kind-aware development registry shared by actors and ordinary libraries.

## Requirements

### Requirement: Actor packages use one canonical metadata contract
Every standalone actor package SHALL declare schema-v2 package.toml with an
explicit package.kind discriminator, identity/version, ABI/input contract,
artifact, typed dependencies, publication and compatibility metadata. Required
fields and empty dependency lists SHALL be explicit; unknown fields, actor.toml
and old workspace actors.toml SHALL be rejected without compatibility defaults.
Current declarations, not a package lock or registry solver, SHALL select builds.

#### Scenario: Standalone actor declares dependencies
- **WHEN** a schema-v2 actor enters the selected build graph
- **THEN** tooling checks its declared library, logical actor and system edges against explicit source/provider bindings

#### Scenario: Actor metadata omits a required field
- **WHEN** package.toml lacks required identity, ABI, artifact, dependency, compatibility or publication data
- **THEN** validation reports the field and inserts no implicit value

#### Scenario: Actor platform was not built and verified
- **WHEN** development metadata is indexed without verified release assets
- **THEN** no download URL or deployable platform artifact is fabricated

#### Scenario: An old package format is supplied
- **WHEN** a caller supplies only actor.toml or workspace actors.toml
- **THEN** tooling requires current package.toml/packages.toml rather than converting it

### Requirement: Installed SDK builds V2 actor packages
The installed SDK SHALL provide public headers, its matching imported core,
ABI V2 export helpers, canonical schema-v2 parser and CMake graph/audit tooling.
It SHALL support declared ordinary libraries without treating them as runtime
actors. Workspace and installed builds SHALL use explicit current declarations
and the same supported dependency/toolchain checks. A successful development
build MUST NOT be advertised as full release or runtime acceptance.

#### Scenario: Clean external actor build
- **WHEN** a standalone V2 actor and declared libraries use the installed SDK with explicit dependencies
- **THEN** configuration and compilation do not require core-private source headers or an old metadata helper

#### Scenario: Installed surface is actor-only
- **WHEN** installed runtime extension interfaces are inspected
- **THEN** they expose ABI V2 actors, not plugin/V1/Asio-v1 factories; ordinary library helpers add no runtime factory

### Requirement: Active project surfaces use actor terminology
Runtime extension surfaces SHALL use actor terminology. General build and
registry surfaces SHALL use package terminology for actor/library kinds.
Historical notes MAY identify removed names, but active tooling SHALL NOT
provide an old-format reader or plugin ABI entry point.

#### Scenario: Active surface audit
- **WHEN** current source and installed interfaces are inspected
- **THEN** runtime extension paths identify actors and generic metadata paths identify packages without active plugin factories

### Requirement: Package registry distinguishes actors from ordinary libraries
The local development registry SHALL validate schema-v2 actor/library records
using the canonical core package tooling rather than a copied validator. Kind,
artifact, dependency and compatibility rules SHALL agree with local builds.
The deterministic index SHALL identify development-metadata-only status and
source metadata digests without inventing download assets or published tooling
pins. Registry entries SHALL NOT select workspace dependency versions.

#### Scenario: A valid library is indexed
- **WHEN** a valid library record is supplied to canonical registry tooling
- **THEN** its kind and exports can be indexed without actor ABI/factory fields

#### Scenario: A package kind and artifact disagree
- **WHEN** actor metadata declares a static artifact or library metadata declares an actor factory
- **THEN** canonical registry and build validation reject it consistently

#### Scenario: A package is absent from explicit workspace bindings
- **WHEN** a compatible registry record exists but the workspace has no required source binding
- **THEN** resolution fails rather than choosing the registry record
