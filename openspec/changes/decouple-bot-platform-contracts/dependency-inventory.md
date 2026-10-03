# Implementation baseline / dependency inventory

## Revisions and boundaries

Observed at implementation start (the root's actual revision is
`fb80ac6`, not the earlier conversation's `55a811e`):

| Repository | Revision | Migration responsibility |
| --- | --- | --- |
| core | `fb80ac6` | SDK, runtime, configuration, actor loader, exports and tests |
| `local_actor/obcx-message-bridge` | `8f0a9c1` | Both platform adapters, fake gateway, retry, metadata |
| `local_actor/chat_llm` | `3412e48` | Common + Telegram adapters, fake gateway, metadata |
| `local_actor/obcx-message-store` | `1be9ce6` | Rebuild generated input contract even without Bot egress |
| `local_actor/obcx-actor-template` | `74d2085` | Rebuild helper/SDK smoke, metadata |
| `local_actor/obcx-actor-registry` | `69457df` | Entries, generated index and validation tests |

`legacy-dependencies.txt` is a sorted 90-file textual inventory of enum,
all-platform-client, connection-variant or schema-1 references in root and
nested repositories. It includes the new golden test as a consumer to migrate.
It is a starting inventory, not a claim that all text matches denote the same
schema or that transitive include dependencies are captured by grep.

Search expression:

```text
BotSurface|BotAction|action_ids::all|action_supports_surface|BotOperationClient|
BotInstallationSurface|BotConnectionConfig|schema_version.{0,8}1
```

Excluded generated/build trees, bundles and historical research are not runtime
consumers. Header/include closure, installed SDK and isolated runtime builds
remain the acceptance tests. Public `common/message_type.hpp` currently depends
only on common JSON and standard headers: preserving its existing MessageSegment
shape does not require including a provider SDK.

## Root ownership hot spots

- `include/core/bot/bot_operation_types.hpp`: enums, ordinal lookup, global
  compatibility matrix, shared references/errors and Telegram topic target.
- `bot_operations.hpp`, `onebot11_bot_operations.hpp`,
  `telegram_bot_operations.hpp`, `bot_operation_contract.hpp`,
  `bot_operation_client.hpp`: all-platform DTO/include/virtual dependency.
- `src/core/bot/bot_operation_{dispatcher,components}.cpp`: routes, response
  parsers, per-action overrides, supported sets; split without altering safety.
- `include/common/config_loader.hpp`, `src/common/config_loader.cpp`: private
  credential configs, central `BotConnectionConfig` variant, full raw snapshot.
- `bot_installation_assembler`, `bot_component_runtime`, event/protocol/transport
  components and installation directory: recipes and concrete platform types.
- `src/core/command/`, `src/core/runtime/runtime_generation.cpp`, `src/app/main.cpp`:
  Telegram config/catalog plumbing, surface-to-ingress mapping, generation
  references, private fingerprint inputs. Include non-enum platform dependencies
  when splitting these modules.
- `src/CMakeLists.txt`, `cmake/obcx-sdk-config.cmake.in`, root SDK version metadata:
  currently a combined `obcx_core` target and explicit install header list.

## Actor contract schemas

Root tests exercise the current schema-2 contract and reject unsupported future
schema values with root-owned fixtures. Tests dedicated to pre-current actor
contracts and optional-symbol compatibility are not retained.

`actor.toml`, registry index, and release manifest schema numbers are package
formats distinct from `obcx_get_actor_contract()` input schema. Keep those
formats unless their own schema changes are required. Bridge database schema 3
and Message Store state remain unchanged.

## Offline inputs (not local actor HEADs)

Bundle heads verified with `git bundle list-heads`:

| Actor | Archived base | Patched restore output |
| --- | --- | --- |
| bridge | `de8c3046c218c9e2a254abe832e91595f4cc629a` | `7b1fb9e5f5d9d094dd68c7185a78948b79c2b91c` |
| message-store | `3a9dfc2b27375d22531b4308356b75f4bac7077f` | `06f14ed226369079c99474c62088293628ce94e3` |
| registry | `057b46522872bfbc2dd87435e3751b8d2001e26b` | `986c40978111360e31e314cb9e25d5b641fab8a4` |
| template | `993048e6d7e280167cc2189a51464d8fd9197c68` | `30a1373f53b637eb8bf00250c8f9c3ca26578512` |

Update `packaging/actors/bundles/`, `patches/`, `restore-sources.sh`,
`apply-patches.sh`, `README.md` together; the scripts verify exact revisions.
Current bundled actors exclude Chat LLM. Its standalone suite still must run.
Do not equate local actor HEADs with restored sources or blindly substitute pins.
Other consumers: actor registry entries/index, `cmake/parse_actor_packages.py`,
release packaging/verification/rollback scripts, the root-owned generic
`tests/cmake/run_v2_sdk_smoke.cmake`, benchmarks, and deployment docs. External
actor repositories own their architecture, configuration, behavior, and
integration tests.

## Baseline verification

- Main specifications: 24/24 pass strict OpenSpec validation after ordered sync.
- Configuration tests: 7/7 pass, including enabled/disabled mandatory omissions.
- Contract/golden/component tests: 30/30 pass, covering all 13 golden actions.
- No runtime/SDK migration, provider calls, database changes or commits in phase 1.
