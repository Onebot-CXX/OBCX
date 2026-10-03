# Implementation record

## Confirmed scope

The user explicitly approved moving **this change only** from ExHentai to the OBCX root and implementing across core/SDK, registry, maintained actors and the new ordinary library. `qq-gallery-forward-batches` remains under ExHentai. The authoritative `actor-package-ecosystem` specification remains `openspec/specs/actor-package-ecosystem/spec.md` in core. No remote publication, deployment, service restart, live message, commit or archive is authorized by this implementation step.

The independent `local_actor/obcx-actor-registry` is the publication source; root `actor-registry/` becomes a pinned conformance snapshot, not a second independently maintained registry. The canonical schema/parser is core SDK tooling; registry consumes an exact version and digest of that tooling. A coordinated cutover must ship core/SDK/tooling, all maintained packages and registry together. Rollback restores the previous complete release set, not selected v1 manifests in an otherwise v2 deployment. Current registry checkout: `11c3033bddb25a2fe61456545edb20b458a391cc`; core base: `05de83465830503a46840526f2cf939911e384bf`. New release commit identities must be recorded after approved commits exist, never invented in advance.

## Latest scope decision

The user explicitly deferred standalone SDK binary/header publication and external-consumer concerns because the whole project is still in development. Continue with the core workspace development loop, then mapper and its two consumers. Release/distribution/coordination work is deferred, not completed or silently removed. Keep existing SDK functionality and runtime safety regressions. Necessary local metadata/schema/validator migrations still accompany the source cutover.

The earlier request to choose a standalone source-lock/release-layout policy is withdrawn as a current blocker. No new policy was approved by that discussion. Use the already explicit development/working-tree semantics for the current workspace; do not require clean commits, change source-lock meaning, add defaults, or require a separate repository merely to resume core implementation. The remaining release/standalone edge case stays recorded for its later stage.

## Cutover inventory

| Surface | Required migration |
| --- | --- |
| Root `actors.toml` (local ignored), `actors-example.toml`, CMakeLists.txt, CMakePresets.json, .gitignore | Explicit v2 workspace/lock/graph and preparation commands; no file-missing fallback |
| `cmake/actor_metadata.py`, `parse_actor_packages.py`, `gen_vcpkg_manifest.py`, `OBCXActor.cmake`, `OBCXActorLoader.cmake` | Canonical v2 tooling, metadata-first resolution, graph-driven linkage and audit; remove v1 readers at cutover |
| `schemas/actor-package.schema.json` | Replace actor-only contract; remove current schema/parser divergence (input_contract_schema) |
| `src/CMakeLists.txt`, `cmake/obcx-sdk-config.cmake.in` | Install versioned parser/schema/helpers and equivalent independent-build support |
| `local_actor/obcx-message-bridge` (`vollate.bridge`) | Manifest, CMake/internal targets/tests/docs; mapper consumer |
| `local_actor/obcx-message-store` (`onebot-cxx.message-store`) | Manifest, CMake/tests/docs |
| `local_actor/obcx-exhentai-fetch` (`vollate.exhentai-fetch`) | Manifest, CMake/internal targets/tests/docs; mapper consumer |
| `local_actor/obcx-exhentai-fetch/tools/qq-forward-probe-actor` (`vollate.qq-forward-probe`) | Manifest, standalone CMake/tests/docs; preserve user-operated probe |
| `local_actor/chat_llm` (`faccoco.chat-llm`) | Manifest, CMake and standalone interface even when not a selected root |
| `local_actor/obcx-actor-template` (`onebot-cxx.example`) | Manifest, template CMake/docs; no old-format example |
| `tests/fixtures/standalone_v2_actor` | Metadata/CMake, clean installed SDK smoke |
| Root and independent registry entries for bridge/message-store | v2 package records and generated package index |
| Independent registry `scripts/actor_metadata.py`, `generate_actor_index.py`, schemas, tests, workflow, README | Remove copied validator; pinned SDK tooling; kind-aware index; verified assets only |
| Root registry generator/schema/index/README | Pinned conformance snapshot and tests, not another validator fork |
| `scripts/package_actor_release.py`, `verify_actor_release.py`, rollback rehearsal | Inventory-driven package selection and complete dynamic closures |
| `tests/python/actor_{metadata,package_manifest,vcpkg_manifest,release_tools}_test.py`, `tests/cmake/python_tests.cmake`, SDK smoke | Replace old contract fixtures/assertions and add graph/provider/audit/install negatives |
| `tests/cpp/command_coordinator_test.cpp` | Review terminology string hits; runtime actor configuration is not build selection and must remain unchanged |
| `README.md`, `tests/README.md`, docs/architecture, .github/workflows/ci.yml, flake.nix, vcpkg-base.json | Update active build/release instructions and provider provenance, preserving core baseline |
| `packaging/actors/patches/{bridge,message-store,template,registry}.patch`, benchmarks, changelog | Classify active bootstrap patches versus historical evidence; remove active v1 bootstrap, retain clearly historical records |
| Independent `local_library/obcx-path-mapping/` repository | SDK-independent static PIC package and containment/URI tests; explicit workspace source, not tracked in core |

This inventory covers repositories present locally, not uninspected external consumers. Implementation adds v2 infrastructure first; the old pipeline is removed at the coordinated cutover, not supported by a compatibility parser. Staged source edits do not constitute two supported release protocols.

## Progress and verification

Completed tasks: 1.1–1.3, 2.1–2.2, 2.4, 3.1–3.6, 3.8, 4.1–4.3, 5.1–5.5 and 6.1 (**22/49**). The real root and seven maintained manifests use v2; the four selected actors build through metadata-driven linkage/audit. Old core readers/helpers and copied registry validators are removed. Registry local checks use canonical tooling and publish no invented downloads. Existing installed-SDK functionality is preserved; expanded standalone acceptance and approved published-tool pins remain deferred. Tasks 6.2/6.3 stay open for remaining repository workflow/docs and historical container/release entry points. Final receipts, release acceptance and mapper extraction remain unfinished.

Earlier component-stage evidence follows; the latest core integration increment below supersedes its pre-cutover status statements.

Validation:
- `cmake --build build --parallel 20`: passed; `/tmp/obcx-package-v2-build.log`.
- First 65 Python cases: passed individually with 20 workers; `/tmp/obcx-package-v2-unit-tests.log`.
- Expanded 70 Python cases: passed in CTest, including real Git object preparation via an offline fixture transport, installed canonical parser validation and cache/credential/path negatives.
- Full CTest ran once **811/811 passed**. A later full follow-up with the added installed-tool assertions was **810/811**: only the previously known, unmodified `StatisticsIntegrationTest.RetirementDrainsAndLateCompletionPreservesOutcome` lifetime race failed. Preserve that failure evidence in `/tmp/obcx-package-v2-ctest-followup.log`; do not describe it as a package regression fix.
- Focused rerun of that statistics case, all three new Python suites and updated installed SDK smoke: **5/5 passed**, 20 workers; `/tmp/obcx-package-v2-focused-tests.log`.
- Strict OpenSpec validation and `git diff --check`: passed.

Provider/preparation increment:
- `OBCXPackageProviders.cmake`, `package_cmake.py` and `obcx_package/providers.py` verify explicit config/module/pkg-config/workspace-SDK bindings, reported versions, authorized targets and environment prefixes. Unversioned targets need pinned, related file evidence. Environment inputs are checked before provider CMake executes.
- Canonical provider-environment and provider-receipt schemas accompany mandatory `version_probe` and `package_manager` fields. Existing v2 workspace fixtures were migrated; no fallback values were introduced.
- `gen_vcpkg_manifest.py` now exports a prepared frozen v2 graph without executing package CMake. It retains the supplied core base and requires explicit port/features/default-feature-policy/baseline mappings. The maintained actor manifests and root loader still await coordinated v2 cutover; this exporter is not advertised as working with their old manifests.
- Task 4.3 has environment input/prefix checks, including concrete Nix store roots, but actual core flake/provider wiring is still pending. Keep 4.3 and expanded standalone task 4.4 unchecked.
- Following the user's testing request, removed five newly written redundant unit checks; retained 14 focused provider cases and rewrote the existing four vcpkg cases instead of adding a second legacy/v2 suite. Future tests should cover distinct contracts/real paths, not mirror private helper branches.
- Initial targeted package/vcpkg run: **87/88**, with the sole failure caused by CMake wrapping the expected diagnostic across lines, not provider acceptance. Fixed the assertion to ignore display whitespace. Follow-up contract/provider/vcpkg run: **42/42**, 20 workers (`/tmp/obcx-package-provider-focused-tests.log`). No new full C++ regression run was needed for this increment.
- Provider contract and transition boundaries: `docs/architecture/package-providers.md`.

CMake graph/audit increment:
- Added `OBCXPackages.cmake`, `obcx_package/cmake_plan.py` and `obcx_package/target_audit.py`, with `package_cmake.py` entry points. The new loader checks current frozen inputs offline, enforces platform/compiler/ABI and explicit configuration/state, supports empty roots, and loads prepared sources in topology order.
- V2 library/actor constructors and internal/test target registration derive artifact identity and external usage from metadata. Targets are checked for ownership, export/type/output/PIC/standard; package-created imported targets are rejected. Private/test-only dependencies cannot become public/production usage. Provider target snapshots and provenance are rechecked.
- Deferred checks handle visible edges; native CMake generation supplies configuration-specific expression results (separate LINK_ONLY link/usage contexts). A compulsory build dependency runs generated-graph plus File API checks before any package target compiles. It cross-checks sources/objects, includes, dependency/ABI/standard/PIC flags, artifacts, link fragments and actual compiler implicit libraries/directories. Configure success alone is not a passed generated audit.
- Kept the real-build fixture compact: seven cases cover static/shared/header-only plus an internal OBJECT target, fixture workspace SDK/actor construction, production/tests profiles, empty roots, hidden direct dependencies, active conditional edges, PIC, test-only leakage and a source-specific flag bypass caught only by File API. This does not claim a real SDK actor invocation or installed-interface acceptance.
- Fixed native evaluation of LINK_ONLY (file generation has no link context) and legitimate CMake-generated build RPATH validation while developing the fixture. Final focused package/component run: **95/95 passed**, 20 workers; `/tmp/obcx-package-graph-component-tests.log`. Seven real-build cases also passed separately; `/tmp/obcx-package-cmake-tests.log`. No unchanged full C++ suite was rerun.
- Tasks 5.1/5.3 remain unchecked: the new component exists but the active root loader/old actor helper and maintained DEPS lists are not yet replaced. Task 5.6 has local test/production isolation, but clean installed public/private interface leakage acceptance remains pending. Task 5.7 final receipts/isolated-CI network enforcement is also pending.
- Next is the coordinated core source cutover, including Nix/provider anchors (4.3), explicit workspace settings and removal of inherited core include directories from SDK-independent packages. Preserve existing SDK/stager tests and unrelated actor edits. Usage and limits: `docs/architecture/package-cmake.md`.

Review found a source identity/layout issue before cutover: hashing generated graph/lock/cache under a source root becomes self-referential; pinning a path package's containing Git commit in a lock stored in the same Git repository also creates a commit/lock cycle (including monorepo subdirectories). The current data tool rejects in-source generated state rather than falsely claiming reproducibility. The standalone/release policy was not selected and is now explicitly deferred; it does not block freezing the development part of 1.4 or the core workspace CMake implementation. See tooling-contract.md. Do not impose the initial restriction as a final standalone workflow or pretend that the eventual release requirement is solved.

## Core build integration increment

- Root `CMakeLists.txt` now requires explicit workspace/lock/prepared graph/cache/mode/state and uses `OBCXPackages`; it no longer reads legacy build selection. Removed inherited core include directories so ordinary libraries do not silently acquire SDK headers. Both presets explicitly select development source semantics and distinct state paths; the release-named preset is an optimized build, not formal release acceptance.
- Authored v2 metadata and migrated CMake for Bridge, Message Store, ExHentai, the QQ probe, chat_llm, template and SDK fixture. External edges come from TOML; internal/test targets are registered and only same-package links remain handwritten. Runtime names, ABI V2 and business sources are unchanged. Current roots remain Message Store, Bridge, ExHentai and probe; inactive chat_llm/template builds are not claimed as tested.
- Local `packages.toml`, `packages.lock` and `.package-state/providers/` bind reviewed actual Nix outputs, flake input hashes, SDK source CMake anchors and reported versions. SDK exports its actual `OBCX_SDK_VERSION` property. Local records are ignored rather than published as portable machine-independent configuration. Automatic/repeatable flake/CI authoring is still pending, so 4.3 stays unchecked.
- SDK installs the canonical CMake modules and Python driver instead of the old actor helper. The existing SDK smoke authors explicit frozen inputs from trusted core dependency observations, checks installed tooling with PYTHONPATH unset, then builds, installs, loads and invokes the fixture through the v2 graph. This is preservation of existing functionality, not expanded task 5.8 completion.
- Real integration exposed and fixed distinct CMake semantics: SYSTEM include markers classify existing paths without introducing search directories; manual dependencies impose build order without granting linkage; native install RPATH padding is not an extra declared directory; package/bootstrap directory identity must use binary directories when source paths repeat. File API additionally rejects generated unregistered targets inside owned binary roots. These are exercised by the actual core/SDK paths, not redundant new unit suites.
- `cmake --build build --parallel 20`: passed, 458 build steps (`/tmp/obcx-v2-cutover-build.log`), with the explicit `Development` configuration preserving the previous non-optimized local compiler flags. Subsequent incremental build passed (`/tmp/obcx-v2-cutover-followup-build.log`). The mandatory generated audit passes for all selected packages.
- Full CTest, 20 workers: **826/827 passed** (`/tmp/obcx-v2-cutover-ctest.log`). The sole failure is the existing unmodified statistics lifetime race, `StatisticsIntegrationTest.RetirementDrainsAndLateCompletionPreservesOutcome`; focused rerun passed (`/tmp/obcx-v2-cutover-statistics-rerun.log`). Do not report a clean all-pass full run or claim that race was fixed.
- Existing SDK smoke passed independently (`/tmp/obcx-v2-cutover-sdk.log`) and in the full run. Provider and seven-case CMake suites passed. No additional test cases were added merely to duplicate covered contracts.
- Real core SDK-only configuration plus generated audit passed with explicit empty roots (`/tmp/obcx-v2-sdk-only.log`); no duplicate full compilation was needed for that path.
- Remaining coordinated cutover work is explicit: legacy metadata/parser/schema/helper files and their registry/release/docs/test consumers still exist and must be migrated/removed. The old files are not read by the new build and are not a fallback. Section 6 stays unchecked; this staged branch is not a finished dual-format release. Retained bridge standalone conformance entry points have not yet been accepted through installed-SDK private-header isolation; do not claim that optional path is complete.
- Next: finish local validator/schema/script/fixture references and reproducible environment wiring, then mapper extraction/two consumers. No deployment, live QQ send, publication, commit or archive occurred.

## Canonical-reader cutover increment

- User correction: third-party environment selection belongs to existing `flake.nix`/`flake.lock`. Reverted the attempted flake/environment-list generator, verified both flake files unchanged, and removed that extra authoring requirement as a blocker. Task 4.3's SDK/actor boundary and existing environment integration are complete for the narrowed development scope; no replacement Nix lock or new third-party list is introduced.
- Removed core `actor_metadata.py`, `parse_actor_packages.py`, `OBCXActor*.cmake`, the actor-only schema/example and all seven maintained legacy manifests. The active files are v2 `package.toml`; runtime TOML, names, ABI and business logic are unchanged. Task 6.1 is complete.
- Added canonical registry metadata/index operations to `package_tool.py`/`obcx_package/registry.py`. Independent registry now has a thin, explicit-tool wrapper and no copied validator/schema; the root snapshot mirrors its entries/wrapper/index. The v2 index is explicitly `development-metadata-only`, with source digests but no manufactured binary URLs. Local version checking is not a claim of approved published tooling pins; tasks 2.3/8 remain open for their deferred release portion.
- Migrated release metadata reads to the canonical parser and the v2 installation path. Existing archive and atomic-switch helpers/tests remain. The narrow single-DSO packager now rejects declared ordinary-library dependencies instead of silently omitting their closure. Full inventory-driven packaging and the old standalone verification/container recipes are not claimed complete.
- Core CI now validates the v2 snapshot and explicitly prepares empty roots for its core-only build; it no longer restores obsolete v1 actor bundles. Compilation/tests request at least six workers. Existing installed-SDK CI label selection was not broadened or silently reduced. Native CMake fixture and SDK smoke platform inputs were made consistent with the explicit runner/workspace platform; local verification is x86_64 only, not a claim of an executed ARM CI job.
- Independent registry workflow definitions require an explicit full tooling commit (none invented). Automatic index commit/Pages publication is disabled for the metadata-only development catalog; manual generation produces only a review artifact. No remote workflow was executed.
- Replaced the two v1 parser test suites with canonical equivalents already present in `package_contract_test`, resolver and real CMake suites, rather than duplicate all their rules. Added only three registry-specific cases and one release-closure refusal case. Existing installed-SDK smoke additionally rejects installation of the removed helper/parser/schema surfaces. Independent registry keeps a single delegation/current-index check instead of copying metadata test rules.
- `cmake --build build --parallel 20` passed. Focused CTest **12/12 passed**, 20 workers, including canonical package suites, registry, vcpkg, release utilities, installed SDK, two stager safety cases and bot modularity (`/tmp/obcx-package-reader-build-final.log`, `/tmp/obcx-package-reader-tests-final.log`). Independent registry delegation check passed (`/tmp/obcx-registry-local-wiring.log`). Unchanged full C++ suite was not rerun for this metadata/tooling increment; the previous statistics flake evidence remains unchanged.
- Updated root/registry/template/Bridge/Message Store/probe/author-guide instructions. Remaining historical container/source-archive and cross-repository standalone workflow cleanup is explicit; do not mark 6.2/6.3 or complete release acceptance merely because local checks pass. Mapper remains next after the required development-path cleanup.

## Development-entry cleanup and approved media policy

- Audited the maintained actors: no additional actor-local `.github` workflows or CMake presets were present to migrate. Corrected active architecture/SDK guidance to `package.toml`/`OBCXPackages`; earlier breaking-change and roadmap records are explicitly historical. Existing source-bundle inspection scripts now require a destination instead of defaulting to maintained `local_actor/` checkouts.
- Retired the old container build wrapper and standalone release-verification orchestration. They fail before invoking builders, running actors or modifying directories; the historical Containerfile is explicitly unsupported. The coordinated single-DSO release CLI likewise refuses before cleanup or asset production. Archive and atomic-switch helpers and their tests remain; this is not implementation of section 7's new release system.
- Added one release-entrypoint refusal regression covering the three CLIs and preservation of an existing output directory. No runtime business code or SDK/stager coverage was removed.
- Incremental build passed with 20 workers (`/tmp/obcx-package-goal-cleanup-build.log`). Focused canonical package, release-tool and installed-SDK suites passed **8/8** (`/tmp/obcx-package-goal-cleanup-tests.log`); stager safety cases passed **2/2** (`/tmp/obcx-package-goal-cleanup-stager.log`). Strict OpenSpec validation, shell syntax and diff checks passed. Both flake files remain byte-identical to HEAD. Full C++ regression was not repeated for this scripts/docs increment.
- Local task 6.2/6.3 cutover is complete; deferred standalone/published acceptance, final receipts and release closure tasks remain unchecked. chat_llm/template metadata and CMake are migrated, but their production binaries have not been accepted by a real build in this workspace.
- User confirmed shared-file paths, permissions, retention and capacity in goal `muf7ej4d-srqpof`; the actor-local `qq-gallery-forward-batches/shared-files-approved.md` records actual mount inspection and exact approved values. Paths are read from actor configuration and passed into mapper, never hardcoded. No shared directory was created and no runtime configuration, deployment, restart or QQ message was performed.

## Library implementation and consumer acceptance moved

The original mapper implementation, Bridge consumer increment, two-consumer acceptance and repository-ownership correction sections moved verbatim to `local_library/obcx-path-mapping/openspec/changes/declarative-package-dependencies/implementation.md`; see [migration.md](migration.md). Historical paths, failed runs, reruns, counts and lock digests are preserved there, not reinterpreted as new acceptance.

Core integration evidence from those joint increments remains relevant:

- C++23 ordinary libraries no longer receive the C++26-only reflection flag; the SDK smoke installs only the core-owned subtree and retains surface auditing.
- Both real consumers passed mandatory dependency auditing, and missing-source/version negatives failed before package CMake.
- Historical stage-completion CTest passed **834/834**. After repository relocation, the first full run was **866/867**, with the unmodified Asio lifetime observation race; ten independent reruns passed, then the full run passed **867/867**. Neither that race nor the earlier statistics race was claimed fixed.
- These summaries do not replace the complete original commands, logs, failures and boundaries in the relocated record. No new build/test run is claimed for the 2026-09-30 documentation move.

## Preserve unrelated work

Core HTTP/proxy timeout changes and standard OneBot forward changes predate this step. Bridge has unrelated handler/image/repository/test modifications. ExHentai has statistics and gallery work in progress. Do not reset, stage wholesale, or overwrite them. Compilation/test workers: 20 on the currently idle 20-core host.
