## Context

The user explicitly requests removal of the package lock, not automatic maintenance of it. The existing resolver visits only explicitly bound sources. Development path packages freeze metadata rather than source content; remote inputs already require complete commits. Nix owns the third-party environment. The original `declarative-package-dependencies` lock/frozen design is superseded by this change; its unrelated validation and release work remains intact.

## Goals / Non-Goals

**Goals:** One CMake configure resolves current metadata, enforces the same substantive checks and produces disposable build state. Delete the package-lock model and its public commands, parameters, schema, install entry and receipt field. Preserve all pre-existing user changes.

**Non-Goals:** No implicit configuration values, automatic downloads, version selection, relaxed provenance, old-format compatibility, Nix/vcpkg lock removal, removal of process synchronization, deployment or commit. Bare CMake still requires explicit workspace/cache/mode/state/configuration, supplied by existing presets.

## Decisions

1. Flatten `resolved-packages` into one current graph: workspace digest, platform/profile/mode/roots, complete package nodes and content receipts, providers, edges, order and unused sources. Remove the nested `lock` and `lock_sha256`; build receipts retain only `graph_sha256`. A generated graph is not compared against an earlier approved snapshot and cannot block a valid metadata change.
2. CMake calls the resolver offline from `plan`, then performs compiler/platform/provider checks and writes its graph and plan under the explicitly configured state directory. Remove `LOCK` and prepared `GRAPH` inputs. Keep workspace, package metadata, providers and tool sources as reconfigure dependencies; generated graphs are outputs, not configure prerequisites. Track selected Git-source metadata as well as local package metadata and retain input/output containment checks.
3. The CLI keeps `resolve`, `prepare`, `check`, `explain` and schema/registry operations, without a lock parameter or command. Graph export is optional tooling, not required by CMake. Exporters resolve the current workspace themselves; no stale graph file becomes an alternate authority.
4. Keep atomic writes and graph/cache process guards. Avoid rewriting identical generated content where practical. This is synchronization, unrelated to dependency version locking. Do not run concurrent Ninja processes against a build directory.
5. Migrate SDK smoke preparation to create explicit workspace/provider declarations only; its CMake invocation resolves internally. Update root and maintained nested documentation, presets, CI and schema snapshots together. Preserve historical archived evidence; annotate the superseded active design rather than rewriting its history.

## Risks / Trade-offs

- Metadata changes no longer require separate acceptance → explicitly requested trade-off; all current dependency constraints and source/provenance checks still run.
- Schema and command breakage → no fallback; regenerate disposable graphs and update all maintained consumers/tests in this change.
- Accidental repeated configuration or compilation → clean configure/build and a second unchanged build must demonstrate no repeated compilation/linking; the existing always-run audit is expected.
- Provider evidence hashes CMake integration files → refresh only the affected local reviewed evidence/hash after these authorized edits, without changing Nix inputs or bypassing provenance.
- Scope overlaps the ongoing package change → clearly record supersession and leave unrelated pending release tasks untouched.

## Migration Plan

Update tooling and consumers atomically in the worktree, remove the obsolete local package lock after preserving a diagnostic copy outside the repository, configure from explicit declarations and rebuild with 20 workers. Validate CLI/schema tests, real isolated CMake fixtures, workspace build/tests and installed SDK consumption. Rollback is a source revision rollback, not a legacy-format reader; no running bot is restarted.

## Open Questions

None for the requested scope.
