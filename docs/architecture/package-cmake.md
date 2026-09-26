# V2 graph-driven CMake (development component)

`cmake/OBCXPackages.cmake` now drives the root build and the maintained actor
CMake entry points. Seven package manifests use v2; the four currently selected
actors have passed the real build gate and compilation. Legacy readers/helpers
are removed and registry checks use the canonical reader. Full release/container
workflow migration remains unfinished; there is no second supported build format.

## Entry point

After defining the workspace SDK targets, call the loader with every argument:

```cmake
include(OBCXPackages)
obcx_load_packages(
  WORKSPACE "${workspace_manifest}"
  LOCK "${absolute_lock_path}"
  GRAPH "${absolute_prepared_graph_path}"
  CACHE "${absolute_source_cache_path}"
  MODE development
  STATE_DIR "${CMAKE_BINARY_DIR}/package-state")
```

The root and standalone bootstrap use `obcx_load_configured_workspace()`, which
requires all six `OBCX_PACKAGES_WORKSPACE`, `OBCX_PACKAGES_LOCK`,
`OBCX_PACKAGES_GRAPH`, `OBCX_PACKAGES_CACHE`, `OBCX_PACKAGES_MODE`, and
`OBCX_PACKAGES_STATE_DIR` variables. Presets specify development mode and their
own graph/cache/state paths; prepare that graph before configuring.

The CMake configuration must also be explicit (`CMAKE_BUILD_TYPE` or a
multi-configuration generator). The loader checks the frozen graph offline,
including source mappings and actual target platform/compiler, then configures
only selected packages in topological order. It neither fetches sources nor
updates a lock. An explicit empty roots list configures no packages.

State must be outside package source roots. Workspace/lock/graph, package metadata
and provider environment inputs participate in CMake reconfiguration tracking.
Implementation edits do not force a metadata relock. Current graph content
receipts are not yet final binary/build receipts.

## Package targets

The metadata chooses target name, export alias, artifact type and output name;
there is no second `DEPS` list or inferred SDK dependency:

```cmake
obcx_add_library(SOURCES mapping.cpp) # header-only: obcx_add_library()
# Actor packages instead use: obcx_add_actor(SOURCES actor.cpp)

add_library(package_impl OBJECT implementation.cpp)
obcx_package_target(package_impl ROLE implementation)
target_link_libraries(the_metadata_target PRIVATE package_impl)

if(OBCX_PACKAGE_PROFILE STREQUAL "tests")
  add_executable(package_check check.cpp)
  obcx_package_target(package_check ROLE test)
  target_link_libraries(package_check PRIVATE the_metadata_target)
endif()
```

`OBCX_PACKAGE_PROFILE` is the explicit locked profile, not a package-level default.
Helpers apply declared library/system edges and their visibility to owned targets.
Tests additionally receive test dependencies. Internal targets remain part of the
same package and must be registered; another package's internal target is not an
export. Production targets cannot link test targets or test-only dependencies.

Targets must be created within the package's source and binary scopes. Main
artifact/type/export/output identity and compiled-target PIC/C++ standard are
checked after package CMake finishes. The selected libstdc++ ABI is compiled as a
probe; actor construction additionally checks the reflection compiler contract.
The fixture actor checks construction, not runtime factory/invocation semantics;
existing SDK/runtime smoke tests remain separate.

## Audit stages

1. Provider closures are captured before package code and sealed against later
   target-property changes. Their actual locations/includes must remain within
   declared environment prefixes; receipts and input hashes are rechecked.
2. A root-directory deferred audit rejects missing/misowned targets, export/type
   drift and directly visible unauthorized edges, including hidden internal-target
   paths. Package-created imported targets are rejected: use provider bindings.
3. CMake generates per-configuration link/source/usage observations. CMake itself
   evaluates expressions. `LINK_ONLY` is evaluated separately for link and usage
   contexts, so private static dependencies do not grant public include access.
   Unsupported contextual expressions fail generation; unresolved values fail the
   audit rather than disappearing.
4. An obligatory `obcx_package_audit` build target checks the evaluated graph and
   cross-checks CMake File API compiler, target types, dependencies, source/object
   files, includes, standards, ABI flags, PIC and link fragments. Every compiled
   package target depends on this gate, including when built individually.

Only declared exports may be linked directly across packages; legal public usage
can propagate transitively. Raw package archives, link directories and dependency
flags are rejected. Actual compiler implicit libraries/directories come from File
API, not a handwritten list. Generated runtime search directories must belong to
the registered artifact/toolchain closure. Native, unannotated CMake RPATH padding
for installable development targets is recognized separately; this is not an
installed-ELF/RPATH acceptance check. Explicit empty search paths remain rejected.
Manual build-order dependencies are checked separately and do not grant link or
include usage. SYSTEM include markers alone do not add search directories.
Source-specific flags are checked in
File API, not merely in target properties. Nested package source directories do
not become owned by an enclosing package just because their paths share a prefix.

Generation-dependent failures are reported at the mandatory pre-compilation gate;
a successful `cmake -S ... -B ...` alone is not evidence of a passed build audit.

## Boundaries and remaining work

This is a consistency check over supported CMake targets, not a sandbox for
scripts/custom commands or proof of C++ ABI compatibility or every header read.
Provider scripts remain trusted inputs. Formal isolated CI/network enforcement,
final build receipts, clean installed-interface leakage checks and expanded
installed-SDK consumption remain separate tasks. Complete dependency closure
packaging is not inferred from these target records.

The component is exercised with a compact real-build fixture covering all three
library types, an internal OBJECT target, a fixture workspace SDK/actor, both
profiles, and focused failure cases. The real Nix-backed root now builds Message
Store, Bridge, ExHentai and the QQ probe through that gate. Global core include
paths no longer leak into SDK-independent packages. Existing installed-SDK smoke
now prepares its own explicit v2 graph, builds, installs and invokes the actor
using installed tooling; this preserves that regression, not task 5.8's expanded
actor-plus-library standalone acceptance.
