# declared-build-dependencies Specification

## Purpose
Define implemented metadata-driven CMake ownership/provider/link audits for
workspace and installed-SDK builds, without claiming release acceptance.

## Requirements

### Requirement: Build preparation consumes one resolved dependency graph
Source preparation, third-party requirement export and CMake SHALL use the
same graph derived from explicit current declarations. CMake SHALL resolve
internally offline, not accept a source lock/pre-generated graph as authority,
fetch unprepared sources or consult a registry solver. SDK-only configuration
SHALL use explicit empty roots.

#### Scenario: A remote actor has third-party requirements
- **WHEN** permitted metadata identifies system dependencies
- **THEN** data tooling can export them before executing package CMake

### Requirement: System dependencies and SDK bindings are declared and verified
Third-party and SDK dependencies SHALL declare ID, version constraints, targets,
visibility and explicit provider bindings. Tooling SHALL verify observed versions,
targets and provenance, requiring verifiable evidence when an actual version
cannot be reported. A same-named installed library alone is insufficient; package
manager names and SDK bindings MUST NOT be guessed.

#### Scenario: A package is installed but undeclared
- **WHEN** a target outside metadata and its permitted closure is linked
- **THEN** mandatory audit rejects it despite successful find_package

#### Scenario: A provider exposes the wrong version or target
- **WHEN** actual version/targets do not satisfy the binding
- **THEN** configuration fails with expected/actual diagnostics

#### Scenario: An actor omits the SDK dependency
- **WHEN** actor metadata omits obcx-sdk
- **THEN** tooling rejects it rather than injecting a dependency

### Requirement: Package-owned targets and exports have verified ownership
Production/internal/test targets SHALL have package owners. Main target kind,
artifact/export identity, global alias uniqueness, PIC and supported compiler/
standard properties SHALL agree with metadata. Static/shared/header-only library
forms SHALL be distinguished, not guessed from suffixes.

#### Scenario: A package exports a different target kind
- **WHEN** a declared static library creates a shared artifact or omits its export
- **THEN** package validation rejects the mismatch

#### Scenario: An internal target hides another package dependency
- **WHEN** actor to internal target to external L lacks an authorized L edge
- **THEN** recursive audit reports the target chain rather than checking only direct helper arguments

### Requirement: Actual link and usage graphs must match declarations
Mandatory configure/generated graph and File API audits SHALL compare ownership,
link/usage, aliases, INTERFACE/OBJECT/LINK_ONLY and supported configuration-specific
expressions with metadata. Cross-package edges SHALL have appropriate direct
or propagated visibility. Undeclared imports, external include/archive/link
paths, bare libraries and unsupported expressions SHALL fail except explicitly
registered toolchain/system provenance. Configure success alone SHALL NOT be
reported as a passed generated audit.

#### Scenario: An actor bypasses the dependency helper
- **WHEN** target_link_libraries introduces an undeclared external edge
- **THEN** compulsory audit rejects it rather than issuing optional lint only

#### Scenario: A library is header-only
- **WHEN** no native shared-library dependency is emitted for a declared header-only package
- **THEN** version/owner/include/usage and transitive edges are still audited

### Requirement: Tests cannot introduce undeclared production dependencies
Test-only dependencies SHALL be available only under explicit tests profile
and test target ownership. Production link/usage closure SHALL NOT gain a
test-only library merely because tests were configured.

#### Scenario: A test library leaks into the actor artifact
- **WHEN** a production target links a dependency declared only for tests
- **THEN** graph audit rejects the leakage

### Requirement: Installed SDK supports the current canonical checks
The installed SDK SHALL carry matching parser/schema/helpers for explicit
current declarations and provider/target audits without old package readers or
core-private source headers. Expanded clean-prefix library publication/load
acceptance remains distinct from these implemented build interfaces.

#### Scenario: A standalone actor consumes declared ordinary-library sources
- **WHEN** its explicit workspace is configured against a frozen installed SDK
- **THEN** the canonical graph and supported ownership/provider/usage audits apply without a core rebuild

### Requirement: Build checks state their trust boundary
Supported graph forms SHALL fail closed and offline metadata preparation SHALL
not silently fetch dependencies. Package CMake/custom commands remain trusted
code. Target audits MUST NOT be advertised as a malicious-code sandbox or
proof of every arbitrary command's dependencies. Full isolated release,
installed-interface and provenance-receipt acceptance SHALL be recorded
separately rather than inferred from a successful development link.

#### Scenario: Build audit evidence is reported
- **WHEN** graph audits and a development build pass
- **THEN** the report distinguishes that evidence from unexecuted release, sandbox or runtime validation
