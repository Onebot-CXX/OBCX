# declarative-package-resolution Specification

## Purpose
Resolve the explicit current development package graph without legacy manifests,
registry version selection or a dependency-lock workflow.

## Requirements

### Requirement: Workspace selection and source bindings are explicit
Schema-v2 packages.toml SHALL explicitly declare roots, platform, profile and
source/provider bindings. Reachable package IDs SHALL bind uniquely to declared
path or fixed Git commit/subdirectory sources. Tooling MUST NOT guess kind from
a directory, enable unselected roots, supply configuration defaults or search
a registry for an alternative. Current declarations supersede old source-lock
proposals; graphs are disposable evidence, not acceptance inputs.

#### Scenario: A transitive library has no binding
- **WHEN** actor A declares library L without a workspace source binding
- **THEN** resolution fails before package CMake and reports the dependency chain

#### Scenario: Source list order differs from dependency order
- **WHEN** source declarations are reordered
- **THEN** the dependency graph, not declaration order, determines configuration

#### Scenario: A source is not reachable from selected roots
- **WHEN** an unused source is declared
- **THEN** it is not fetched, configured or built

### Requirement: Package identity and dependency kinds are validated
The resolver SHALL validate source ID/kind/version against bindings and incoming
edges, reject duplicate IDs/exports and reserved SDK impersonation, and require
library edges to target libraries and logical actor edges to target actors.
Libraries MUST NOT depend on actor instances or link actor DSOs as libraries.

#### Scenario: A supposed library contains actor metadata
- **WHEN** a library edge resolves to actor metadata
- **THEN** kind validation fails regardless of the artifact suffix

#### Scenario: A provider is accidentally bound under another identity
- **WHEN** binding L contains metadata identifying M
- **THEN** resolution rejects the mismatch rather than rebinding it

### Requirement: Version constraints are evaluated over the complete graph
One explicit version per package ID SHALL satisfy all direct/transitive incoming
constraints. Supported SemVer ordering and comma-intersection constraints SHALL
be evaluated rather than approximated by regex or string order. Unsupported
range syntax and disallowed prereleases MUST fail without selecting another
source/version.

#### Scenario: A diamond dependency is compatible
- **WHEN** one explicit L version satisfies A and B
- **THEN** both consumers use the same graph node/source

#### Scenario: A diamond dependency conflicts
- **WHEN** explicit L cannot satisfy both incoming constraints
- **THEN** diagnostics identify the chains/constraints/version without downloading alternatives

#### Scenario: A pre-release or unsupported range is encountered
- **WHEN** an unsupported range or disallowed prerelease is selected
- **THEN** validation fails rather than guessing its meaning

### Requirement: Cycles and profiles are resolved before build execution
The selected platform/profile SHALL determine the graph before package CMake.
Self-dependencies and cycles SHALL fail with a stable diagnostic and valid graphs
SHALL use stable topological order. Production SHALL exclude test-only edges.
Logical actor dependencies SHALL validate package existence/version without
creating runtime instances or rewriting runtime requires.

#### Scenario: A transitive cycle is present
- **WHEN** A depends on B, B on C and C on A
- **THEN** resolution reports the cycle before executing package CMake

#### Scenario: A production build excludes test helpers
- **WHEN** T is declared only in test_dependencies and production is selected
- **THEN** T is excluded from the selected production graph

### Requirement: Network access and source caches are controlled
Source preparation SHALL use explicit allow/deny policy; offline CMake resolution
MUST NOT download missing objects. Remote sources SHALL require full commits,
restricted subdirectories and verified source/cache identity. Cache publication
SHALL be synchronized/atomic and reject escape, tampering and implicit unpinned
submodule/LFS substitution. Nix lockfiles and cache mutexes are not package locks.

#### Scenario: Offline cache is incomplete
- **WHEN** a required fixed remote source is missing from verified cache
- **THEN** preparation fails without a download or fallback source

#### Scenario: Cached metadata is altered
- **WHEN** cached identity/content fails verification
- **THEN** resolution rejects it before package execution

### Requirement: Dependency resolution is inspectable without executing package code
Canonical data-level checking/resolution/explanation SHALL report nodes, edges,
explicit sources and dependency diagnostics without running package CMake or
starting actors. Errors SHALL identify relevant paths/fields/chains and MUST
NOT reveal credentials. Expanded release receipts/provenance acceptance SHALL
NOT be inferred from development graph generation.

#### Scenario: An operator asks why a library is needed
- **WHEN** the data tooling explains a selected library
- **THEN** the selected source/version and dependency chains are inspectable without compilation or actor invocation
