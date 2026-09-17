# Modular Bot SDK migration

This is a coordinated C++ SDK break, not a new messaging feature or a new Bot
configuration format. Telegram/OneBot contracts and connection schemas now belong
to their modules. Keep existing canonical TOML, the 13 production action strings,
exact installation/conversation routing and Bridge schema-3 state.

## Do not confuse the version gates

| Boundary | Required value |
| --- | --- |
| Actor numeric dispatch ABI | `2` (unchanged) |
| Generated `obcx_get_actor_contract().schema_version` | `2` |
| `actor.toml` `[compatibility].input_contract_schema` | `2` |
| `actor.toml` document `schema_version` | `1` (unchanged) |
| Current Bridge database schema | `3` (unchanged) |

A schema-1 Actor is rejected before factory/preparation during startup,
validation-only and reload, even when it reports ABI 2. Merely editing package
metadata does not rebuild the DSO. A compatible schema-2 Actor can still omit the
optional generation-preparation symbol. All Actors must be rebuilt, including
Message Store and template Actors that never send a Bot operation.

## Actor author checklist

1. Replace the removed all-platform client/umbrella with
   `core/bot/operation_gateway.hpp` and only the common/platform headers used.
2. Obtain `BotOperationGateway` from `ActorContext`; use `obcx::bot::invoke` or
   `MessagingClient` / the platform's `Client`. Keep request/result pairing and
   tracked `await_asio` ownership; never add detached gateway work.
3. Link `obcx::bot_common_sdk`, `obcx::bot_onebot11_sdk`, or
   `obcx::bot_telegram_sdk` as appropriate, alongside the existing Actor framework
   SDK. Platform targets do not require the peer platform's headers.
4. Declare exact expected surface strings (`onebot11.qq`, `telegram.bot_api`) in
   scalar and collection bot-reference contracts. Existing ingress/command route
   strings remain `qq` and `telegram`; they are not surface aliases.
5. Include `common/config_snapshot.hpp` for configuration views. Full
   `ConfigLoader` and `BotPlatformCatalog` APIs are process-private.
6. Independent tests use
   `ActorConfigSnapshotBuilder::build(actor_document, metadata, config_path)` and
   a fake gateway. Pass Actor-owned values and explicit non-secret Bot metadata;
   a `bots` table is rejected. This builder does not validate real connections.
7. Preserve failure semantics: invalid side-effect results are not success and
   must not enable automatic resend. Internal media bytes use binary gateway
   codecs; public DTO JSON is still the documented numeric-array format.

See [operation examples](qq-telegram-bot-operations.md) and
[module configuration/lifecycle](bot-component-runtime.md). The SDK has no
platform catalog registration, connection-manager, provider-method or arbitrary
provider-URL passthrough API for Actors. Actor-owned HTTP clients and credentials,
such as Chat LLM's model service, are separate from process-owned Bot connections.

## Build and release acceptance

- Build core and every Actor with the matching C++26/reflection toolchain.
- Install into a new empty prefix; run common-only, OneBot-only and Telegram-only
  SDK fixtures with the peer headers physically absent.
- Run the generic `test.echo` fixture without production platform linkage,
  independent Actor suites, lifecycle/reload/cancellation gates, and full CTest.
- Update and verify SDK/Actor compatibility metadata and the offline
  bundle/patch/revision chain. Local uncommitted Actor builds alone are not
  reproducible release acceptance.
- Run the new binary's `--validate-config <path>` against the coordinated Actor
  artifacts before starting provider activity. Validation checks plans and
  contracts without starting Bot workers or publishing command menus.

## Deployment and rollback

Stop the process, replace core and **all** Actor artifacts together, and restart.
Do not use Actor-only reload to mix the old and new SDK. Within a compatible
release, Actor-only reload reuses the same catalog, gateway and installations;
connection, secret, disabled-bot or selected recipe/manifest changes require a
process restart.

Rollback restores the previous matching core and complete Actor artifact set.
This change does not require a TOML rewrite or a Bridge database downgrade:
existing schema-3 rows, persisted retry identities and mappings are unchanged.
Historical pre-schema-3 migrations have their own backup/rollback requirements;
they must not be mistaken for a migration introduced by this SDK refactor.
