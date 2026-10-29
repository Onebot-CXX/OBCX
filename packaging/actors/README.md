# Actor source inputs

Build maintained repositories through the explicit `packages.toml` workspace.
Pre-package-v2 offline bundles, adaptation patches and replay scripts are no
longer distributed. Use repository Git history and OpenSpec for historical
reference; do not restore old snapshots over maintained local repositories.

## Platform image interfaces

See [OneBot image access](onebot-image-access.md) for typed `get_image`, the
LLOneBot URL-only extension, and shared bounded HTTP acquisition. Native image
references, transient URLs, and OneBot server paths are distinct types of data.

## Shared self identity

See [cached self identity](self-identity.md) for the common QQ/Telegram
`GetSelfRequest` API, its refresh configuration, and installation lifecycle.

## Hot-reloadable actor builds

A root workspace build is **not actor-only**, even with
`cmake --build ... --target exhentai_fetch_actor`. CMake also updates stale
transitive dependencies, including `libobcx_core.so`. Replacing that file changes
its content identity and makes the running process reject actor reloads with
`reload_dependency_identity_conflict`. The `actor-dev` and `actor-release`
presets remain full workspace builds.

Use `scripts/actor_build.py` for an isolated build against a **fixed installed
SDK**. The separate `cmake/actors/` entry point imports the core library; it
never adds core implementation sources or a buildable core target. Actor and
library working trees remain editable and incremental builds use their current
contents. The original `packages.toml` and provider evidence are not modified.

### 1. Freeze the runtime's SDK once

Run in the same Nix development environment as the core build. Select an
already-built core that matches the runtime you intend to use. The example
explicitly selects `build/actor-dev`; use the actual configured core directory
if it differs.

```bash
REPO=$(git rev-parse --show-toplevel)
CORE_BUILD="$REPO/build/actor-dev"
SDK="$REPO/build/sdk-$(date +%Y%m%d-%H%M%S)"

python3 "$REPO/scripts/actor_build.py" snapshot-sdk \
  --core-build "$CORE_BUILD" --output "$SDK"
```

This operation **does not compile anything**. It refuses pending core build work,
installs only the existing SDK into a new directory, verifies that its core
binary is byte-identical to the original, records file hashes, and makes the
snapshot read-only. Existing SDK directories are never overwritten.

The snapshot also records the compiler, CMake, Ninja, discovered Nix dependency
locations, compilation flags and an allowlist of build-environment inputs. Actor
builds reuse that context rather than silently switching to a new shell's
compiler or dependencies. Keep the referenced Nix store outputs available;
missing pinned inputs must be restored or deliberately replaced by a new
runtime/SDK generation. This helper currently supports native Linux Nix builds
with the single-config Ninja generator.

A snapshot of today's on-disk core cannot recover an older core already loaded
in memory. If the disk binary was replaced, restart the process with the matching
runtime before trying to hot reload. Do not disable dependency identity checks.

### 2. Configure a separate actor workspace

Choose a new build directory and explicit actor **package IDs**. This example
selects development sources, Debug compilation, and file/line logging disabled;
these are explicit choices, not configuration defaults.

```bash
ACTOR_BUILD="$REPO/build/exhentai-actors-only"
python3 "$REPO/scripts/actor_build.py" configure \
  --sdk "$SDK" \
  --workspace "$REPO/packages.toml" \
  --cache "$REPO/build/actor-dev/package-state/sources" \
  --output "$ACTOR_BUILD" \
  --actors vollate.exhentai-fetch \
  --mode development --configuration Debug --debug-trace OFF
```

The existing resolver selects the transitive actor/library closure offline.
For example, selecting `vollate.bridge` also includes Message Store. Missing
cached Git sources are errors, not an implicit download. Use a production-profile
workspace. External provider evidence may use absolute or workspace-relative
prefixes; build-relative external prefixes must be made explicit first.

The generated workspace binds `obcx-sdk` as `installed-sdk`, retaining provider
version/path verification. Use a new actor build directory when changing the
SDK, actor selection, provider declarations or configuration. Changes to existing
actor source files only require the next step.

### 3. Build and rebuild actors

```bash
JOBS=$(nproc)
[ "$JOBS" -ge 6 ] || JOBS=6
python3 "$REPO/scripts/actor_build.py" build \
  --output "$ACTOR_BUILD" --jobs "$JOBS"
```

The helper checks SDK integrity before and after compilation and verifies that
the core is imported from the selected snapshot. It builds all selected and
transitively required actors, prints their absolute paths under
`$ACTOR_BUILD/cmake/actors/`, and does not deploy or restart anything. Use the
helper rather than invoking the generated build directly so these checks and
the frozen environment are applied every time.

Deploy only the required actor artifacts and any package-private shared-library
closure through your normal deployment process. **Do not copy the SDK's core
library over the running runtime.** Full core/toolchain upgrades are separate
build, deployment and process-restart operations; freeze a new SDK afterward.
