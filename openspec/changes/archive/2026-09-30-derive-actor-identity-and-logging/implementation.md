# Implementation and verification

## Delivered

- `cmake/OBCXActorIdentity.cmake` generates metadata-only and reflection-binding headers from explicit package identity. `OBCXPackages.cmake` automatically binds actor artifact, implementation and test compilation targets privately. Unchanged configure preserves generated timestamps.
- `reflected_actor_impl.hpp` contains the common identity-parameterized SDK implementation and export machinery. `reflected_actor.hpp` requires the generated package facade. Package-specific aliases retain `core::ReflectedActor<Derived>` without handwritten name/version or a second argument.
- Bridge, ExHentai, Message Store, the QQ probe, the actor template and ChatLLM no longer repeat identity constants. Host-only multi-identity tests use an uninstalled test adapter. Host runtime utilities and the ExHentai host SDK probe use the unbound implementation header.
- Logging macros select the generated actor logger at their expansion sites. Core logs use `core`; no thread-local/global actor switching. Named loggers share sinks and policy, with `call_once` initialization and synchronized creation/level updates.
- Authoring/build documentation and installed SDK files reflect the new contract. No runtime configuration options or defaults were added. No live configuration, database, service activation, commit or archive occurred.

## Focused coverage

The standing suite grows by only two entries: one concurrent logger behavior test and one missing-build-metadata compile-contract case. Existing package CMake and installed SDK fixtures were extended rather than duplicated:

- Concurrent first logger creation, two source-owned actor helper libraries (including debug-trace macro mode), host logs, shared sinks/flush policy, and level changes.
- Real package builds verify artifact/helper identities, changed manifest versions and stable object/generated-header timestamps.
- Installed SDK smoke builds a real internal static helper, invokes it on a blocking worker, resumes actor work, and verifies generated identity, logger names, ABI exports, contract and dispatch.
- Existing reflection rejection/dispatch, loader, generation/reload, availability, Bridge/ExHentai/probe and SDK isolation coverage remains.

## Build issues discovered and resolved

1. Installed SDK Makefile linking exposed an existing audit error for target-directory-relative internal archives. Link fragments and search paths now resolve from the actual generator link directory; declared closure checks remain unchanged. The original failure is preserved in `/tmp/obcx-identity-tests.log`.
2. The installed-template Ninja build exposed duplicate missing-input rules when provider receipts reference already-included SDK modules through `../`. Provider configure dependencies are now normalized. The existing package CMake fixture includes this receipt alias, without adding another expensive build fixture.

## Evidence

- Complete root build with 20 workers passed: `/tmp/obcx-identity-final-build.log` (and `/tmp/obcx-identity-build-after-cmake.log` after the dependency-path normalization).
- **162/162 focused CTest entries passed**, including four installed SDK suites: `/tmp/obcx-identity-final-tests.log`. The subsequent CMake dependency-path correction passed both affected suites again: `/tmp/obcx-identity-build-helper-tests.log` (**2/2**).
- ExHentai's independent installed-SDK host probe built and passed **1/1**: `/tmp/obcx-identity-exhentai-sdk.log`.
- Canonical actor template independently built against the installed SDK with Ninja. Dynamic export/contract checks returned `example`, `0.1.0` and the matching actor contract: `/tmp/obcx-identity-template.log`.
- ChatLLM's declaration was migrated but it is not selected by the current workspace and was not independently built in this acceptance run.
- Explicit unchanged root reconfiguration preserved all eight selected-package generated header timestamps and caused **zero C++ compilation/linking** (only regenerated scanner/dyndep metadata). The following unchanged build ran only the package audit: `/tmp/obcx-identity-final-reconfigure.log`, `/tmp/obcx-identity-final-incremental.log`, `/tmp/obcx-identity-final-unchanged.log`.
- Latest root `nix fmt` changed zero files: `/tmp/obcx-identity-format-final.log`. Root and nested whitespace checks passed. Strict change validation and all 20 main specs passed: `/tmp/obcx-identity-specs.log`.
- The original root staged patch is byte-identical to `/tmp/obcx-identity-staged-before.patch`; original root unstaged patch was empty. Untouched pre-existing nested diff blocks were compared against `/tmp/obcx-identity-<repo>-before.patch` and preserved.
- The complete remaining **898-entry** CTest inventory was not rerun; focused acceptance is intentionally not described as a full-suite run.
