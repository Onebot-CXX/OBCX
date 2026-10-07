# Container packaging is not provided

Use the root [workspace build instructions](../../README.md) and
[package CMake guide](../../docs/architecture/package-cmake.md).
A verified package inventory/closure-based container release workflow remains
unimplemented. There is no historical Containerfile or alternate build wrapper.

Removing old build inputs does not modify `dev/onebot/compose.yaml`, deployed
images, or the running LLOneBot service.
