# Core implementation and historical acceptance

## Delivered

- Opt-in `actor_scoped(...)` metadata and installed data-only scope/matcher/publisher SDK.
- Owner-bound validated preparation publication, explicit-empty versus missing scope, and immutable generation finalization.
- Shared exact-route, ACL and scope eligibility for help and dispatch; `command_unavailable` terminates before transactions, command replies, handlers or ordinary fallback.
- Startup, validation-only, failed candidates, draining admissions, cutover, observers and complete platform catalogs retain their generic contracts.

## Historical verification

These are the original archive's results, not tests rerun for the 2026-09-30 documentation relocation. Top-level build and CTest requested 20 workers.

| Check | Original result |
| --- | --- |
| Complete workspace build | Passed |
| Non-installed-SDK CTest | 890/890 passed |
| `actor_sdk_v2_smoke` | 1/1 passed |
| Bot SDK isolation suites | 3/3 passed |
| Earlier runtime loading new scoped SDK fixture | Rejected unsupported `availability` member |
| Strict OpenSpec and repository whitespace checks | Passed |

The complete original acceptance record, including the earlier-runtime SHA-256, commands, logs, 59/59 independent actor tests, 1/1 installed-real-actor regression, and operational boundaries, is preserved unchanged as `implementation.md` in both relocated actor archives. See [migration.md](migration.md) for links. Actor-owned target-group behavior is maintained there rather than specified by core.

No production configuration/defaults, credentials, live databases, deployments or commits were changed by the original implementation. This relocation likewise changes documentation only.
