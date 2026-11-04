# Staging optimization evidence

## Environment and isolation

- Source baseline: `f00a41b89f802bd9cbc367ab3855d5f776d6bfd8` (unrelated pre-existing `.envrc` and `AGENTS.md` changes left untouched).
- Linux x86_64, GCC 16.2.0, CMake 4.4.4, patchelf 0.19.2; 20 logical CPUs, 46 GiB RAM. Build used `--parallel 20` with initially low system load.
- Isolated work root: `/home/vollate/Codes/OBCX/build/staging-optimization` (`$W` below). Core output is `$W/core`, NOT the running process's `build/src/libobcx_core.so`.
- Four actor DSOs were copied unchanged into `$W/artifacts`; no actor targets were built. Original live artifact hashes are recorded in `$W/results/original-sha256.txt` for a final integrity check.
- `benchmarks/actor_package_staging_probe.cpp` calls only the real package stager, reports identities/timings, and destroys staged packages. It does not discover/activate actors or read production configuration. Interactive mode supports manual staging and expected-identity checks on disposable files; it is not a unit-test suite.
- The baseline core and probe were built from the unchanged stager implementation in the current shell, then copied to `$W/baseline-lib/libobcx_core.so` and `$W/baseline-probe`. The probe's RPATH prefers this isolated baseline library. Original stager/header/probe sources were preserved under `$W/baseline-*`.
- Baseline `strace` observed **zero `connect` calls and zero database opens**. No online reload/restart, install, production actor activation, or production database access was performed.

## Reproduction commands

From the repository root, in the same Nix development environment:

```sh
W="$PWD/build/staging-optimization"
mkdir -p "$W"/{baseline-lib,artifacts,results}
cp --reflink=auto build/actors/{exhentai_fetch,message_store,bridge,chat_llm}.so "$W/artifacts/"
sha256sum build/src/libobcx_core.so build/actors/{exhentai_fetch,message_store,bridge,chat_llm}.so > "$W/results/original-sha256.txt"
cmake -S . -B "$W/core" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER="$(command -v g++)" \
  -DENABLE_DEBUG_TRACE=OFF -DOBCX_BUILD_BENCHMARKS=OFF \
  -DOBCX_ENABLE_IO_URING=ON \
  -DOBCX_PACKAGES_WORKSPACE="$PWD/packages.toml" \
  -DOBCX_PACKAGES_CACHE="$W/package-cache" \
  -DOBCX_PACKAGES_STATE_DIR="$W/package-state" \
  -DOBCX_PACKAGES_MODE=development
cmake --build "$W/core" --target obcx_core --parallel 20
cmake -S . -B "$W/core" -DOBCX_BUILD_BENCHMARKS=ON
cmake --build "$W/core" --target actor_package_staging_probe --parallel 20
# Run these snapshot steps BEFORE changing the stager/context implementation.
cp --reflink=auto "$W/core/src/libobcx_core.so" "$W/baseline-lib/"
cp "$W/core/benchmarks/actor_package_staging_probe" "$W/baseline-probe"
patchelf --set-rpath "$W/baseline-lib:$(patchelf --print-rpath "$W/baseline-probe")" "$W/baseline-probe"
"$W/baseline-probe" "$W/stages" batch 3 \
  "$W"/artifacts/{exhentai_fetch,message_store,bridge,chat_llm}.so \
  > "$W/results/baseline.txt"
strace -f -qq -e trace=execve,openat,read,connect \
  -o "$W/results/baseline.strace" \
  "$W/baseline-probe" "$W/stages" batch 1 \
  "$W"/artifacts/{exhentai_fetch,message_store,bridge,chat_llm}.so \
  > "$W/results/baseline-traced.txt"
```

The package workspace is configured only to satisfy core configuration; no actor build target is invoked. If an actor rebuild becomes necessary, it must use the frozen installed SDK workflow instead.

## Full-staging baseline

Untraced complete four-actor runs: **18,636.6 / 16,056.4 / 15,947.3 ms**, median **16,056.4 ms**. This includes package cleanup and probe identity output, unlike the earlier standalone RPATH-query loops. Timing excludes process startup and no page cache was dropped. The first run followed compilation; subsequent runs and the median represent warm-cache behavior.

| Actor | Exact bytes | DT_NEEDED count | Source RPATH calls |
|---|---:|---:|---:|
| exhentai_fetch | 80,209,800 | 84 | 83 |
| message_store | 12,465,536 | 83 | 82 |
| bridge | 165,560,504 | 83 | 82 |
| chat_llm | 78,544,856 | 84 | 83 |

The separate full-staging trace contains **330 RPATH subprocesses** and **8 print-needed subprocesses** (one source and one staged-output inspection per actor). The original investigation's 14.2-second RPATH-loop total and 25.9-GiB repeated userspace read volume are supporting measurements, not this full-stager baseline.

## RPATH implementation and manual checks

- Source directories are lazily prepared once per closure object, in local storage outside the dependency loop. Appending objects cannot invalidate this storage; no reference to an object is held across `objects.push_back`.
- The isolated RPATH-only build succeeded. `$W/results/rpath.strace` reports **4 RPATH queries and 8 print-needed queries**, retaining independent staged-output verification.
- Disposable empty ELF DSOs were produced with `g++ -shared -nostdlib -x c++ /dev/null -o "$W/manual/base.so"`; these are manual package inputs, not actor builds or unit tests. Files were copied and adjusted using patchelf as below.

Manual package layout and commands:

```sh
M="$W/manual"
# Each named .so starts as a copy of $M/base.so.
patchelf --set-soname libchoice.so "$M/package/libchoice.so"
patchelf --set-soname libchoice-alternate.so "$M/package/sub/libchoice.so"
patchelf --set-rpath '$ORIGIN/sub' --add-needed liba.so --add-needed libchoice.so "$M/package/actor.so"
patchelf --set-rpath '${ORIGIN}' --add-needed libb.so "$M/package/sub/liba.so"
patchelf --add-needed "$M/shared/libexternal.so" "$M/absolute/actor.so"
patchelf --add-needed libchoice.so "$M/missing/actor.so"
patchelf --set-rpath '' --add-needed libchoice.so "$M/empty/actor.so"
strace -f -qq -e trace=execve -o "$W/results/manual-rpath.strace" \
  "$W/core/benchmarks/actor_package_staging_probe" "$W/manual-stages" batch 1 \
  "$M"/{package,absolute,missing,empty}/actor.so > "$W/results/manual-rpath.txt"
strace -f -qq -s 4096 -e trace=execve -o "$W/results/absolute.strace" \
  "$W/core/benchmarks/actor_package_staging_probe" "$W/manual-stages" batch 1 \
  "$M/absolute/actor.so" > "$W/results/absolute.txt"
```

Observed outcomes:
- `$ORIGIN/sub` selected `package/sub/liba.so`; its `${ORIGIN}` dependency selected `package/sub/libb.so`, growing the closure from one object to four.
- The actor-parent `package/libchoice.so` won over `package/sub/libchoice.so`, preserving search precedence.
- Both missing-RPATH and explicitly empty-RPATH actors resolved their sibling `libchoice.so`.
- The absolute-only actor resolved the external process-owned dependency with **zero RPATH queries**.
- The three private dependencies in the nested package received content-versioned identities and successfully completed edge/SONAME verification. The combined trace shows ten print-soname calls (source and staged copies for five private dependencies across the packages).
- All manual stages succeeded and left zero staged entries after package destruction. Actual patchelf execution failure on RPATH inspection has not yet been fault-injected; the source retains the existing parent-only fallback on inspection failure.

## Digest cache and integrated results

Pending subsequent implementation tasks. No final optimized latency is claimed yet.
