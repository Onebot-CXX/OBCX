# Container packaging is deferred

`Containerfile` is a **historical pre-package-v2 recipe**, not a supported build
entry point. It restores old actor bundles, lacks the six explicit package
workspace/lock/graph/cache/mode/state inputs, and does not verify an installed
package inventory or private library closure.

`scripts/build-podman-image.sh` now refuses before invoking Podman. Do not run
the old Containerfile directly or treat an old image as current v2 acceptance.
Rewriting this release workflow is deferred, not a prerequisite for developing
the shared path library.

Use the root [workspace build instructions](../../README.md) and
[package CMake guide](../../docs/architecture/package-cmake.md) for development.
The existing installed-SDK smoke and runtime-stager regressions remain enabled;
they do not prove a new container release works.

This status does not stop, rebuild, or alter any existing container. In
particular, `dev/onebot/compose.yaml` and the running LLOneBot service are not
changed by retiring the build wrapper.
