# Proposal

## Why

Startup and actor reload spend seconds repeatedly inspecting the same large Debug actor libraries before `dlopen`: the reported four-actor startup took about 16.8 seconds before the last contract discovery, while individual staging-to-discovery intervals were only 2–4 milliseconds. Offline measurements reproduced 14.2 seconds of repeated RPATH queries alone, making redundant staging work the first optimization target without changing dependency-isolation guarantees.

## What Changes

- Inspect each closure object's source RPATH once and reuse the parsed search directories for all of its dependency edges, preserving resolution order and `$ORIGIN` semantics.
- Share successful process-owned dependency digests within one generation-build attempt, guarded by canonical file identity and change detection; never reuse this cache across build attempts or treat the active generation's expected digest as a fresh observation.
- Preserve private dependency copying, content-versioned identities, edge rewriting, staged-output verification, existing failure gates, and generation lifetimes.
- Validate the improvement with offline before/after measurements and manual isolated staging checks; add no unit tests. The current checkout has no staging/reload unit-test sources in `tests/`.
- Defer a native ELF parser, parallel staging, symbol stripping/build-mode changes, and link-interface cleanup. Introduce no configuration options or defaults.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

None. This is an implementation-only performance optimization. Existing `package-release-closures` and `actor-runtime-reload` requirements remain unchanged; `.openspec.yaml` explicitly sets `skip_specs: true` rather than inventing a new behavioral requirement.

## Impact

- Primary implementation: `src/core/actor/actor_package_stager.cpp` and `include/core/actor/actor_package_stager.hpp`.
- Integration: `src/core/runtime/runtime_generation.cpp`, which owns the per-attempt staging context for startup, validation-only, and reload candidate builds.
- No actor ABI, configuration schema, package layout, external dependency, or reload-cutover policy changes are intended.
- Verification uses isolated core builds, copied actor artifacts, and staging-only diagnostics without starting production actors. Any required actor rebuild uses `scripts/actor_build.py` with a frozen installed SDK; no live core library is rebuilt or replaced, and no production reload/restart is part of this change.
