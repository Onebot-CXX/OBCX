## 1. Remove the lock model

- [x] 1.1 Flatten the resolved graph and remove lock comparison, schema and receipt fields.
- [x] 1.2 Remove lock CLI interfaces and prepared-graph exporter inputs while preserving atomic output, source checks and explicit options.

## 2. Configure directly

- [x] 2.1 Resolve offline inside CMake with four explicit package inputs and correct input tracking/output containment.
- [x] 2.2 Update presets, CI, installed SDK packaging/preparation and remove obsolete local lock artifacts.

## 3. Regression coverage and documentation

- [x] 3.1 Replace frozen-lock tests with current-declaration, removed-interface, concurrent export and safety regressions.
- [x] 3.2 Cover clean single-step configure, metadata reconfiguration, forged/stale graph replacement, failed validation and unchanged incremental builds.
- [x] 3.3 Update core/nested build documentation and annotate the superseded active package design without rewriting historical evidence.

## 4. Acceptance

- [x] 4.1 Run all package/schema/export/isolated CMake tests with at least six workers.
- [x] 4.2 Configure the real workspace without preparation, build with 20 workers and verify a second unchanged build performs no compilation/linking.
- [x] 4.3 Run workspace tests and installed SDK smoke/isolation tests with 20 workers, preserving prior actor regressions.
- [x] 4.4 Validate OpenSpec, formatting/diffs, absence of active package-lock interfaces and unchanged Nix inputs; record evidence without deploying or committing.

## 5. User-requested test audit follow-up

- [x] 5.1 Inventory registered tests and inspect retirement-only assertions, duplicated build fixtures and source-text checks across core and maintained actor tests.
- [x] 5.2 Remove obsolete-feature absence tests and proven duplicates, retain current safety/behavior coverage, and document the testing rule.
- [x] 5.3 Run focused affected tests and record the scope, remaining coverage and actual reduction in test work without claiming every remaining test is necessary.
