# Design

## Context

See `proposal.md` for motivation and scope. Startup and reload both use `RuntimeGenerationBuilder::build`, whose ordered actor loop calls `ActorPackageStager::stage` before contract discovery. Staging currently calls `resolve_needed` for each dependency; every non-absolute dependency causes another `patchelf --print-rpath` process. Each query reads the entire consumer ELF, including non-loadable debug sections. Process-owned dependency SHA-256 digests are also recalculated for each actor.

Investigation evidence from the current Debug artifacts (warm-cache offline measurements, not an instrumented production reload):

| Actor | Size (MiB) | RPATH calls per stage | Repeated-query time |
|---|---:|---:|---:|
| exhentai_fetch | 76.5 | 83 | 3.423 s |
| message_store | 11.9 | 82 | 0.572 s |
| bridge | 157.9 | 82 | 6.757 s |
| chat_llm | 74.9 | 83 | 3.457 s |

`strace` confirmed a 165,560,504-byte read for a single bridge RPATH query. The four repeated-query loops read approximately 25.9 GiB into userspace; this is not a claim of equivalent physical disk traffic. Hashing the 281.8 MiB core library took about 170 ms per observation. These measurements identify the bottleneck but do not establish post-change end-to-end latency.

The existing `actor-runtime-reload` and `package-release-closures` specs remain authoritative, especially private SONAME isolation, candidate verification before loading, and process-owned identity checks. `tests/` currently contains no files; no existing staging/reload test suite is assumed.

## Goals / Non-Goals

**Goals:**
- Reduce source-RPATH subprocess work from once per dependency edge to at most once per closure object.
- Hash an unchanged process-owned file only once per generation-build attempt, while comparing every observed dependency against the active identity as before.
- Keep caches short-lived and limited to metadata/digests; preserve staged file ownership and cleanup.
- Demonstrate reduced operation counts as well as elapsed time, without touching the running core or starting business actors during measurements.

**Non-Goals:**
- Persistent caches, new configuration, native ELF parsing, parallel staging, or changing `fork`/`exec` mechanics.
- Actor ABI or public dependency-identity representation changes.
- Removing actor/private-dependency hashing, copy steps, post-rewrite verification, or process-owned compatibility gates.
- Establishing an atomic filesystem snapshot or supporting concurrent in-place mutation of loaded libraries. This change must not claim to solve existing hash/copy races.

## Decisions

### 1. Resolve a consumer's search directories once

For each source closure object, lazily construct a search-directory list on its first non-absolute dependency. Keep the consumer's parent first, then parsed RPATH entries in their existing order, expanding both `$ORIGIN` and `${ORIGIN}` relative to that consumer. Pass the prepared directories to dependency resolution rather than invoking `patchelf` inside every edge lookup.

Absolute dependencies retain the direct canonicalization path and need not trigger RPATH inspection. Preserve existing empty/failed RPATH handling (parent-directory fallback) and retain canonicalization, private-boundary classification, and process-owned classification on each edge. Keep per-object data safe from `objects` vector reallocation as closure discovery appends dependencies.

This is preferable to a native ELF parser because it removes the demonstrated multiplicative cost without introducing another ELF implementation. Source metadata reuse does not replace inspection of rewritten staged files: existing `--print-needed` and SONAME verification still run against staged outputs.

### 2. Use an explicit generation-build-local digest context

Introduce an internal staging context shared by the stager calls in one `RuntimeGenerationBuilder::build` invocation. Construct it before the actor loop and destroy it on all build exits; never store it in the active generation, a static variable, or the reload controller. Standalone staging must use a fresh local context rather than a hidden global cache. Keep context plumbing separate from `ProcessOwnedDependencyIdentity` and from configuration options.

Cache only successful process-owned file digests. A cache lookup is keyed by canonical path and validated with file metadata including device, inode, size, nanosecond modification time, and change time. Canonicalize and inspect current file identity on each use, so path replacement and normal in-place edits invalidate reuse. Do not cache lookup-by-SONAME or skip `loaded_library_path`: the identity being observed must still be selected using the existing loaded-library-first logic.

On a miss, hash the file and compare metadata before and after reading before admitting the digest to the cache. If metadata cannot be obtained reliably, use the uncached path. If metadata changes during hashing, do not cache that result. This guard prevents publishing an unstable digest for later reuse; it does not add a new atomic-snapshot promise to the existing hashing path. Do not cache missing files or failed digest reads.

Every edge still compares its observed digest with `expected_process_owned_dependencies`; expected values from the active generation are not proof of current contents. Conflict merging and stable failure codes remain unchanged. A later startup/validation/reload build starts with an empty cache, even if it has the same generation ID as a previous failed attempt.

A process-global cache was rejected because it risks accepting stale core/runtime identities across reloads. Caching all private/actor hashes is deferred because those hashes determine staged content identities and require additional source-copy consistency analysis.

### 3. Keep verification evidence offline and reproducible

Use a staging-only diagnostic/benchmark entrypoint invoking the real stager against isolated copies of the four investigated artifacts and their correct dependency layout. Do not instantiate actors, connect bots, use production databases, or invoke online reload. Any diagnostic helper is a measurement tool, not a new unit-test suite. Record its invocation and limitations in the change's evidence document.

Compare baseline and optimized builds with identical artifacts, dependency resolution context, toolchain, filesystem, and warm-cache conditions. Use at least three untraced runs for medians; collect subprocess/read traces separately because tracing changes wall time. Record artifact sizes and dependency counts, total stage time, RPATH subprocess count, and digest cache hits/misses or hash-call observations.

Deterministic acceptance criteria:
- The four zero-private-dependency actors require four source-RPATH queries total rather than 330; absolute-only dependency lists require none.
- An unchanged shared process-owned file is hashed once per build context; a fresh context observes it again.
- Replaced or modified files do not reuse prior cached entries, and mismatch checks still reject incompatible candidates.
- Private dependency rewriting and staged-output verification remain present and are exercised in an isolated package where available.

For the measured four-actor workload, target at least an 80% reduction in median staging wall time against an equivalent offline baseline. This is an engineering target, not a portable runtime SLA; if not achieved, record phase measurements and explain the remaining cost rather than inferring success from operation counts. No unit tests will be authored; missing coverage must be recorded honestly. Manual checks cover empty/missing RPATH behavior, absolute and `$ORIGIN` dependencies, repeated public dependencies, file replacement, changed expected digests, and failure cleanup. Existing integration checks may be run where relevant but are not substitutes for absent staging coverage.

## Risks / Trade-offs

- [Filesystem metadata is not cryptographic proof of immutability] → Limit reuse to one attempt, check nanosecond mtime/ctime plus inode/device/size, fall back when identity is unavailable, and retain the operational prohibition on modifying live core libraries. Do not present this as protection from adversarial filesystem mutation.
- [Caching changes error handling or resolution order] → Retain existing fallback paths, loaded-library preference, search order, and edge-by-edge identity checks; review these explicitly.
- [Source inspection accidentally reused for staged output] → Keep rewritten-output verification outside the source inspection cache.
- [Offline timing overstates production benefit] → Measure the real stager in equivalent contexts, label warm-cache conditions, and avoid promises about cold storage, fork cost in a large live process, or actor preparation time.
- [No staging unit suite is available] → Perform and document isolated manual checks without adding unit tests; clearly mark any unexercised scenario.
- [Core rebuild replaces a running DSO] → Build core in a separate output tree. If actors must be rebuilt, use the frozen-SDK actor build workflow, never actor targets in the live workspace.

## Migration Plan

No configuration or data migration is required. Implement and validate in an isolated core build, then provide the evidence for operator review. Installing the new core/SDK and restarting the process are separately authorized operations; actor reload alone cannot deploy this core change. Rollback uses the previous coordinated core/SDK artifacts and a separately approved restart, not overwriting the running `libobcx_core.so`.
