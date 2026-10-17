---
name: obcx-image-build
description: Build a minimal OBCX Docker image for the actors named by the user, including their transitive actor/library dependencies. Use for actor-specific container builds or rebuilds. Produce a verified local Docker-loadable archive and return its absolute path; do not deploy it.
compatibility: Native Linux x86_64 or arm64, this OBCX workspace, Nix, Python 3.11+, and an available Docker or rootless Podman engine.
---

# Build a minimal OBCX image

The user supplies actor names or package IDs, for example:

```text
/skill:obcx-image-build bridge message_store
/skill:obcx-image-build exhentai_fetch chat_llm
```

Build from the current working files, including requested uncommitted changes.
Do not reset, stage, commit, format unrelated code, or edit the user's
`packages.toml`. Do not upload, modify remote configuration, restart services,
run unit tests, or create checksum sidecars unless separately requested.

Resolve every `scripts/...` path below relative to this **skill directory**.
The repository path comes from the actual checkout, never from a remembered
`/home/...` path. Never hardcode a host, deployment configuration, old image tag,
Nix store hash, or actor selection from an earlier conversation.

## 1. Resolve the requested actors

- Read the repository guidance and `packages.toml`. Inspect the bound
  `package.toml` files to match package IDs, `package.name`, or `actor.name`.
  Resolve informal names such as “store” to the actual package ID before running
  the helper. Ask one short question if the request is missing or ambiguous;
  do not silently select every workspace root.
- Follow production `dependencies.actors` and `dependencies.libraries` through
  the existing package resolver. For example, Bridge needs Message Store even
  when the user only names Bridge. Tell the user which extra actors are required.
- Inspect the selected actors' external commands and runtime data. Bridge needs
  headless ffmpeg and `/bin/sh`; Message Store and Chat LLM do not require those
  utilities merely because another actor uses them. ExHentai's optional image
  censor requires an explicitly selected interpreter, packages and model assets.
  Ask whether such optional helpers are required; never turn off a feature or
  invent configuration defaults to make an image smaller.
- Check the build host architecture and available local container engine. Do not
  silently use a remote Docker context or Podman connection. This workflow is
  native Linux only; stop rather than producing the wrong architecture. Use all
  available compilation workers when load is low, with at least six.

## 2. Snapshot and compile the minimal workspace

Choose a new, ignored build directory and an explicit unique image tag. Existing
output directories and image tags are refused, never overwritten. All helper CLI
options are required; these are build decisions, not application configuration
defaults. Example for Bridge on an x86_64 host:

```bash
REPO=$(git rev-parse --show-toplevel)
SKILL="$REPO/.agents/skills/obcx-image-build"
WORK="$REPO/build/actor-image-$(date +%Y%m%d-%H%M%S)"
JOBS=$(nproc)
[ "$JOBS" -ge 6 ] || JOBS=6

python3 "$SKILL/scripts/prepare_workspace.py" \
  --repo "$REPO" --output "$WORK" --platform linux-x86_64 \
  --actors vollate.bridge

nix develop "$REPO#ci" --command cmake \
  -S "$WORK/source" -B "$WORK/cmake" -G Ninja \
  -DCMAKE_BUILD_TYPE=MinSizeRel -DCMAKE_UNITY_BUILD=OFF \
  -DCMAKE_INSTALL_PREFIX=/opt/obcx -DCMAKE_INSTALL_LIBDIR=lib \
  -DENABLE_DEBUG_TRACE=OFF -DOBCX_ENABLE_IO_URING=ON \
  -DOBCX_BUILD_BENCHMARKS=OFF \
  -DOBCX_PACKAGES_WORKSPACE="$WORK/source/packages.toml" \
  -DOBCX_PACKAGES_CACHE="$WORK/package-state/sources" \
  -DOBCX_PACKAGES_STATE_DIR="$WORK/package-state/cmake" \
  -DOBCX_PACKAGES_MODE=development

nix develop "$REPO#ci" --command cmake --build "$WORK/cmake" --parallel "$JOBS"
nix develop "$REPO#ci" --command cmake --install "$WORK/cmake" \
  --prefix "$WORK/install" --strip
```

Substitute the resolved package IDs and native platform, not a preselected actor
set. `prepare_workspace.py` reuses the repository resolver, checks production
requirements, copies source snapshots, and generates `build-plan.json` containing
**all** actor artifact names needed by the packager. Cached fixed Git sources are
supported; it does not fetch missing sources automatically. Get approval before
fetching, and never weaken provider/version/path validation to bypass a failure.

`MinSizeRel` selects size optimization; `Release` currently forces unity builds,
which can collide on anonymous-namespace helpers. Do not patch product code just
to enable unity builds. Keep stdout/stderr in per-stage logs during long builds.

## 3. Declare only necessary non-ELF runtime assets

Create `$WORK/runtime.json` with **all three lists explicitly present**:

```json
{
  "executables": [],
  "files": [],
  "directories": []
}
```

This is a manifest shape, not a usable empty runtime: every image needs a CA
bundle entry in `files`, installed at `/etc/ssl/certs/ca-bundle.crt`.
Each entry must have absolute `source` and image `destination` strings.

Resolve runtime tools from the repository's pinned Nixpkgs, for example:

```bash
nix build --impure --no-link --print-out-paths --expr \
  'let p = import (builtins.getFlake "'"$REPO"'").inputs.nixpkgs { system = builtins.currentSystem; }; in p.lib.getBin p.ffmpeg-headless'
```

Request ffmpeg **only when selected actors need it**. Resolve `cacert`,
`openssl`, and, for shell-based helpers, `bash` from the same pinned Nixpkgs.
Use their actual realized paths; do not copy this example's paths as placeholders.

- `executables`: native ELF tools and their stable aliases, such as the headless
  ffmpeg binary at `/usr/local/bin/ffmpeg`, and Bash at `/bin/sh` when needed.
  Their ELF dependency files are collected automatically.
- `files`: CA bundle, necessary OpenSSL configuration, explicitly requested
  helper scripts/models, and license notices. Scripts must have their interpreter
  declared separately. Keep compiled-in asset paths when tools rely on them.
- `directories`: explicitly audited Nix runtime subdirectories, such as OpenSSL
  provider modules or required Python libraries. Source and destination must be
  the same absolute Nix path. Every contained DSO gets its dependencies included.
  Do not put an entire Nix closure, source checkout, model cache, or build tree
  here. Never include credentials, production configs, databases, or test fixtures.

ELF dependencies do not describe `dlopen` plugins, Python imports, model files,
fonts or other data. Inspect the selected helpers and include those assets
explicitly. If their required runtime closure is unknown, stop and clarify;
missing functionality is not a size optimization. Preserve licenses for packaged
third-party components; installed actor licenses are retained automatically.

## 4. Package and verify the scratch image

Read `build-plan.json` for the complete actor artifact list, and the build's
`CMakeCache.txt` for the exact `OBCX_PATCHELF_EXECUTABLE`. That executable is also
needed by the application's actor staging code and must remain at its compiled-in
path. Run the packager inside the same CI development shell:

```bash
nix develop "$REPO#ci" --command python3 "$SKILL/scripts/pack_runtime.py" \
  --prefix "$WORK/install" \
  --actors bridge message_store \
  --runtime-spec "$WORK/runtime.json" \
  --patchelf "$PATCHELF_FROM_CMAKE_CACHE" \
  --output "$WORK/image" \
  --tag "$EXPLICIT_UNIQUE_IMAGE_TAG" \
  --engine podman
```

Replace the actor list, patchelf path, tag and engine with the observed values.
Do not build an x86_64 artifact for an arm64 request or substitute a different
patchelf silently. The packager:

- copies only OBCX, the exact installed actor closure, reachable shared libraries,
  explicitly declared tools/assets, and required licenses into a scratch rootfs;
- retains Nix loader/library paths **at file granularity**, not full Nix outputs;
  excludes compiler/SDK headers, static libraries, debug artifacts and other actors;
- rewrites installed application RPATHs, checks for missing ELF dependencies,
  builds without network access, then checks `--version` and actor dependencies
  in network-disabled containers;
- exports a gzip-compressed Docker archive and records measured sizes, image ID,
  actor inventory and runtime file inventory in `image-report.json`.

The image intentionally contains **no deployment configuration or default
startup command**. Its entrypoint is `/opt/obcx/bin/obcx`; callers must explicitly
mount their configuration and supply `--config <container-path> --no-tui`.
No change to an existing Compose service is part of this skill.

Check required external helpers with network-disabled, local invocations. For
Bridge, also verify ffmpeg's software JPEG/PNG/GIF/WebM conversion path. Use a
throwaway local directory, never production media/state. If the user supplied a
configuration, validate only an isolated copy with temporary database/media paths;
do not embed it in the image or connect its bots. No unit tests are requested by
this skill. Dependency checks alone do not prove live forwarding or optional
helper behavior.

Minimize contents without breaking features or removing licenses. Report actual
size, not an unverified claim of a globally smallest possible image. If validation
fails, retain logs and report the failure; do not call an unverified archive ready.

## 5. Return the artifact

After all required checks pass, the final response starts with the **absolute
path of the actual `.tar.gz` file**, followed by image tag, compressed size and
included actors. Verify the file exists and is nonempty immediately before replying.
If the user may be on a different host/filesystem, state the build hostname too.
Do not output only a Podman tag or a temporary Nix symlink.

Do not upload or deploy unless the user separately asks. When authorized later,
respect their transfer preference (for example, rsync and no checksum sidecar).
Use Docker Compose `up --no-deps --force-recreate` for an existing Compose-managed
OBCX service; never replace it with a manually managed `docker run` service or
restart unrelated services.
