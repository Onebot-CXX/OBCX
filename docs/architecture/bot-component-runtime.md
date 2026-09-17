# Bot Installation Component Runtime

Each `BotInstallation` owns one exact id, surface, executor, capability registry,
components, and lifecycle. It is not a universal bot object. Actors receive
`BotOperationGateway`, not installations, platform catalogs, provider connections,
component registries, or process command publishers.

## Module ownership and composition

`src/app/builtin_bot_platforms.hpp` explicitly registers the two production
modules into a `BotPlatformCatalog`, seals it, and injects it into configuration
and generation building. No static self-registration or global config loader is
used. A reload keeps the same process catalog, gateway and installations.

| Surface | Transport | Owning recipe components |
| --- | --- | --- |
| `onebot11.qq` | `websocket` | protocol, WebSocket transport, event ingress, operations |
| `onebot11.qq` | `http` | protocol, HTTP transport, event ingress, operations |
| `telegram.bot_api` | `http` | protocol, HTTP transport, event ingress, media upload, operations, command catalog |

Each module owns its closed connection parser, typed connection values, component
factories, operation definitions, ingress platform and bound command adapter.
The generic loader validates only the common bot table and resolves the exact
surface/transport registration. Its immutable installation plan retains typed
configuration in a process-only factory: assembly never reparses raw TOML.
Duplicate recipe keys, unknown surfaces/transports, `qq.official`, `ws` aliases,
and arbitrary component lists fail before transport construction.

`obcx_generic_runtime` can be linked without either production module. The
application combines generic, OneBot and Telegram implementation objects into
its existing runtime library. A separate `test.echo` translation unit proves
parse/describe/assemble/dispatch/stop without production platform linkage; it is
never registered in the application.

## Lifecycle and executable operations

Components declare stable ids and provided/required capabilities. Assembly
validates the DAG and installs capabilities. It rejects duplicates, missing or
undeclared capabilities, cycles, and factories that differ from their manifests.
Independent components retain recipe order; dependency lists are sets.

Before any component starts, all components prepare in topological order. Then
components start in that order. Failure rolls back prepared components; shutdown
closes admission, cancels/drains admitted work, stops in reverse order and destroys
the executor last. Stop is idempotent. Event subscribers attach before `start()`.
Ingress receives explicit owning installation, surface and module-provided
`qq`/`telegram` platform metadata, not an inferred platform fallback.

Operations components prepare sealed `OperationRegistry` endpoints. Each action
binds typed SDK codecs, scope checks, dependency requirements and a handler.
Static manifests and live registrations use the same platform definitions; the
union remains the reviewed [13 actions](qq-telegram-bot-operations.md). Telegram
upload adds an explicit same-installation uploader dependency. An upload-free
test recipe removes both that dependency and the advertised upload action.

Command detection belongs to the module and captures its validated target name.
The directory exposes an optional generic `CommandCatalogPublisher` selected by
the recipe's capability id. Reconciliation publishes complete aggregates only
after activation/cutover; failure preserves local routing and bounded retries.

## Canonical configuration

These are illustrative explicit values, **not defaults**. Replace credential
placeholders outside version control; the loader does not expand `${...}`.

```toml
[bots.qq_main]
enabled = true
surface = "onebot11.qq"
transport = "websocket"

[bots.qq_main.connection]
host = "127.0.0.1"
port = 3001
access_token = "YOUR_ONEBOT_ACCESS_TOKEN"
connect_timeout_ms = 5000
action_timeout_ms = 30000

[bots.telegram_main]
enabled = true
surface = "telegram.bot_api"
transport = "http"

[bots.telegram_main.connection]
host = "api.telegram.org"
port = 443
access_token = "YOUR_TELEGRAM_BOT_TOKEN"
bot_username = "example_bot"
use_tls = true
connect_timeout_ms = 5000
action_timeout_ms = 30000
poll_timeout_ms = 25000
poll_force_close_ms = 30000
poll_retry_interval_ms = 3000
```

For OneBot HTTP, explicitly supply `host`, `port`, `access_token`, `use_tls`,
`connect_timeout_ms`, `action_timeout_ms`, and `poll_interval_ms`. OneBot's token
may be explicitly empty; Telegram's token may not. Telegram requires TLS.

The Telegram proxy group can be absent as a whole. When used, explicitly supply
all five fields in its connection table: `proxy_host`, `proxy_port`, `proxy_type`,
`proxy_username`, and `proxy_password`; anonymous credentials use explicit empty
strings. No timeout, transport, TLS or proxy credential is filled in implicitly.
Disabled bots undergo the same schema validation. Unknown, misplaced, ignored,
legacy and missing keys produce path-specific diagnostics without credential
values.

## Actor configuration boundary: option A

The installed `common/config_snapshot.hpp` exposes Actor views and
`ActorConfigSnapshotBuilder::build(actor_document, bot_metadata, config_path)`.
This is an explicit data-only construction API, **not** a full process loader.
It rejects a `bots` table in its document; callers supply non-secret installation
metadata separately. Independent Actor tests construct their contexts here.
Full connection-schema tests stay in core/platform integration suites.

Process snapshots reconstruct `bots` from allowlisted metadata, rather than
redacting selected fields from the original connection table. Neither nested
getters nor root-section views expose private Bot values. Actor-owned parameters,
such as Chat LLM's model API key, remain available to that Actor. No process-config
or host SDK is published.

Fingerprints include every installation (also disabled), complete normalized
connection digests, recipe/component/action descriptions, identity, enabled
state, and existing process budgets. A secret-only change requires restart even
when public metadata is unchanged. Actor-only views cannot be published as
validated process snapshots.

## Validation and deployment

`obcx --validate-config <path>` parses plans and validates manifests/contracts
without constructing Bot transports, starting workers, publishing command menus
or calling providers. See [modular-bot-sdk-migration.md](modular-bot-sdk-migration.md)
for the schema-2 rebuild gate, whole-artifact restart/rollback and test procedure.
This SDK change keeps canonical TOML and Bridge schema 3 unchanged; historical
pre-component configuration/database migrations are not rerun for it.
