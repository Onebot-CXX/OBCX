# Tasks

## 1. Isolated measurement baseline

- [x] 1.1 Establish an isolated core build and staging-only diagnostic invocation using copied actor artifacts; verify that output paths do not overlap the live core workspace and that no actor instances, bot connections, or production databases are opened. Use at least six parallel compilation workers, and all available cores when load is low; any required hot-reloadable actor build must use `scripts/actor_build.py` with a frozen installed SDK.
- [x] 1.2 Capture at least three baseline staging runs plus a separate subprocess/read trace; deliver `evidence.md` in this change with exact commands, toolchain/artifact identities, sizes, dependency counts, medians, and the distinction between earlier RPATH-loop measurements and actual full staging measurements. Do not add unit tests.

## 2. Reuse source RPATH inspection

- [x] 2.1 Refactor source search-directory preparation and dependency resolution in `actor_package_stager.cpp` so each closure object queries RPATH at most once; verify by trace that the four measured actors issue four source-RPATH queries instead of 330, while staged `--print-needed` verification still executes.
- [x] 2.2 Perform isolated manual checks of absolute dependencies, `$ORIGIN` and `${ORIGIN}`, search-order precedence, empty/missing RPATH, and closure growth; record commands/outcomes and any unavailable scenario in `evidence.md`, and review that vector growth cannot invalidate retained metadata references. Verify absolute-only resolution performs no RPATH query.

## 3. Reuse guarded process-owned digests

- [ ] 3.1 Introduce explicit per-build staging-context ownership and integrate it into the ordered actor loop in `runtime_generation.cpp`; verify by source inspection and diagnostic runs that startup, validation-only, and reload builds each receive a new context, standalone staging is isolated, and failed builds do not retain cache entries.
- [ ] 3.2 Implement successful-digest caching guarded by canonical path, device/inode, size, and nanosecond mtime/ctime, with pre/post-hash stability checks and uncached fallback; verify hash-call observations show one hash per unchanged shared process-owned file in a context and a new hash in a fresh context. Keep loaded-library resolution and expected-digest comparison on every edge.
- [ ] 3.3 Manually exercise repeated-file reuse, atomic path replacement, same-size content modification, unavailable metadata/digest failure paths where feasible, and an expected-digest mismatch using disposable files; deliver results and cache-lifetime/guard documentation in `evidence.md`. Verify incompatible identities retain the existing failure code, and source review confirms failed or unstable observations cannot become reusable entries.

## 4. Integrated staging acceptance

- [ ] 4.1 Run the optimized isolated build through the same complete staging workload and at least three untraced timing runs; record before/after medians, query counts, and hash observations in `evidence.md`, targeting at least 80% lower median staging wall time. If the target is missed, record phase-level evidence and the remaining bottleneck rather than declaring an unmeasured speedup.
- [ ] 4.2 Check private dependency content-versioning, rewritten-edge/SONAME verification, process-owned mismatch rejection, and failed-stage cleanup with isolated artifacts where available; record exercised cases and explicit coverage gaps. Review against the unchanged `package-release-closures` and `actor-runtime-reload` requirements without adding unit tests or triggering production reload.
- [ ] 4.3 Verify the final isolated core build succeeds and any applicable existing integration checks pass using at least six workers (all available cores when load is low); record missing test coverage, confirm no actor ABI/configuration/default changes, and run `openspec validate optimize-actor-package-staging --strict`.

## Workflow follow-up

- Review the implementation and evidence before archiving the change.
- Core/SDK installation and process restart remain separately authorized operator operations; actor reload cannot deploy this core optimization.
- Run `treefmt` from the repository root before any commit, inspect formatting changes across repositories, and follow the repository signing-reminder requirements if unsigned commits are requested.
