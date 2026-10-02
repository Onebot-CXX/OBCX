## Why

OBCX already binds every package source explicitly and verifies dependency constraints without selecting alternative versions. Requiring a separately maintained package lock and a prepared graph adds redundant state and prevents a clean CMake configure after deleting the build directory.

## What Changes

- **BREAKING**: Remove `packages.lock`, the lock command/schema, frozen comparison, lock hashes, and lock-related CMake/CLI arguments; do not rename or replace the lock with another authoritative snapshot.
- Resolve the current explicit workspace offline inside CMake configure. The resolved graph becomes disposable generated build state, not an input that users must prepare.
- Retain source identity, version, dependency, compiler/platform, provider provenance, and actual target/link audits. Keep fixed remote commits and release source validation.
- Update CLI export tools, installed SDK tooling, tests, presets, CI and maintained actor documentation. Keep Nix `flake.lock`, vcpkg baselines and process mutexes unchanged.
- Supersede the package-lock/prepared-input requirements of the ongoing `declarative-package-dependencies` change. No configuration defaults, compatibility reader, deployment or commit is introduced.

## Capabilities

### New Capabilities
- `direct-package-configuration`: Lock-free workspace resolution and single-step CMake configuration with explicit inputs and preserved validation.

### Modified Capabilities
None. The prior package-resolution capability remains in an unarchived change, not a main specification.

## Impact

Affects `cmake/obcx_package`, CMake package loading, package/export CLIs and schema snapshots, installed SDK packaging, package/integration tests, presets, CI and build documentation. Existing package/workspace declarations remain valid; generated graphs must be regenerated using the new shape. Earlier actor availability and management-permission work is preserved.
