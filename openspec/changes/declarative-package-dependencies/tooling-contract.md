# Implemented v2 data tooling and core build

The root CMake pipeline now consumes the v2 graph. Maintained CMake entry points and seven package manifests use the canonical contract; the current four selected actors are built and audited through it. Legacy readers/helpers and actor-only schemas are removed; registry local validation now delegates to this tooling. Complete release/container workflow cutover remains open. The v2 parser never reads v1 as a compatibility format.

## Canonical implementation

- `cmake/package_tool.py`: CLI entry point.
- `cmake/obcx_package/contracts.py`: sole schema definition and stdlib-only structural/semantic validator. Generated `schemas/{package,workspace,packages-lock,resolved-packages,package-build-receipt,provider-environment,provider-receipt}.schema.json` snapshots are checked against it in tests, not independently maintained rules.
- `versions.py`: actual SemVer comparison, explicit comparator conjunctions and same-core prerelease admission; numeric provider versions have a separate explicitly selected scheme. No version selection or fuzzy matching.
- `sources.py`: path and pinned HTTPS Git sources, full content identities, atomic cache publication, no hooks/LFS/submodule downloads. Git blobs come from raw objects rather than checkout/archive filters. A mutable/tampered cache is an error, not a reason to refetch silently.
- `resolver.py`: canonical metadata, profile-scoped transitive graph, all incoming constraints, ownership/export conflicts, stable topology and explain chains.
- `io.py`: canonical serialization, process locks and fsync+atomic replacement.
- `providers.py` / `OBCXPackageProviders.cmake` / `package_cmake.py`: explicit provider discovery, actual version/target checks and environment/receipt evidence for core development integration.
- `build_inputs.py` / `gen_vcpkg_manifest.py`: prepared graph plus frozen-lock verification and explicit vcpkg port/baseline export, without package CMake execution.
- `cmake_plan.py` / `OBCXPackages.cmake` / `target_audit.py`: offline graph import, v2 artifact/internal/test helpers, sealed provider closures and mandatory deferred/generated/File API dependency checks. This component now drives the real root build and the existing installed-SDK smoke, as well as focused fixtures.

Only Python 3.11+ standard library and Git are required for these data operations. The installed SDK includes the same modules and generated schemas; an installed-tool smoke runs with no source checkout on PYTHONPATH. Registry local consumption is migrated, with an explicit tool path and protocol-version check. Approved published tooling pins remain deferred (task 2.3 remains open).

## Explicit fields

The authoritative exhaustive field sets are in the generated schemas. Every applicable field is required and unknown fields are rejected. Package branches:

- `package`: id/name/version/kind.
- `actor` (actor only): name/ABI=2/entrypoint/input_contract_schema=2.
- `artifact`: kind/name/target/export_target/platforms. Actors are shared; libraries support static/shared/header-only.
- `dependencies`: libraries/actors/system arrays; `test_dependencies`: libraries/system arrays.
- `compatibility`: cpp_standard, `{compiler.id, compiler.version}`, stdlib, cxx11_abi, PIC. Actors additionally declare OBCX range, ABI min/max and reflection macro. Pure libraries do not acquire an SDK dependency from their compiler contract.
- `publication`: repository/homepage/license/description. URLs exclude inline authentication/query credentials.

Workspace sources and providers are separate. Path sources explicitly specify `provenance.kind = "working-tree"`, a fixed Git identity, or a fixed archive path+digest; source code and release records are not guessed from directory names. Providers declare kind/package/components/targets/version/version_scheme, mandatory version_probe/package_manager bindings and a provenance file+digest. Resolver verifies that anchor and declared constraints. **The CMake provider component separately verifies actual targets/versions and environment prefixes; this does not make the resolver a compiler/ABI verifier.** The CMake component now checks actual platform/compiler/ABI and package target/link/usage graphs. The real root integration passes; final build receipts remain pending. The user rejected additional flake changes or a duplicate Nix dependency-list generator; existing flake inputs remain responsible for that environment. Its entry points, generation-time boundary and trust limits are documented in `docs/architecture/package-cmake.md`. See `docs/architecture/package-providers.md` for environment inputs, unversioned receipts, vcpkg preparation and current cutover boundaries.

All workspace paths are relative to `packages.toml`, never cwd; Git subdirs may not escape. Platform is Linux x86_64 or arm64. Profile is production or tests. Empty roots are explicit SDK-only selection. Unreachable sources are not fetched.

## Commands available now

All mode/network/cache/lock/output choices must be supplied explicitly. Replace the illustrative paths with the desired workspace and state directories:

```sh
python3 cmake/package_tool.py validate path/to/package.toml --kind package
python3 cmake/package_tool.py inspect path/to/packages.toml --kind workspace

python3 cmake/package_tool.py lock \
  --workspace path/to/packages.toml --lock state/packages.lock \
  --graph state/resolved-packages.json --cache state/source-cache \
  --mode development --network deny

python3 cmake/package_tool.py resolve \
  --workspace path/to/packages.toml --lock state/packages.lock \
  --graph state/resolved-packages.json --cache state/source-cache \
  --mode development --network deny

python3 cmake/package_tool.py check \
  --workspace path/to/packages.toml --lock state/packages.lock \
  --cache state/source-cache --mode development --network deny

python3 cmake/package_tool.py explain example.mapping \
  --workspace path/to/packages.toml --lock state/packages.lock \
  --cache state/source-cache --mode development --network deny
```

`lock` alone updates the lock. `resolve`, `prepare`, `check` and `explain` are frozen and never rewrite it. `prepare` additionally exports the selected system bindings; it does not install system packages. Changing network to `allow` explicitly permits missing pinned Git source fetches. No command executes CMake or actor code.

Locks contain normalized metadata/workspace identities, selected source identities, edges, providers and topology; no timestamps, machine absolute paths or cache paths. Resolved graphs additionally contain local absolute source directories and per-source content receipts, so they are build-local output rather than portable source locks. A source receipt is **not** a final build receipt: actual flags/compiler/provider/ABI/files still need task 5.7 and release verification.

## Deferred standalone/release boundary — not a core development blocker

The initial implementation prevents cache, lock and graph outputs from living inside package source roots. This prevents hashing generated output into its own source receipt; a release Git lock that pins its own containing commit also creates a commit/lock self-reference. It works for the root multi-repository workspace but imposes an awkward restriction on a standalone repository that keeps its workspace and dependency lock next to package.toml.

The user has deferred standalone SDK publication/consumption while the project is in development. No alternative source-lock policy was approved; the previous request to decide it before continuing core work is withdrawn. The current root workspace can use the existing explicit development/working-tree mode, with graph/cache outside individual actor/library source directories, without creating another repository or requiring clean commits.

Do not silently declare the current restriction the final standalone workflow. Revisit it when that deferred work is actually resumed. Any future distinction between locked dependency provenance and local source build receipts must be specified and tested then; no arbitrary source ignore patterns or implicit configuration defaults are authorized now.
