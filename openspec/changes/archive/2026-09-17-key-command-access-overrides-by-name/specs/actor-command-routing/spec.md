## MODIFIED Requirements

### Requirement: Per-command policy overrides replace global policy
`command_runtime` SHALL accept bounded unique overrides as a TOML table keyed by canonical command name at `command_runtime.access.overrides.<command>`. Every override MUST provide complete explicit group and user policies under `.groups` and `.users` and SHALL replace both global policies for that canonical command. An override MUST reference an active canonical command or reserved `help`; duplicates, inactive names, malformed names, and matcher-derived aliases MUST fail candidate validation. The legacy array-of-tables representation and redundant `command` fields MUST be rejected rather than ignored. Each override value MUST be a table containing only `groups` and `users`.

#### Scenario: Help override grants broader access
- **WHEN** global group policy denies a caller but the canonical `help` override replaces it with policies that permit the caller
- **THEN** `/help` is permitted while other commands remain governed by the global denial

#### Scenario: Pattern alias selects an overridden command
- **WHEN** a matcher alias resolves to a canonical command having an override
- **THEN** the canonical command's replacement policy is evaluated

#### Scenario: Override names an inactive command
- **WHEN** configuration contains an override for a command absent from every active route and not equal to `help`
- **THEN** candidate validation fails before activation

#### Scenario: Command identity is visible in policy headings
- **WHEN** configuration supplies complete policies at `[command_runtime.access.overrides.exhentai.groups]` and `[command_runtime.access.overrides.exhentai.users]`
- **THEN** the override applies to canonical command `exhentai` without a separate command field

#### Scenario: Multiple keyed policies remain independent
- **WHEN** configuration supplies distinct complete override tables for `help` and another active command
- **THEN** each command receives only its own replacement policies and other commands use global policies

#### Scenario: Legacy override syntax is rejected
- **WHEN** configuration uses `[[command_runtime.access.overrides]]` with a command field
- **THEN** validation rejects it with a diagnostic directing the operator to command-keyed tables

#### Scenario: Keyed policy is malformed
- **WHEN** an override is not a table, has an invalid canonical key, includes unknown fields, or omits a required policy dimension, mode, or entries array
- **THEN** configuration validation fails before activation without inferring policy defaults

#### Scenario: Duplicate keyed policy is defined
- **WHEN** configuration defines the same override key or policy table twice
- **THEN** TOML parsing rejects the duplicate rather than merging conflicting policies
