## Why

Actor identity is duplicated between package manifests and C++ declarations, while every log currently appears under `obcx`. Generate identity and logger attribution from the admitted package metadata so authoring stays simple and concurrent actors remain distinguishable.

## What Changes

- Generate package-specific identity bindings from `actor.name` and `package.version` at configure time, including internal implementation targets.
- Preserve `core::ReflectedActor<Derived>` authoring through a generated, package-namespaced alias to a common identity-parameterized implementation.
- **BREAKING**: maintained actor sources no longer declare identity constants; actor builds must receive explicit generated metadata from the SDK build helpers.
- Attribute logging macros to the actor package; host code logs as `core`. Share existing sinks, levels and formatting with concurrency-safe logger initialization/registration.
- Migrate maintained actors and SDK fixtures, update authoring documentation, and exercise identity, logging, reload and standalone SDK behavior using existing fixtures where possible.

## Capabilities

### New Capabilities

- `actor-package-identity`: Manifest-derived actor authoring identity, ABI exports and source-owned logging.

### Modified Capabilities

None. Existing typed dispatch, command availability and ABI V2 signatures remain unchanged.

## Impact

SDK reflected actor headers and installs, package CMake helpers, logger implementation/macros, maintained actor repositories, fixture build helpers, focused tests and documentation. No live configuration changes, database changes, deployment, restart, commit or automatic archive.
