# Actor source inputs

Build maintained repositories through the explicit `packages.toml` workspace.
Pre-package-v2 offline bundles, adaptation patches and replay scripts are no
longer distributed. Use repository Git history and OpenSpec for historical
reference; do not restore old snapshots over maintained local repositories.

## Platform image interfaces

See [OneBot media access](onebot-image-access.md) for ordinary bounded
`MediaRef` reads, separate sensitive standard media-query results, and retirement
of the private native-image/URL actor example. Provider paths and detached query
selectors do not grant common media-read authority.

## Domain identity persistence

See [domain identity encoding and persistence](domain-identities.md) for the
version-1 wire format, full installation/conversation scope, fixed examples,
and the distinction between structural restoration and live authority.

## Shared self identity

See [cached self identity](self-identity.md) for the common QQ/Telegram
`GetSelfRequest` API, its refresh configuration, and installation lifecycle.

## Domain migration SDK floor

SDK 2.7.0 removes old messaging DTOs/facades, the QQ cache-only login projection,
CardView, archive decoders/imports and generic reference conversion. Current
configuration uses explicit kind/native ID/topic with ConversationResolver.
Store schema 5 persists typed messages only; Chat/Bridge consume those snapshots.
Bridge schema 5 removes old scalar member/publication/route projections, while
preserving scoped receipts, retries and command fences. Old database schemas are
rejected without runtime migration. The standalone Python offline utility is
operator-run only; see [the coordinated cutover](domain-sdk-cutover.md).

The maintained Message Store, Bridge, Chat LLM, ExHentai, template and QQ
forward probe sources now require SDK/core **>=2.7.0,<3.0.0** in both package
dependencies and runtime compatibility; standalone CMake uses the same minimum.
This is the conservative floor for the coordinated migration, not a claim that
all earlier 2.x binaries are compatible or that every actor uses every new API.
Scheduler ABI V2, package manifest 2 and input contract schema 3 are unchanged.

The preceding migration build used a frozen SDK2.5.19 at
`build-dev/forward-resource-fix/sdk`; all six real consumers built through
`actor_build.py` against it (20 workers, 90 production actor/library objects).
Those products are historical build evidence, not the current generation. This
snapshot includes the Telegram receipt/empty-photo and QQ forward resource-ID
fixes; older SDK2.5.19 snapshots lack some of these corrections. A version string
alone does not establish binary identity.

The operator requested implementation handoff without new verification programs
and will test after deployment. Loader, full ABI, media and recovery acceptance
remain unexecuted, not passed. Older frozen SDKs remain immutable retained
products; do not alter their manifests or weaken requirements to reuse them.
Core/SDK replacement and restart remain separate operations, not actor hot reload. See the
[coordinated cutover inventory](domain-sdk-cutover.md) for the six package
versions, public API migration, owner-specific data boundaries and matching
backup/rollback gates. The development registry is not a binary release.

## Generation-owned background callbacks

The 2.5.18 source adds out-of-line `ActorGenerationLifecycle` helpers:
`make_serial_executor`, `spawn_owned`, `wait_owned`, and `post_owned`. These
require a matching built/frozen SDK; they are not in the frozen 2.5.17 SDK.

Subscribe during preparation, admit work with the active token, and retain its
route lease through every timer, continuation, completion and cancellation post.
Use the core-created serial executor for a coroutine and its cancellation-slot
emission. The core helpers own the outer Asio handlers and destroy actor closures,
coroutine frames and exceptions before releasing the lease. Merely capturing a
lease inside an actor-compiled lambda does not protect its final destructor or
an inline awaitable completion. Terminal completion is posted after that handler
unwinds. A catastrophic terminal-post failure retains the pin and makes drain
fail rather than allowing unsafe DSO unload. Allocation/initiation failure also
clears actor callbacks before the caller's lease can expire; function-parameter
destruction order is not an ownership guarantee. Standard `ActorEvents` delivery
uses this terminal-retirement boundary too. These paths remain unverified at
runtime; the helper's pin is not proof that arbitrary actor destructors or
externally retained callbacks are safe.

Stop must close new admission. An owner that preserves a flush-on-close contract
may finish batches admitted under existing leases; that is not admission of new
retry attempts. Never force-stop an executor to bypass drain. Publication can be
refused after invalidation; a durable completed send is not replayed to repair a
notification. These are source contracts pending consolidated runtime acceptance.

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
contents. The original `packages.toml` is not modified.

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
workspace. Nix/vcpkg own system dependency locking; no provider receipts or
installation-prefix allowlists are required.

The generated workspace binds `obcx-sdk` as `installed-sdk`, checking discovered
versions against consumer requirements. The helper still verifies that the core
is imported from the explicitly selected frozen SDK. Use a new actor build directory when changing the
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
