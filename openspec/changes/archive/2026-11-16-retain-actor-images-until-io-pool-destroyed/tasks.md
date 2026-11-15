# Tasks

## 1. Lifetime fix and contract

- [x] 1.1 Audit pool ownership, reverse member destruction, lifecycle/service holders, and all library-close paths; record the conclusion and remove only the early retirement conditional, verified by the source diff.
- [x] 1.2 Document the policy and complete safe-lifetime delta requirement with all original scenarios; verify with `openspec validate retain-actor-images-until-io-pool-destroyed --strict`.
- [x] 1.3 Run root `treefmt` while excluding protected actor repositories and real runtime data; verify no unrelated source changes.

## 2. Build and integration verification

- [x] 2.1 Rebuild `obcx_core` and `obcx` with at least 8 jobs; verify successful linking and confirm actors do not need relinking.
- [x] 2.2 Discover and run existing reload/generation/manager tests with at least 8 jobs; record results or explicitly record an empty inventory. Do not add unit tests.
- [x] 2.3 Run the existing socket-probe, template, and persona two-reload/SIGTERM cases and socket-probe/persona five-reload serving cases against the rebuilt core; verify reload completion, clean exit, and loaded core path in fresh `/tmp` directories.

## Verification results

- `cmake --build build --target obcx_core obcx --parallel 20`: passed; both shared core and executable linked. Stale Nix dependency cache paths required rediscovery before building; no build-system source edits or actor relinking.
- `ctest --test-dir build -N` and the existing `build/actor-dev` inventory: zero tests; `tests/` is empty. `ctest --test-dir build -j 20 --output-on-failure -R 'actor.*reload|runtime.*generation|actor.*manager'` reported `No tests were found!!!`; this is not a unit-test pass.
- Existing harness, run with a 20-worker executor and preserved actor binaries: socket-probe/template/persona each completed two reloads and SIGTERM; socket-probe/persona each completed five reloads with serving/liveness checks and SIGTERM. All five inferiors exited 0, with no SIGSEGV; process maps confirmed the rebuilt shared core.
- Root `treefmt` passed, zero changes, with `TREEFMT_EXCLUDES='local_actor/**,local_library/**,*.toml,**/*.toml,*.nix,**/*.nix'` to protect actor repositories and real runtime configuration. Only C++ changed, so no Nix/TOML formatting was needed.
- Strict OpenSpec validation and `git diff --check` passed. Original safe-lifetime requirement text and all five original scenarios were preserved.
- Full commands, audit, build logs, process maps, GDB logs, and results: `/tmp/obcx-dso-fix/FIX-REPORT.md`, `/tmp/obcx-dso-fix/fixed-matrix.json`, and the per-case directories recorded there. Baseline binaries remain unchanged.
