# Implementation checkpoint

## Progress

59/62 tasks are checked. Functional migration, SDK isolation, Actor migration,
documentation and current-checkout acceptance are complete. Remaining:
- 10.1: select coordinated release versions and finish compatibility ranges/index.
- 10.2: refresh the reproducible offline bundle/adaptation patch/revision chain.
- 10.6: restore those updated sources into an empty directory and run conformance.

No working-tree commits, online publication, deployment, real provider calls or
production database changes have been performed. **Offline release acceptance is
not complete**: the existing archived actor revisions still target the old SDK.
Do not treat the development checkout's passing tests as a releasable bundle.

## Implemented boundaries

- Public, owning SurfaceId/ActionId and common references/errors/results/messages;
  platform SDKs own their DTOs, validation/codecs, action IDs and typed facades.
- Fixed BotOperationGateway, typed invocation, exact endpoint registries,
  conservative failures, cancellation ownership and binary media gateway codecs.
- Platform modules own connection schemas, typed values, recipes, protocol,
  transport, event/operation and command implementations. Generic runtime has no
  platform imports, central surface/action enums, connection variant or fallback.
- BotPlatformCatalog is explicitly constructed/sealed by
  `src/app/builtin_bot_platforms.hpp` and injected into startup/validation/generation.
  Actor reload reuses its exact catalog, gateway and live installations.
- Immutable installation plans retain typed private values in module factories,
  not raw connection TOML. Factory manifests are checked with set semantics for
  capability dependencies, while component/lifecycle order remains significant.
- Selected uploader dependencies and optional command publication are explicit.
  Generic command routing uses module-bound adapters, exact metadata and a generic
  CommandCatalogPublisher. Actor constraints use exact registered surface IDs.
- Fingerprints include complete normalized connection digests, disabled bots,
  identity, recipe/actions/components/publication and existing process budgets.
- Actor input contracts and package compatibility declarations require schema 2;
  scheduler ABI remains 2, package-document schema remains 1, Bridge DB remains 3.
  Root tests cover the current contract and unknown future schemas without
  retaining pre-current actor compatibility fixtures.
- Bridge, Chat LLM, Message Store, template, root/independent registry metadata and
  fixtures are migrated. Legacy umbrella/client headers are gone; historical
  documentation is explicitly marked.
- CMake exports common/OneBot/Telegram Bot SDK interface targets without linking
  the combined runtime. Generic runtime and both implementation object targets
  are separate, with platform -> generic dependency only.

## User-selected configuration option A

Full ConfigLoader and BotPlatformCatalog APIs are process-private. Actor SDK
configuration is in `common/config_snapshot.hpp`:
`ActorConfigSnapshotBuilder::build(actor_document, metadata, config_path)` accepts
explicit Actor-owned values and non-secret Bot metadata, rejects Bot tables,
and is not full process-configuration validation. No process-config/host SDK was
added. Independent Actor tests explicitly adapt synthetic fixture data to this
builder; core/platform integration tests continue to validate full connection
schemas, including disabled bots.

Public process snapshots reconstruct Bot tables from an allowlist, so neither
nested getters nor root-section views retain Bot connections/credentials. Actor
owned parameters such as Chat LLM's model API key remain available. The existing
caller-owned networking ConnectionConfig moved out of messaging headers into
`network/connection_config.hpp`; its existing networking behavior/defaults were
not changed and it is outside the Bot SDK include closure.

## Verification of current checkout

- Root `nix fmt` and `cmake --build build -j 2` passed.
- Full root CTest covers root-owned runtime, protocol, package-tooling, CLI,
  installed Actor SDK smoke, and three physically isolated Bot SDK prefixes.
  Independent actor repositories own their separate behavior and integration
  suites.
- Generic test.echo module, in a separate translation unit linked only to
  `obcx_generic_runtime`: parse/describe/assemble/invoke/stop, private metadata,
  disabled-secret fingerprints and forged/unknown request rejection passed.
- Actor-only configuration tests compile/run without the combined core library.
- 27 gateway/registry/installation/Actor-cancellation tests each repeated 25 times:
  **675 successful runs**. This is the applicable concurrency gate; no sanitizer
  run is claimed for this checkpoint.
- Metadata and current configuration/schema gates passed. Strict OpenSpec
  validation and `git diff --check` passed.
- A temporary, disabled-Bot copy of the documented canonical configuration passed
  the real binary's `--validate-config`; temporary logs/config were removed. No
  production configuration/database was used for this acceptance command.

The earlier isolated Bridge failures were obsolete expected aliases, and the
registry failure was the root registry's stale schema declaration; both were
corrected and are included in the final 438/438 result.

## Release preparation deferred by the user

The user explicitly selected “暂缓发布准备” after reviewing the suggested
Core/SDK 2.0.0 and Actor 0.2.0 labels. Those new versions were NOT selected or
applied. Current labels remain Core/SDK 1.1.0 and Actor packages 0.1.0; input
contract schema is already 2. Keep tasks 10.1, 10.2 and 10.6 unchecked and do not
resume version/range changes or offline patch/pin generation without further
instructions. Existing archived sources still target the previous SDK. No
working-tree commits or OpenSpec archive were performed.

## Previously approved binary-media decision

The user selected internal binary bytes after a 4 MiB probe showed 64 MiB of
numeric JSON-array storage (`sizeof(Json) == 16`; a 128 MiB payload would require
2 GiB for arrays alone). Public JSON/golden fixtures retain numeric arrays, while
internal Telegram gateway codecs transfer owning Json::binary buffers directly,
validate bounds and reject numeric-array gateway media. Tests cover buffer
addresses and the 4 MiB native multipart path. No blob service, cache, new
configuration defaults or lower media limits were introduced.
