# Acceptance evidence

The counts and coverage below record the initial implementation acceptance.
The user's subsequent test-pruning request is documented in [test-audit.md](test-audit.md),
which supersedes the standing retirement-only tests and records the latest focused verification.

## Implemented

- Removed the package-lock command, schema, frozen comparison, nested graph lock fields, build-receipt lock digest, SDK schema install entry, CMake lock/prepared-graph arguments and all active consumer references.
- CMake derives the current graph offline from four explicit package inputs. Graph outputs are disposable; no replacement approval snapshot was introduced. Presets, CI, CLI exporters and installed SDK consumer preparation use the new path.
- Retained current-declaration validation, fixed source identity, provider provenance, actual target/link audits, atomic publication and process/cache synchronization.
- Updated core and maintained actor build documentation. Annotated the superseded active package proposal/design/tasks; historical archived evidence was not rewritten. Its old specification clauses must be reconciled with this successor before future archive/sync.
- Removed the ignored workspace's obsolete `packages.lock` and its guard; a diagnostic copy is outside the repository at `/tmp/obcx-retired-packages.lock`. Refreshed only the SDK evidence hash for the removed schema install entry in `src/CMakeLists.txt` and its reference in `packages.toml`.

## Verification

All compilation and test commands used 20 workers (isolated fixture builds use available CPUs, minimum six). Only one Ninja instance built each directory.

1. Eight package test modules: **113/113 Python cases passed**. Includes removed-interface/schema rejection, metadata changes without acceptance, output containment, concurrent atomic exports, offline cache errors, one-step configure, stale graph replacement, metadata-triggered reconfiguration and incremental builds.
   - The first run exposed one assertion that did not normalize CMake's wrapped diagnostic; corrected the assertion and reran all modules successfully. No production validation was weakened.
   - Logs: `/tmp/obcx-no-lock-*_test.log`, `/tmp/obcx-no-lock-package-tests.status`.
2. Real root workspace configure succeeded without package-tool preparation; full build succeeded.
   - `/tmp/obcx-no-lock-configure.log`, `/tmp/obcx-no-lock-build.log`, `/tmp/obcx-no-lock-build.status`.
3. Fresh `cmake --preset actor-dev` succeeded, generating `build/actor-dev/package-state/cmake/current-graph.json` directly.
   - `/tmp/obcx-no-lock-clean-preset.log`.
4. Unchanged root builds had **zero compilations and zero links**. After an explicit reconfigure CMake refreshed dyndep metadata only; the next build and final Ninja dry-run contained exactly **one package audit** and no dependency-log recovery warnings.
   - `/tmp/obcx-no-lock-incremental-{first,second}.log`, `/tmp/obcx-no-lock-final-dry-run.log`.
5. Workspace CTest: **897/897 passed**; installed SDK CTest: **4/4 passed** (actor SDK smoke plus all three bot isolation suites), **901/901 total CTest entries**. The Python case count above is separate from the CTest module-entry count and is not added to it.
   - `/tmp/obcx-no-lock-workspace-tests.log`, `/tmp/obcx-no-lock-sdk-tests.log`, corresponding `.status` files.
   - Installed SDK smoke explicitly checks absence of the retired schema and absence of manually prepared lock/graph inputs before consumer configure.
6. `nix fmt` succeeded, 427 files processed and zero changed. Root and affected nested diffs pass whitespace checks. New change and superseded active change pass strict OpenSpec validation; all **20 main specifications** pass.
   - `/tmp/obcx-no-lock-format.log`, `/tmp/obcx-no-lock-specs.log`.
7. Original staged changes compare byte-for-byte with the pre-change snapshot. `flake.nix` and `flake.lock` retain their original SHA-256 hashes. No retired interface remains in active implementation or documentation; intentional negative tests and explicitly historical OpenSpec artifacts retain references.

## Operational scope

No live configuration, database or running service was modified. No deployment, restart, commit or archive was performed. The existing `build/` is configured and fully built; the fresh preset directory was configured but not redundantly built.
