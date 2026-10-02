# actor-package-identity Specification

## Purpose

Define manifest-derived actor identity and source-owned logging while preserving one-parameter actor authoring, ABI V2 exports, isolated package bindings and shared thread-safe host output.

## Requirements

### Requirement: Manifest-derived actor identity
The SDK SHALL derive actor identity from explicit admitted `actor.name` and `package.version`. Actor authors SHALL retain `core::ReflectedActor<Derived>` without repeating name/version constants or supplying an identity template argument. Generated identity SHALL determine inherited constants, reflected contracts and existing ABI V2 name/version exports.

#### Scenario: Actor builds from admitted metadata
- **WHEN** an actor with valid handlers is built through the package helpers
- **THEN** its inherited identity, input contract and exported identity agree with the manifest without handwritten identity constants

#### Scenario: Required metadata is unavailable
- **WHEN** a production actor facade is compiled without a generated package binding
- **THEN** compilation fails with an actionable diagnostic rather than assigning fallback identity

### Requirement: Package bindings are isolated and incremental
The build SHALL isolate generated identities by package, apply them privately to compiled actor-owned targets, and keep common SDK template definitions independent of current-package macros. Manifest changes SHALL regenerate identity; unchanged generated content SHALL not cause recompilation.

#### Scenario: Multiple actor packages coexist
- **WHEN** separately built actors are loaded in the same process
- **THEN** their identities and dispatch remain distinct and core code does not acquire an actor binding

#### Scenario: Actor manifest version changes
- **WHEN** a valid package version changes and the build runs again
- **THEN** generated and exported version reflect the new manifest without a manual C++ update

### Requirement: Logs identify their source owner
Logging macros in actor-owned compilation targets SHALL use the manifest actor name, including internal implementation libraries and asynchronous background work. Host logs SHALL use `core`. This SHALL identify source ownership, not dynamically reattribute common-library logs to their caller.

#### Scenario: Actor and host log concurrently
- **WHEN** multiple actors and host code emit logs from worker threads
- **THEN** each record carries its source-owned logger name without global or thread-local actor switching

### Requirement: Named loggers share thread-safe host output
Initialization and first acquisition of the same named logger SHALL be concurrency-safe. Named loggers SHALL share configured host sinks, effective log level and flushing behavior. File, terminal and TUI output SHALL continue using the existing logger-name formatting.

#### Scenario: Concurrent first acquisition
- **WHEN** multiple threads first request the same actor logger
- **THEN** acquisition succeeds without duplicate registration errors and all use the same registered logger

#### Scenario: Log level changes
- **WHEN** the host changes its logging level before or after an actor logger is created
- **THEN** actor and core loggers obey the effective level and retain the shared output destinations
