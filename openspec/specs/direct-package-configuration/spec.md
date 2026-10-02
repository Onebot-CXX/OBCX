# direct-package-configuration Specification

## Purpose
Derive package builds directly from explicit current declarations, resolve offline inside CMake, and retain source, provider and build safeguards without a dependency-lock workflow.

## Requirements

### Requirement: Current declarations are the only package-resolution authority
The package resolver SHALL compute the selected graph from the explicit workspace and current package metadata without reading, writing or requiring a dependency lock. It MUST preserve identity, source, version, dependency-cycle, profile and platform checks.

#### Scenario: Valid metadata changes require no relock
- **WHEN** a selected package changes its version within all consumer constraints or the workspace explicitly changes profile
- **THEN** resolution succeeds with a newly derived graph without an acceptance snapshot

#### Scenario: Invalid dependencies still fail
- **WHEN** a current declaration has a missing source, incompatible version, wrong kind, conflicting target or cycle
- **THEN** resolution fails with the dependency diagnostic rather than selecting an alternative

#### Scenario: Obsolete lock interfaces are removed
- **WHEN** a caller requests the lock subcommand, lock schema or lock CLI argument
- **THEN** the tool rejects the unsupported interface and does not provide compatibility behavior

### Requirement: CMake resolves offline during configuration
CMake SHALL accept explicit workspace, cache, source mode, state directory and build configuration, resolve the workspace offline internally and generate its own current graph. No package lock or pre-generated graph SHALL be a configure input. No missing option SHALL receive a new implicit value.

#### Scenario: Clean configure uses a single entry point
- **WHEN** the build directory is absent and the explicit configuration is supplied through a preset or arguments
- **THEN** CMake configures selected packages without a preceding package-tool invocation

#### Scenario: Metadata changes trigger configuration
- **WHEN** selected package metadata or workspace/provider/tool inputs change
- **THEN** the next build reconfigures and validates current declarations without a manual prepare step

#### Scenario: Unchanged build is incremental
- **WHEN** a successful build is repeated without changing inputs
- **THEN** no compilation or link is scheduled solely because graph preparation is internal
- **AND** the existing always-run package audit remains permitted

### Requirement: Generated graphs are disposable evidence
Resolved graph output SHALL directly describe the selected packages, source receipts, providers and edges without a nested lock or lock digest. Build receipts SHALL identify graph content without a lock field. CLI exporters SHALL resolve current declarations instead of trusting a prepared input graph.

#### Scenario: Stale graph cannot select sources
- **WHEN** an old graph exists with forged source mappings or stale metadata
- **THEN** configuration derives mappings from the explicit workspace and overwrites generated state rather than consuming the old graph

#### Scenario: Concurrent graph exports remain intact
- **WHEN** multiple processes export the same graph
- **THEN** process synchronization and atomic publication prevent partial output without creating a package lock

### Requirement: Source and build safeguards survive lock removal
Lock removal MUST preserve complete remote commit requirements, offline missing-cache failures, release source identity checks, provider provenance and actual-version checks, SDK/platform/compiler constraints, and actual target/link audits. Nix lockfiles and cache/process mutexes SHALL remain outside this removal.

#### Scenario: Undeclared link remains denied
- **WHEN** a package links an undeclared target or a production target uses a test-only dependency
- **THEN** the configure/build audit rejects it before successful package compilation

#### Scenario: Unavailable remote source remains offline
- **WHEN** a clean configure requires a remote source absent from the verified cache
- **THEN** configuration reports the missing source without downloading or selecting another version

#### Scenario: Installed SDK has the same entry-point behavior
- **WHEN** a standalone actor supplies explicit installed-SDK workspace/provider declarations
- **THEN** its CMake configure derives the graph internally and installed tooling exposes no package-lock schema or API
