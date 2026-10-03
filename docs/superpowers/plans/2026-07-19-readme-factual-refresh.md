# Multi-Repository README Factual Refresh Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace stale information in the OBCX, bridge, message-store, registry, and actor-template root READMEs with commands and contracts verified against the current actor-only implementation.

**Architecture:** Treat `actor.toml`, current CMake/CLI interfaces, and conformance tests as the documentation source of truth. Commit each standalone repository independently, then refresh OBCX's offline actor-source pins, conformance matrix, and evidence so the documentation-only candidate remains reproducible and CI-clean.

**Tech Stack:** Markdown, Git, CMake/CTest, Python 3 metadata tools, POSIX shell restoration scripts, Nix development environment.

---

## File Map

- `README.md`: Chinese operational entry point for OBCX.
- `local_actor/obcx-actor-bridge/README.md`: bridge package, runtime, and bot/DB constraints.
- `local_actor/obcx-actor-message-store/README.md`: persistence actor contract and schema behavior.
- `local_actor/obcx-actor-registry/README.md`: standalone publication registry workflow.
- `local_actor/obcx-actor-template/README.md`: actor-authoring starter workflow.
- `packaging/actors/bundles/bridge.bundle`: offline bridge history including the README commit.
- `packaging/actors/patches/{message-store,registry,template}.patch`: deterministic adaptation streams ending at the README commits.
- `packaging/actors/{restore-sources.sh,apply-patches.sh,README.md}`: fixed revision checks and audit documentation.
- `actor-registry/conformance-matrix.json`: final five-repository source identity.
- `benchmarks/evidence/actor-only-release/*.json`: reports bound to the final matrix and real verification runs.

### Task 1: Refresh The OBCX README

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Record the stale statements that must disappear**

Run:

```bash
rg -n "obcx-actor-example|actor_runtime_gate.py report|--evidence-dir" README.md
```

Expected: the remote package example and unsupported benchmark subcommand are reported.

- [ ] **Step 2: Rewrite the operational sections**

Keep the existing Chinese overview, runtime TOML, actor authoring contract, and
license. Apply these exact content rules:

```markdown
## 构建

```bash
nix develop
cmake --preset actor-dev
cmake --build --preset actor-dev --parallel
ctest --preset actor-dev
```

## 选择 actor package

`actors.toml` 是 package 选择文件；package 自身的身份、ABI、依赖、兼容范围和
发布信息只来自 `actor.toml`。本地 package 使用 `path`，远程 package 必须使用
不可变 tag 或完整 commit revision。配置完成后，以实际 preset 的 binary directory
调用 `cmake/gen_vcpkg_manifest.py`。

## 验证与文档

```bash
ctest --preset actor-dev
python3 scripts/verify_actor_cutover_evidence.py
python3 scripts/generate_api_docs.py
```
```

Also document that name-based actor lookup covers the active build tree and the
installed `<prefix>/lib/obcx/actors` directory, `--validate-config` validates
DB providers/dependencies/pipeline sources and modes without starting runtime
activity, and `packaging/actors/restore-sources.sh` restores the four pinned
standalone repositories without network access.

- [ ] **Step 3: Verify the root README facts**

Run:

```bash
rg -n "CMake 3.30|blocking_workers|requires = \[\"message_store\"\]|verify_actor_cutover_evidence.py|restore-sources.sh" README.md
rg -n "obcx-actor-example|actor_runtime_gate.py report|--evidence-dir" README.md
git diff --check -- README.md
```

Expected: current facts are present, the stale scan has no matches, and the
diff check exits zero.

- [ ] **Step 4: Commit the OBCX README**

```bash
git add README.md
git -c commit.gpgsign=false commit -m "docs: refresh actor-only README"
```

### Task 2: Refresh The Bridge README

**Files:**
- Modify: `local_actor/obcx-actor-bridge/README.md`

- [ ] **Step 1: Replace the ABI, build, DB, and bot statements**

The resulting README must state all of the following explicitly:

```markdown
- `OBCX_ACTOR_EXPORT_V2` exports the numeric ABI generation, factory,
  destructor, actor name, actor version, and generated schema-2 input contract.
- Supported build baseline: Linux x86_64/arm64, CMake 3.30+, GCC 16.1+,
  C++26, `-freflection`, and `__cpp_impl_reflection >= 202506L`.
- Bridge consumes `obcx::message_store::events::MessageStored` and emits
  `bridge::events::MessageForwarded` or
  `bridge::events::MessageForwardFailed`.
- Actor state uses the `DbManager` instance selected by `db = "main"` and
  `db_namespace = "bridge"`; `database_file` is not the actor storage source.
- Runtime configuration declares `requires = ["message_store"]`.
- The current forwarding runtime uses unambiguous platform-only bot lookup;
  configure exactly one live QQ bot and one live Telegram bot. Multiple live
  accounts for one platform make lookup ambiguous and forwarding fails.
```

Retain the feature list, required `bridge_files_dir`, ffmpeg behavior, install
paths, and cross-repository test description. Remove the claim that the actor
has only one exported entry point and remove `database_file` from the TOML
snippet.

- [ ] **Step 2: Verify and commit bridge documentation**

Run:

```bash
rg -n "CMake 3.30|OBCX_ACTOR_EXPORT_V2|requires = \[\"message_store\"\]|exactly one live|DbManager" local_actor/obcx-actor-bridge/README.md
rg -n "one entry point|database_file|CMake 3.25" local_actor/obcx-actor-bridge/README.md
git -C local_actor/obcx-actor-bridge diff --check -- README.md
git -C local_actor/obcx-actor-bridge add README.md
git -C local_actor/obcx-actor-bridge -c commit.gpgsign=false commit -m "docs: refresh actor runtime guidance"
```

Expected: current facts are present, stale scan is empty, and one bridge commit
is created.

### Task 3: Refresh The Message-Store README

**Files:**
- Modify: `local_actor/obcx-actor-message-store/README.md`

- [ ] **Step 1: Add package, runtime, and install contracts**

Preserve the schema-v3 migration warning and add these exact facts:

```markdown
- Actor id `onebot-cxx.message-store`, actor name `message_store`, version
  `0.1.0`, ABI 2, target `message_store_actor`, artifact `message_store.so`.
- Linux x86_64/arm64, CMake 3.30+, GCC 16.1+, C++26 reflection baseline.
- Runtime uses an injected core `DbManager`; the actor config selects `db =
  "main"` and `db_namespace = "message_store"`.
- Input is `obcx::core::events::RawMessageEvent`; outputs are
  `obcx::message_store::events::MessageStored` and
  `obcx::message_store::events::MessageStoreFailed`.
- Installation writes `lib/obcx/actors/message_store.so` and
  `share/obcx/actors/onebot-cxx.message-store/actor.toml`.
```

Include a minimal DB/actor/pipeline TOML example and retain the build plus
`message_store_smoke` CTest command.

- [ ] **Step 2: Verify and commit message-store documentation**

```bash
rg -n "CMake 3.30|onebot-cxx.message-store|MessageStoreFailed|db_namespace|share/obcx/actors" local_actor/obcx-actor-message-store/README.md
git -C local_actor/obcx-actor-message-store diff --check -- README.md
git -C local_actor/obcx-actor-message-store add README.md
git -C local_actor/obcx-actor-message-store -c commit.gpgsign=false commit -m "docs: document current message-store contract"
```

### Task 4: Refresh The Registry README

**Files:**
- Modify: `local_actor/obcx-actor-registry/README.md`

- [ ] **Step 1: Clarify current registry behavior**

Keep the current standalone commands and add:

```markdown
The checked-in entries are `onebot-cxx.message-store` and `vollate.bridge`.
`actor.toml` is the only accepted metadata dialect. Validation rejects unknown
fields, boolean schema versions, unsafe list delimiters, unsupported ABI or
toolchain ranges, and undeclared artifact platforms. The generated index is
deterministic and publishes only assets for platforms listed in
`artifact.platforms`.

The OBCX conformance matrix binds this repository with core, bridge,
message-store, and actor-template. Standalone registry CI validates the vendored
metadata validator; OBCX verifies it remains byte-identical to the SDK copy.
```

- [ ] **Step 2: Verify and commit registry documentation**

```bash
python3 local_actor/obcx-actor-registry/generate_actor_index.py validate
python3 local_actor/obcx-actor-registry/generate_actor_index.py generate --check
git -C local_actor/obcx-actor-registry diff --check -- README.md
git -C local_actor/obcx-actor-registry add README.md
git -C local_actor/obcx-actor-registry -c commit.gpgsign=false commit -m "docs: refresh actor registry contract"
```

Expected: validation succeeds, the index is current, and the README commit is
created.

### Task 5: Refresh The Actor-Template README

**Files:**
- Modify: `local_actor/obcx-actor-template/README.md`

- [ ] **Step 1: Replace the obsolete toolchain and dependency workflow**

Use this current contract:

```markdown
- Linux x86_64/arm64, CMake 3.30+, GCC 16.1+, C++26,
  `-freflection`, `__cpp_impl_reflection >= 202506L`.
- `OBCX_ACTOR_EXPORT_V2` supplies ABI generation, factory, destructor, name,
  version, and schema-2 input contract.
- `actor.toml` is the canonical identity/dependency/publication document.
- Package dependencies use `[dependencies].packages`; actor dependencies use
  `[dependencies].actors`; the consuming OBCX checkout merges dependencies
  from its selected `actors.toml`.
- The standalone template repository does not contain or invoke
  `cmake/gen_vcpkg_manifest.py`.
```

Keep the create/build/install/layout sections and add a minimal `[actors.example]`
plus pipeline configuration using the fully qualified example input type.

- [ ] **Step 2: Verify and commit template documentation**

```bash
rg -n "CMake 3.30|OBCX_ACTOR_EXPORT_V2|consuming OBCX|actors.example|pipelines" local_actor/obcx-actor-template/README.md
rg -n "CMake 3.25|python3 cmake/gen_vcpkg_manifest.py" local_actor/obcx-actor-template/README.md
git -C local_actor/obcx-actor-template diff --check -- README.md
git -C local_actor/obcx-actor-template add README.md
git -C local_actor/obcx-actor-template -c commit.gpgsign=false commit -m "docs: refresh actor template guidance"
```

Expected: stale scan is empty and the template README commit is created.

### Task 6: Refresh Offline Actor Sources

**Files:**
- Modify: `packaging/actors/bundles/bridge.bundle`
- Modify: `packaging/actors/patches/message-store.patch`
- Modify: `packaging/actors/patches/registry.patch`
- Modify: `packaging/actors/patches/template.patch`
- Modify: `packaging/actors/restore-sources.sh`
- Modify: `packaging/actors/apply-patches.sh`
- Modify: `packaging/actors/README.md`

- [ ] **Step 1: Generate deterministic source artifacts**

```bash
git -C local_actor/obcx-actor-bridge bundle create ../../packaging/actors/bundles/bridge.bundle refs/heads/develop
git -C local_actor/obcx-actor-message-store format-patch --stdout --full-index 3a9dfc2b27375d22531b4308356b75f4bac7077f..HEAD > packaging/actors/patches/message-store.patch
git -C local_actor/obcx-actor-registry format-patch --stdout --full-index 057b46522872bfbc2dd87435e3751b8d2001e26b..HEAD > packaging/actors/patches/registry.patch
git -C local_actor/obcx-actor-template format-patch --stdout --full-index 993048e6d7e280167cc2189a51464d8fd9197c68..HEAD > packaging/actors/patches/template.patch
```

Update both restoration scripts and `packaging/actors/README.md` with the exact
four `git rev-parse HEAD` values produced by the standalone repositories.

- [ ] **Step 2: Restore and compare all pinned repositories**

Run from the OBCX root with an absent `/tmp/obcx-readme-actor-restore`:

```bash
OBCX_ACTOR_SOURCE_ROOT=/tmp/obcx-readme-actor-restore sh packaging/actors/restore-sources.sh
cmp local_actor/obcx-actor-bridge/README.md /tmp/obcx-readme-actor-restore/obcx-actor-bridge/README.md
cmp local_actor/obcx-actor-message-store/README.md /tmp/obcx-readme-actor-restore/obcx-actor-message-store/README.md
cmp local_actor/obcx-actor-registry/README.md /tmp/obcx-readme-actor-restore/obcx-actor-registry/README.md
cmp local_actor/obcx-actor-template/README.md /tmp/obcx-readme-actor-restore/obcx-actor-template/README.md
```

Expected: restoration prints no revision error and every `cmp` exits zero.

- [ ] **Step 3: Commit the offline source refresh**

```bash
git add packaging/actors
git -c commit.gpgsign=false commit -m "build: refresh pinned actor documentation sources"
```

### Task 7: Refresh The Final Candidate Matrix

**Files:**
- Modify: `actor-registry/conformance-matrix.json`

- [ ] **Step 1: Generate and verify the final matrix**

```bash
python3 tests/actor_conformance_matrix.py generate --recorded-date 2026-07-19
python3 tests/actor_conformance_matrix.py check
```

Expected: both commands identify a valid pinned five-repository matrix.

- [ ] **Step 2: Commit the matrix**

```bash
git add actor-registry/conformance-matrix.json
git -c commit.gpgsign=false commit -m "test: pin refreshed actor documentation candidate"
```

### Task 8: Reproduce And Refresh Release Evidence

**Files:**
- Modify: `benchmarks/evidence/actor-only-release/clean-machine-verification.json`
- Modify: `benchmarks/evidence/actor-only-release/rollback-report.json`
- Modify: `benchmarks/evidence/actor-only-release/post-cutover-sanitizers.json`
- Modify: `benchmarks/evidence/actor-only-release/clean-machine-soak-rollback.json`

- [ ] **Step 1: Run clean Release, installed actors, and 100,000-message soak**

```bash
nix develop --command python3 scripts/verify_actor_release.py \
  --work-dir /tmp/obcx-readme-release-20260719 \
  --soak-messages 100000 --jobs 4 \
  --report benchmarks/evidence/actor-only-release/clean-machine-verification.json
```

Expected: standalone repositories pass, installed `obcx --version` succeeds,
and the soak prints `messages=100000 persisted=100000 forwarded=100000 failures=0`.

- [ ] **Step 2: Rerun ASan/UBSan and TSan evidence commands**

Configure dedicated sanitizer trees, build the recorded targets, and run the
exact suites and fixed-seed stress workloads:

```bash
nix develop --command cmake -S . -B /tmp/obcx-review-asan -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DOBCX_BUILD_TESTS=ON \
  -DOBCX_BUILD_BENCHMARKS=ON \
  '-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' \
  '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' \
  '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined' \
  '-DCMAKE_SHARED_LINKER_FLAGS=-fsanitize=address,undefined'
nix develop --command cmake --build /tmp/obcx-review-asan --target \
  actor_work_stealing_executor_test native_actor_scheduler_test \
  actor_asio_interop_test actor_task_test actor_scheduler_v2_stress --parallel 4
nix develop --command env \
  ASAN_OPTIONS=detect_leaks=0:strict_string_checks=1:check_initialization_order=1:halt_on_error=1 \
  UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
  ctest --test-dir /tmp/obcx-review-asan --output-on-failure \
  -R '^(ActorTaskTest|ActorWorkStealingExecutorTest|NativeActorSchedulerTest|ActorAsioInteropTest)'
nix develop --command env \
  ASAN_OPTIONS=detect_leaks=0:strict_string_checks=1:check_initialization_order=1:halt_on_error=1 \
  UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
  /tmp/obcx-review-asan/benchmarks/actor_scheduler_v2_stress \
  --invocations 200000 --batch 10000 --seed 0x4f424358

nix develop --command cmake -S . -B /tmp/obcx-review-tsan -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DOBCX_BUILD_TESTS=ON \
  -DOBCX_BUILD_BENCHMARKS=ON \
  '-DCMAKE_C_FLAGS=-fsanitize=thread -fno-omit-frame-pointer' \
  '-DCMAKE_CXX_FLAGS=-fsanitize=thread -fno-omit-frame-pointer' \
  '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=thread' \
  '-DCMAKE_SHARED_LINKER_FLAGS=-fsanitize=thread'
nix develop --command cmake --build /tmp/obcx-review-tsan --target \
  actor_work_stealing_executor_test native_actor_scheduler_test \
  actor_asio_interop_test actor_scheduler_v2_stress --parallel 4
nix develop --command env \
  TSAN_OPTIONS=halt_on_error=1:second_deadlock_stack=1:history_size=7 \
  ctest --test-dir /tmp/obcx-review-tsan --output-on-failure \
  -R '^(ActorWorkStealingExecutorTest|NativeActorSchedulerTest|ActorAsioInteropTest)'
nix develop --command env \
  TSAN_OPTIONS=halt_on_error=1:second_deadlock_stack=1:history_size=7 \
  /tmp/obcx-review-tsan/benchmarks/actor_scheduler_v2_stress \
  --invocations 200000 --batch 10000 --seed 0x4f424358
```

Require 46 ASan/UBSan tests, 39 TSan tests, 1,008,192 transitions, 200,000
completions, 2,048 cancellations, 2,048 late completions, and zero pending
work. Recompute every recorded executable SHA-256 and bind
`post-cutover-sanitizers.json` to the new matrix revision.

- [ ] **Step 3: Rehearse rollback and assemble the summary**

Use the installed candidate from the clean run and the existing merge-base
install to rehearse rollback:

```bash
previous_revision=$(git merge-base HEAD origin/main)
candidate_identity=$(python3 -c 'import json; print(json.load(open("actor-registry/conformance-matrix.json"))["repositories"]["core"]["revision"])')
nix develop --command python3 scripts/rehearse_actor_release_rollback.py \
  --candidate /tmp/obcx-readme-release-20260719/build/actor-package-conformance/sdk \
  --previous /tmp/obcx-rollback-03b0f77/install \
  --work-dir /tmp/obcx-readme-rollback-20260719 \
  --previous-revision "$previous_revision" \
  --candidate-identity "$candidate_identity" \
  --candidate-health-messages 1000 \
  --report benchmarks/evidence/actor-only-release/rollback-report.json
sha256sum benchmarks/evidence/actor-only-release/clean-machine-verification.json benchmarks/evidence/actor-only-release/rollback-report.json
```

Update `clean-machine-soak-rollback.json` from the generated clean and rollback
report fields and these SHA-256 values; do not invent timings, binary hashes,
or statuses.

- [ ] **Step 4: Verify and commit evidence**

```bash
python3 scripts/verify_actor_cutover_evidence.py
git add benchmarks/evidence/actor-only-release
git -c commit.gpgsign=false commit -m "test(release): refresh README candidate evidence"
```

Expected: verifier prints `valid actor-only cutover evidence bundle` before
the commit.

### Task 9: Final Documentation And Repository Verification

**Files:**
- Verify all files above; no new modifications expected.

- [ ] **Step 1: Check relative Markdown links and stale phrases**

Run a read-only Python link scan over the five README files, resolving links
relative to each repository root. Then run:

```bash
rg -n "CMake 3.25|actor_runtime_gate.py report|--evidence-dir|obcx-actor-example|one entry point" README.md local_actor/*/README.md
git diff --check 79b97de..HEAD
```

Expected: no stale matches and no whitespace errors.

- [ ] **Step 2: Run final root checks**

```bash
python3 tests/actor_conformance_matrix.py check
python3 scripts/verify_actor_cutover_evidence.py
nix develop --command ctest --test-dir build/actor-dev --output-on-failure --parallel 2
```

Expected: matrix and evidence are valid and all configured CTests pass.

- [ ] **Step 3: Audit all worktrees**

```bash
git status --short
git -C local_actor/obcx-actor-bridge status --short
git -C local_actor/obcx-actor-message-store status --short
git -C local_actor/obcx-actor-registry status --short
git -C local_actor/obcx-actor-template status --short
```

Expected: every command prints nothing.
