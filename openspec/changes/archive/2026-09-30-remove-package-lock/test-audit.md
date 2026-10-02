# Test audit follow-up

## User criterion

When behavior A is replaced by B, test B and its real failure boundaries. Do not keep an additional suite whose only purpose is to assert that A, its names, or its files no longer exist. Test count is not a quality target.

## Scope and findings

Inventoried all 901 registered CTest entries and searched root tests plus the maintained Bridge, ExHentai, Message Store and path-mapping tests for retirement/absence patterns. Read the candidate test bodies and the real-build/SDK coverage that can replace redundant source-text checks. This was a targeted retirement/redundancy audit, not proof that every remaining test is indispensable.

Removed or simplified:

- Package contract tests for rejected retired commands, arguments, schema names and filenames. The current schema fixtures, required/unknown-field validation and current CLI option checks remain.
- Repeated assertions that `packages.lock`, its digest or its old prepared graph does not exist, and the test that manufactures an obsolete graph solely to prove the exporter ignores it.
- Five independent CMake test fixtures. Clean configuration already occurs in the existing real-build test; that same built fixture now checks unchanged object/binary timestamps and metadata-triggered reconfiguration. Offline Git cache rejection remains in source tests; output containment remains in CLI tests using the shared validator; incompatible versions remain in resolver tests. No duplicate current behavior was moved into a large loop to disguise the count.
- Two redundant resolver tests: metadata update acceptance is exercised by the real CMake flow, and profile-dependent graph membership is already exercised by the existing resolver/profile and real-build tests.
- SDK retired-file globs, retired-symbol scanning and duplicate private-header globs. Keep the current public-header surface check and actual installed-consumer configure/build/run, plus all three isolated bot SDK builds.
- Two Python architecture tests that mirrored CMake text or manually reconstructed SDK header closure. Keep the source-layer import boundaries that installed public-header compilation does not exercise. Remove historical type-name bans and source spelling assertions.
- Four ExHentai tests for removed configuration options. Keep explicit current management policy, malformed current values, permission matrices, forged-source rejection, current tracking limits and persisted CRUD behavior.
- One C++ test that only assigned public `MessageEnvelope` fields and read them back. Actual typed emit/routing metadata behavior remains covered by actor/reflection/command tests.

Intentionally retained:

- Authorization denials, current invalid-input checks, source integrity, output/data preservation, cancellation and generation lifecycle behavior.
- Database migration tests: these exercise real supported data conversion and preservation, not merely the absence of old behavior.
- `test_retired_release_entrypoints_refuse_before_mutation`: these entrypoints still exist and include destructive `--clean`/work-directory operations. The important assertions preserve a user's sentinel and directory contents on failure; this is a current data-safety boundary, not a deleted-file check.

The policy is recorded in the previously empty `AGENTS.md` unit-test section. Runtime implementation/configuration was not changed by this audit.

## Measured change

- Python test methods across the eight package suites and bot architecture suite: **117 → 106** (11 fewer).
- Registered C++ test cases: **5 fewer**; overall CTest inventory **901 → 896**. Python methods are run inside module-level CTest entries and are not added to these totals.
- Real CMake test cases/independent setup fixtures: **12 → 7**, reusing the existing successful build for incrementality and reconfiguration rather than creating fresh builds for each assertion.
- SDK negative scanning blocks and obsolete assertions were deleted, not converted into new negative tests.

## Verification

- Built only affected `actor_api_test` and `exhentai_config_test` targets with 20 workers.
- Ran **51/51 affected CTest entries successfully** with 20 workers, including all package suites, architecture boundaries, actor API, ExHentai configuration/authorization/command cases, actual-actor availability integration, actor SDK smoke and all three bot SDK isolation suites. These include all **106 Python methods**.
- `nix fmt` completed with zero formatting changes. Root and nested ExHentai whitespace checks pass; staged work is unchanged.
- Logs: `/tmp/obcx-test-audit-{build,tests,format}.log`; before/after inventories: `/tmp/obcx-test-audit-inventory{,-after}.json`.
- The 901-test full-workspace run in `implementation.md` is evidence from before this pruning. This follow-up deliberately reran affected coverage, not the entire unchanged suite. No claim is made that the remaining 896 entries were all rerun in this audit.
- No deployment, service restart, database change, commit or archive occurred.
