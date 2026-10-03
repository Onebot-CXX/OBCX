# actor-package-ecosystem Specification

## Purpose
TBD - created by archiving change actor-only-runtime-cutover. Update Purpose after archive.
## Requirements
### Requirement: Actor packages use one canonical metadata contract
Every standalone actor package SHALL declare its build and publication
metadata in `actor.toml`. The contract SHALL include actor identity and
semantic version, the supported numeric V2 ABI generation, artifact identity,
the explicitly built and verified release platforms, dependencies, publication
data, and supported OBCX/ABI ranges. OBCX build and packaging tools SHALL
derive actor dependencies and publishable platform assets exclusively from
that contract.

#### Scenario: Standalone actor declares dependencies
- **WHEN** an actor package with canonical actor metadata is included in a build
- **THEN** the build tooling resolves its declared dependencies without consulting a plugin manifest

#### Scenario: Actor metadata omits a required field
- **WHEN** `actor.toml` lacks required identity, ABI, artifact, dependency, or publication data
- **THEN** actor package validation rejects it with a field-specific error

#### Scenario: Actor platform was not built and verified
- **WHEN** a registry or release process resolves a platform absent from `artifact.platforms`
- **THEN** no download entry or release asset is generated for that platform

### Requirement: Installed SDK builds V2 actor packages
The installed OBCX SDK SHALL expose only the public headers, libraries, export
helper, and CMake functions needed to build and load an `IActorV2` package.

#### Scenario: Clean external actor build
- **WHEN** a standalone V2 actor is configured against a clean OBCX installation
- **THEN** it compiles, links, installs, loads, and handles an invocation using the actor SDK alone

#### Scenario: Installed surface is actor-only
- **WHEN** the installed include and CMake package contents are audited
- **THEN** no plugin SDK, V1 actor helper, or Asio-v1 build target is exported

### Requirement: Actor registry accepts actor packages only
The package registry SHALL publish an actor-named index whose schema describes
V2 actor identity, version, artifact, metadata, and supported OBCX/ABI ranges.
Its generator and validation workflow SHALL operate only on actor package
entries and SHALL NOT invent artifacts for undeclared platforms.

#### Scenario: Valid actor is indexed
- **WHEN** a repository submits a valid actor package entry
- **THEN** registry validation publishes it in the generated actor index

#### Scenario: Entry lacks actor package fields
- **WHEN** a registry entry does not satisfy the actor package schema
- **THEN** registry validation rejects it without applying plugin-schema compatibility rules

### Requirement: Active project surfaces use actor terminology
Active project surfaces MUST use actor terminology. This includes current
source layout, examples, package names, CMake entry points, registry artifacts,
and user documentation for runtime extensions. Historical records and explicit
breaking-change notes MAY retain the word plugin where needed to identify
removed behavior.

#### Scenario: Active surface audit
- **WHEN** the release source tree and installed artifacts are audited
- **THEN** extension-facing paths and identifiers use actor terminology and expose no active plugin entry point
