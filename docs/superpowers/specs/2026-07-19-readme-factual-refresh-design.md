# Multi-Repository README Factual Refresh Design

## Goal

Refresh the root README in OBCX and in each of the four actor-only companion
repositories so that every command and contract description matches the
current C++26 actor-only implementation. Remove obsolete plugin-era,
pre-CMake-3.30, and superseded release-verification information without
changing runtime or build behavior.

## Scope

The documentation changes are limited to these repository-root files:

- `README.md`
- `local_actor/obcx-actor-bridge/README.md`
- `local_actor/obcx-actor-message-store/README.md`
- `local_actor/obcx-actor-registry/README.md`
- `local_actor/obcx-actor-template/README.md`

OBCX remains Chinese-language documentation. The four standalone repositories
remain English-language documentation. Source files, CMake files, and example
TOML files are not rewritten as part of the documentation change.

Generated pinning and evidence files may change only as required to keep the
existing cross-repository candidate, offline source restoration, and release
verification checks valid after the README commits.

## Sources Of Truth

README statements must be checked against the following current interfaces:

- package identity, compatibility, artifacts, and dependencies from each
  repository's `actor.toml`;
- build requirements and preset paths from root `CMakeLists.txt` and
  `CMakePresets.json`;
- ABI exports from `OBCX_ACTOR_EXPORT_V2` in
  `include/core/actor/reflected_actor.hpp`;
- runtime configuration and validation from `ConfigLoader`, `DbManager`,
  `BotRegistry`, and the current actor-only architecture guides;
- supported CLI commands from each script's argument parser;
- installed paths and cross-repository behavior from the clean conformance
  and release verification tests.

Historical roadmaps and preserved benchmark inputs are evidence, not current
usage documentation.

## Repository Updates

### OBCX

Keep the README as the operational entry point. Use the checked-in CMake
presets for the primary build flow, retain the CMake 3.30/GCC 16.1 reflection
baseline, and show only real actor package repositories. Explain the current
actor search/install layout, validation-only behavior, actor dependency and
pipeline validation, offline pinned actor restoration, and release evidence
verification. Replace the nonexistent `actor_runtime_gate.py report` command
with the current evidence verifier.

### Bridge

Describe the bridge as an ABI 2 reflected actor that consumes
`MessageStored`. State that `OBCX_ACTOR_EXPORT_V2` supplies the ABI generation,
factory, destructor, name, version, and schema-2 input contract instead of
claiming one exported entry point. Document the effective CMake 3.30/GCC 16.1
baseline, shared `DbManager` ownership, `requires = ["message_store"]`, and the
current requirement that platform-only bot lookup resolve to exactly one live
bot per platform. Remove `database_file` from the recommended actor settings;
the runtime DB instance is authoritative.

### Message Store

Add the canonical package contract, CMake 3.30/GCC 16.1 baseline, installed
library and metadata paths, minimal shared-DB actor configuration, and the
RawMessageEvent-to-MessageStored pipeline contract. Preserve the schema-v3
migration and namespace-collision warning, and document the standalone smoke
test.

### Registry

Keep the registry README short. Identify the bridge and message-store entries,
document strict canonical metadata validation, deterministic index generation,
declared-platform-only artifact resolution, and the relationship to the OBCX
five-repository conformance matrix. Commands remain relative to the standalone
registry repository.

### Template

Raise the documented baseline to CMake 3.30. Describe the full reflected ABI
contract, canonical metadata fields, build/install layout, and a minimal
runtime actor/pipeline configuration. Remove the invalid local invocation of
`cmake/gen_vcpkg_manifest.py`: actor packages declare dependencies in
`actor.toml`, while the consuming OBCX checkout merges selected package
dependencies through its own `actors.toml` and generator.

## Pinning And Commit Strategy

Create one documentation commit in each standalone repository. Commit the
OBCX README separately. Then refresh the existing offline restoration model:

- archive the new bridge documentation commit in `bridge.bundle` and update
  its fixed revision;
- regenerate the message-store, registry, and template adaptation patch streams
  so they end at their new documentation commits;
- update `restore-sources.sh`, `apply-patches.sh`, and
  `packaging/actors/README.md` to the resulting deterministic revisions;
- regenerate `actor-registry/conformance-matrix.json` from the final five
  checked-out repositories;
- refresh only the release evidence identities and reports required by the
  existing verifier, using real verification runs rather than fabricated
  success records.

The root integration and evidence refresh are separate commits from the OBCX
README change.

## Verification

Documentation verification consists of:

1. checking all relative Markdown links in the five README files;
2. scanning for the removed stale strings and invalid commands;
3. running each standalone repository's metadata or unit checks where present;
4. restoring all actor repositories from the offline bundles and patches into
   an empty directory and comparing the resulting revisions and README bytes;
5. checking the conformance matrix and actor cutover evidence bundle;
6. running the root documentation/package-related CTest labels and the clean
   cross-repository conformance test;
7. confirming every affected Git worktree is clean after its commits.

No success claim is made from README inspection alone.
