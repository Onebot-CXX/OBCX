# test-suite-ownership Specification

## Purpose
Define ownership boundaries and execution tiers for root and standalone actor test suites.

## Requirements
### Requirement: Root tests cover root-owned behavior only
The OBCX root test tree SHALL contain behavior tests only for root-owned core/runtime, network, protocol-adapter, CLI, packaging, and installed-SDK surfaces. Root runtime tests MAY load minimal generic V2 actor fixtures, but MUST NOT compile production actor implementation sources, include actor-private headers, or assert Bridge, Message Store, or other standalone actor business behavior.

#### Scenario: Core reload needs rebuilt actor artifacts
- **WHEN** a root test verifies same-SONAME staging, dependency isolation, generation cutover, drain, or DSO retirement
- **THEN** it uses a generic fixture actor and generation marker owned by the root test suite rather than a production actor source

#### Scenario: Actor business regression is added
- **WHEN** a regression concerns forwarding, mapping, media, retry, persistence, or another standalone actor contract
- **THEN** its behavior test is added to that actor repository and not to the root test tree

### Requirement: Embedded actor packages do not register actor tests
An actor package embedded in the OBCX build SHALL build its production artifact without adding its repository test cases to the root CTest inventory by default. The same package configured as a top-level standalone project with testing enabled SHALL register its complete actor-owned suite, and SHALL expose an explicit package-scoped override for specialized consumers.

#### Scenario: Root tests are configured
- **WHEN** OBCX enables root testing and embeds Bridge and Message Store through actor package loading
- **THEN** the root CTest inventory contains their production artifacts but no Bridge- or Message-Store-owned behavior cases

#### Scenario: Actor repository is configured standalone
- **WHEN** Bridge or Message Store is the top-level CMake project with testing enabled
- **THEN** that repository registers and can run its complete owned test suite

### Requirement: Actor behavior tests remain with their owner
Each standalone actor repository SHALL own the sources, fixtures, expected contracts, and CTest registration for its business behavior. Cross-actor end-to-end forwarding behavior SHALL be owned by the actor that coordinates that behavior, while each dependency actor retains tests for its own persistence or transformation contract.

#### Scenario: Message Store to Bridge pipeline is verified
- **WHEN** an installed pipeline test asserts forwarding events, mappings, bot delivery, or Bridge reload behavior after Message Store persistence
- **THEN** the test source and assertions reside in the Bridge repository and consume Message Store through its supported installed actor contract

#### Scenario: Message persistence is verified
- **WHEN** a test asserts Message Store schema, deduplication, identity, or emitted stored-message behavior
- **THEN** the test resides in the Message Store repository

### Requirement: Test execution has explicit root-owned tiers
The project SHALL expose fast and full test tiers through stable CTest labels or presets. The fast tier SHALL run deterministic root tests; the full tier SHALL add compile contracts, root architecture/package checks, CLI validation, and installed-SDK smoke. Neither tier SHALL inspect, build, or execute an independent repository under `local_actor/`.

#### Scenario: Developer requests fast verification
- **WHEN** the fast tier is selected
- **THEN** it retains deterministic root network and runtime correctness coverage without cross-repository work

#### Scenario: Normal CI validates a change
- **WHEN** the normal supported-platform CI gate runs
- **THEN** it executes the full root-owned tier, including deterministic WebSocket reliability tests

#### Scenario: Standalone actor requires verification
- **WHEN** a standalone actor changes or consumes a new installed SDK
- **THEN** its owning repository runs that actor's build, installation, behavior, and integration tests outside the root CTest inventory

### Requirement: Test trees contain reproducible source inputs
Tracked test trees SHALL contain only reproducible source, fixtures, scripts, metadata, and documentation required by an automated gate. Generated interpreter caches, credentials, local bot environments, build outputs, and deleted-test residue MUST remain untracked, and test-layout documentation SHALL match the current directory structure.

#### Scenario: Python tests have been executed locally
- **WHEN** Python creates `__pycache__` or bytecode files under a test directory
- **THEN** those generated files remain ignored and are not treated as test sources or release inputs

#### Scenario: Test layout is documented
- **WHEN** a documented test directory is removed or relocated
- **THEN** the active test documentation is updated in the same change
