# Proposal

## Why

Repeated actor reloads and SIGTERM shutdown can segfault on Linux because actor images are unloaded before the actor I/O execution context destroys Asio services whose vtables reside in those images. Joining pool threads does not destroy those services.

## What Changes

- Retain retired actor images until generation member destruction on every platform, matching the existing Apple policy.
- Specify that actor images outlive the actor I/O execution context and its services.
- Verify existing teardown paths and repeated reload/shutdown behavior without adding unit tests.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `actor-runtime-reload`: Extend safe actor-library lifetimes to execution-context service destruction on every platform.

## Impact

Only `src/core/runtime/runtime_generation.cpp` changes in core. No ABI, configuration, dependency, or actor-source changes are needed. The shared core and executable are rebuilt; existing dynamically linked actors need no relinking. Retired images remain mapped until their generation object is destroyed.
