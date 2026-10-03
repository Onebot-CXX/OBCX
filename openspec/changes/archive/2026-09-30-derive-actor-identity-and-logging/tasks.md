## 1. Generated actor identity

- [x] 1.1 Add configure-time identity/binding generation and private actor-owned target wiring, preserving incremental behavior and installed SDK support.
- [x] 1.2 Split identity-parameterized reflection implementation from the generated one-argument facade and retain ABI V2 exports.
- [x] 1.3 Migrate maintained actors and test fixtures to manifest-derived identity; isolate host-only multi-identity fixtures.

## 2. Source-owned logging

- [x] 2.1 Route actor macros through generated names and host logs through core; synchronize initialization and named logger creation.
- [x] 2.2 Verify concurrent attribution, shared sinks and level changes with focused observable tests.

## 3. Acceptance and documentation

- [x] 3.1 Extend existing real-build and installed SDK fixtures for generated identity, helper-library logging, version changes and unchanged rebuilds.
- [x] 3.2 Update SDK/actor authoring documentation and record build, reflection, actor integration/reload and SDK test evidence.
- [x] 3.3 Format, validate the change, check whitespace and verify staged/unrelated work remains intact without deployment or commit.
