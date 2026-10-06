# Declarative package providers (development integration)

The v2 provider component is implemented in `cmake/OBCXPackageProviders.cmake` and
`cmake/obcx_package/providers.py`. The root now consumes a verified v2 graph with
real Nix-backed providers. Registry checks now use the same canonical reader. Publication, packaging
workflow migration and expanded standalone SDK release remain unfinished.

## One binding, explicit discovery

Each system dependency selects a binding in `packages.toml`. The binding supplies
its logical ID, find mechanism, package name, components, authorized targets,
exact version, version scheme, version probe, package-manager mapping and
provenance. All fields are mandatory; empty component/feature lists are explicit.

Supported discovery mechanisms:

- `cmake-config`: `find_package(... CONFIG REQUIRED GLOBAL)`.
- `cmake-module`: `find_package(... MODULE REQUIRED GLOBAL)`.
- `pkg-config`: one explicitly named `PkgConfig::<prefix>` target; no components.
- `workspace-sdk`: existing non-imported targets from core, not another source
  package or a private actor DSO.
- `installed-sdk`: remains a distinct binding kind; expanded external-consumer
  acceptance and distribution are deferred.

`version_probe` selects exactly one mechanism:

- `{ kind = "cmake-variable", name = "LIBXML2_VERSION_STRING" }`;
- `{ kind = "target-property", target = "Example::lib", property = "VERSION" }`;
- `{ kind = "pkg-config" }`;
- `{ kind = "receipt", path = "provider-version.json", sha256 = "..." }`.

These are structural examples, not configuration defaults. A missing version is
an error; the declared version is never substituted for the observed version.
The actual version must match the explicitly declared version under the selected SemVer or
numeric scheme. The resolver separately checks every consumer's range.

## Environment provenance

`provenance.path` names a `provider-environment` JSON record. Its required fields are:

- `schema_version = 2`;
- `kind`, matching the binding's provenance kind;
- `prefixes`: explicit `{base, path}` records, where base is `workspace`, `build`
  or `absolute`.

Provider bindings and environment records do not pin environment-file hashes.
Changes to `flake.nix`, `flake.lock`, or CMake files require no provider hash refresh.
When migrating an existing workspace, remove `provenance.sha256` from provider
bindings and `inputs` from their environment records. Source archive hashes and
binary version-receipt hashes are separate integrity checks and remain required.

Provider paths must come from the explicitly recorded store outputs. A Nix prefix
cannot authorize all of `/nix/store`. Discovered include directories and binary
locations must exist and be contained in the declared prefixes. A matching version
under an unrelated installation prefix is not accepted. Build-relative prefixes
allow the in-tree SDK's generated headers without committing machine-specific paths.

The local workspace stores environment records under `.package-state/providers/`
and references them from `packages.toml`. CMake tracks these records for
reconfiguration and checks actual provider versions, targets, and paths on every
configure. The SDK version comes from `obcx::obcx_core.OBCX_SDK_VERSION`; its include
roots are explicitly `workspace:include` and `build:generated`, not the whole checkout.

The core/SDK baseline owns SDK internal dependencies; actor metadata owns additional
direct dependencies. `flake.nix`/`flake.lock` own the third-party environment; no
second generated dependency list is required. These checks do not install packages
or sandbox malicious CMake. Final build receipts remain a separate integration step.

## Unversioned providers

An explicit version receipt must match the binding's ID and version scheme and
cover the exact authorized target set. Receipt files have fixed hashes and must
belong to the observed target's binary locations or include directories. Every
observed binary location must be covered. A marker file unrelated to the target
cannot establish its version.

Canonical JSON schemas live under `schemas/provider-{environment,receipt}.schema.json`;
validation and CMake consume the same Python contract implementation.

## vcpkg preparation without configure

`cmake/gen_vcpkg_manifest.py` resolves the **current explicit v2 workspace offline**.
It no longer reads legacy actor metadata or requires CMake's fetched-actor tree.
The root loader and maintained CMake entries now use v2. This host's bindings are
explicitly Nix-backed; vcpkg consumption still requires a separately authored
workspace with the mappings below.

```sh
python3 cmake/gen_vcpkg_manifest.py \
  --workspace packages.toml \
  --cache build/package-state/sources --mode development \
  --base vcpkg-base.json --baseline <explicit-40-character-commit> \
  --output vcpkg.json
```

The base manifest supplies core/SDK requirements. Every selected non-SDK provider
must explicitly map to a vcpkg port, feature list, default-feature policy and the
same pinned baseline. Nix/environment bindings with `package_manager.kind = "none"`
are **not guessed into vcpkg names**. Prepare a vcpkg-bound workspace for that
export. Feature unions retain core constraints; incompatible default-feature or
version policies fail rather than being silently overwritten.

The exporter derives source mappings and checks dependency constraints directly
from current declarations, stays offline and atomically writes only its output.
It never executes package CMake or chooses a package version; no prepared graph
is consumed. The explicitly pinned vcpkg baseline remains required.
