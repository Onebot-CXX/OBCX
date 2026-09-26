> This change has been moved to OBCX root with explicit cross-repository
> implementation authorization. Its base specification remains
> `openspec/specs/actor-package-ecosystem/spec.md` in core. Do not create a
> second actor-local master specification or automatically archive this change.

> 阶段说明：当前先完成本地元数据和工作区构建迁移；独立 SDK 新消费场景、发行物 pin 与远程协调发布后置。保留现有 SDK 接口与测试，本阶段不将未完成的发行能力同步为已实现主规格。

## MODIFIED Requirements

### Requirement: Actor packages use one canonical metadata contract

Every standalone actor package SHALL declare its build and publication metadata in schema v2 `package.toml`, using the same canonical package contract as ordinary libraries with an explicit `package.kind` discriminator. Actor metadata SHALL include identity and semantic version, ABI V2/input contract, artifact identity, explicitly built and verified release platforms, typed dependencies, publication data, and supported OBCX/ABI/toolchain ranges. Build and packaging tools SHALL derive dependencies and publishable assets exclusively from this contract and the explicit workspace source lock. Required fields and empty dependency lists SHALL be explicit; unknown fields, v1 `actor.toml`, and old workspace `actors.toml` SHALL be rejected rather than interpreted with compatibility defaults.

#### Scenario: Standalone actor declares dependencies
- **WHEN** an actor package with canonical v2 metadata is included in a build
- **THEN** tooling resolves and validates its declared library, logical actor and system dependencies against explicit sources/provider bindings without consulting an old actor or plugin manifest

#### Scenario: Actor metadata omits a required field
- **WHEN** package.toml lacks required identity, ABI, artifact, dependency, compatibility or publication data
- **THEN** validation rejects it with a field-specific error and does not insert an implicit value

#### Scenario: Actor platform was not built and verified
- **WHEN** a registry or release process resolves a platform without declared and verified artifact support
- **THEN** no download entry or release asset is generated for that platform

#### Scenario: An old package format is supplied
- **WHEN** a maintained package still supplies only actor.toml or workspace actors.toml after cutover
- **THEN** the tool reports that v2 package.toml/packages.toml is required, without a compatibility reader or automatic conversion

### Requirement: Installed SDK builds V2 actor packages

The installed OBCX SDK SHALL expose the public headers, libraries, ABI V2 export helper, canonical v2 metadata tooling, and CMake functions needed to resolve declared dependencies and build/load an IActorV2 package. It SHALL support ordinary library consumption and metadata/target validation without treating libraries as runtime actors. Standalone and workspace builds SHALL enforce the same source-lock, dependency and supported toolchain contract.

#### Scenario: Clean external actor build
- **WHEN** a standalone V2 actor and its declared libraries are configured against a clean OBCX installation
- **THEN** they resolve, compile, link, install and load using installed tooling and explicit dependencies alone, and the actor handles an invocation without referring to the core source tree

#### Scenario: Installed surface is actor-only
- **WHEN** installed include and CMake contents are audited
- **THEN** runtime extension support remains ABI V2 actor-only with no plugin SDK, V1 actor helper or Asio-v1 target; ordinary library build helpers introduce no runtime extension factory

### Requirement: Active project surfaces use actor terminology

Active runtime extension surfaces MUST retain actor terminology and SHALL NOT reintroduce plugin or ABI V1 entry points. General build/publication surfaces SHALL use package terminology when they include actor and library kinds. Historical records and explicit breaking-change notes MAY identify removed actor-only metadata/index names. All maintained packages, examples, tests, schemas and tools SHALL cut over together rather than silently support old metadata.

#### Scenario: Active surface audit
- **WHEN** release source and installed artifacts are audited
- **THEN** runtime extension paths identify actors, generic metadata/registry paths identify packages, and neither old-format readers nor active plugin entry points exist

## REMOVED Requirements

### Requirement: Actor registry accepts actor packages only

**Reason**: Ordinary libraries must be first-class, validated publication units without masquerading as actors. The user approved a breaking metadata cutover rather than maintaining the old actors-only registry model.

**Migration**: Replace the old actor-only entries/schema/index generator with canonical v2 package entries and a kind-discriminated package index. Migrate all maintained entries and pinned consumers together; preserve only historical records, not a legacy compatibility output. Registry source/snapshot ownership and cross-repository release authority must be confirmed before implementation.

## ADDED Requirements

### Requirement: Package registry distinguishes actors from ordinary libraries

The registry SHALL publish canonical v2 package records with identity, version, kind, typed dependencies, artifact and compatibility data. Actor records SHALL validate ABI V2 factory requirements; library records SHALL validate static/shared/header-only exports and MUST NOT claim actor factories. Both SHALL use the same version-pinned canonical validator as build tooling and publish assets only for built and verified platforms. Registry records SHALL NOT cause automatic workspace version selection in this change.

#### Scenario: A valid library is indexed
- **WHEN** a repository submits a valid library package record and verified artifact data
- **THEN** the package index includes its library kind and exports without requiring an actor ABI or factory

#### Scenario: A package kind and artifact disagree
- **WHEN** actor metadata declares a static artifact or a library supplies an actor factory
- **THEN** registry validation rejects it consistently with local build validation

#### Scenario: A package is absent from explicit workspace bindings
- **WHEN** the registry contains a compatible version but the workspace has no source binding for the required ID
- **THEN** resolution fails rather than silently choosing the registry record
