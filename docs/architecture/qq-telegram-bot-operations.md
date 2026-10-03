# QQ/Telegram Bot Operation Boundary

This is intentionally a narrow egress API, not a universal messaging model.
Only the existing, testable `telegram.bot_api` and `onebot11.qq` adapters are
implemented. OneBot support never implies `qq.official`.

## Closed action matrix

| Action | Telegram | OneBot 11 | Current caller |
| --- | --- | --- | --- |
| `message.send_group` | yes | yes | Bridge, `chat_llm` |
| `message.send_private` | yes | yes | Built-in command help |
| `message.delete` | yes | yes | Bridge |
| `telegram.message.send_topic` | yes | no | Bridge, `chat_llm` |
| `telegram.message.edit_text` | yes | no | Bridge |
| `telegram.media.send_photo` | yes | no | Bridge |
| `telegram.media.send_group_urls` | yes | no | Bridge |
| `telegram.media.send_group_uploads` | optional uploader | no | Bridge |
| `telegram.media.fetch_file` | yes | no | Bridge |
| `onebot11.group_member.get` | no | yes | Bridge |
| `onebot11.forward_message.get` | no | yes | Bridge |
| `onebot11.group_file.resolve` | no | yes | Bridge |
| `onebot11.private_file.resolve` | no | yes | Bridge |
| `onebot11.group.poke` | no | yes | Bridge |

Adding an action or platform requires a separate proposal and executable
adapter tests. Removed universal-bot methods are not capabilities.

## SDK and ownership

Use independently exported `obcx::bot_common_sdk`, `obcx::bot_onebot11_sdk`,
and `obcx::bot_telegram_sdk` targets. The platform targets depend only on common;
none links the combined process library merely to encode/call SDK operations.

- `core/bot/messaging_client.hpp`: common group/private-send and delete.
- `onebot11/bot/client.hpp`: OneBot member/forward/file/poke contracts.
- `telegram/bot/client.hpp`: Telegram topic/edit/entity/media contracts.
- `core/bot/operation_gateway.hpp`: the fixed, platform-neutral service.

Every request retains exact installation/group/private-user/message identity.
`GroupTarget` and `PrivateTarget` are distinct values and cannot substitute for
each other. `SurfaceId` and `ActionId` own explicit strings of 1–128 lowercase ASCII letters, digits,
`.`, `_`, or `-`; they have no default identity, enum ordinal, case conversion,
or platform aliases. Valid syntax does not imply registered support:
`SurfaceId{"test.echo"}` is valid data but unavailable in the production catalog.
Telegram chat and message ids remain separate; topic targets belong to Telegram.

`BotOperationGateway` is the only Actor-visible Bot service. Its envelope carries
exact installation, action and SDK DTO values. The generic dispatcher routes to
a sealed endpoint-local operation registry; owning module handlers decode,
validate and call existing protocol/transports. Payload installation/action and
all target/reply references must agree with the envelope before provider I/O.
Supported actions come from executable definitions, not arbitrary strings or
provider method prefixes. Tokens, process catalogs, handler registrations,
transports and tokenized Telegram file URLs are unavailable through this API.

For example, inside an existing tracked Asio operation, with a retained gateway
and targets selected by validated routing:

```cpp
#include <onebot11/bot/client.hpp>
obcx::onebot11::bot::Client client{gateway};
auto result = co_await client.execute(
    obcx::onebot11::bot::PokeOneBotGroupRequest{
        .target = group_target, .user_id = "42"});
```

```cpp
#include <telegram/bot/client.hpp>
obcx::telegram::bot::Client client{gateway};
auto result = co_await client.execute(
    obcx::telegram::bot::SendTelegramTopicMessageRequest{
        .target = topic_target,
        .message = {{.type = "text", .data = {{"text", "hello"}}}}});
```

Each example needs only its own platform SDK. `obcx::bot::invoke(gateway,
request)` is the equivalent typed helper and pairs Request/Result at compile
time. Actor handlers enter Asio through their existing
`ActorContext::await_asio(executor, callback)` lifecycle, retaining gateway and
request values across suspension. Generation-owned core services use the same
process-owned data-only gateway directly. Do not detach work. A thread id alone
does not make a Telegram event a forum topic.

Existing public DTO JSON stays unchanged; the private-send values use the same
strict deterministic codec rules. Internal Telegram upload/fetch gateway codecs
move bounded byte buffers through `Json::binary`, not per-byte JSON numbers;
that representation is not a dump/parse persistence or network protocol.

The former universal/provider bot interfaces, concrete `QQBot`/`TGBot`, live
bot registry, and RTTI wrappers have been removed. See
[bot-component-runtime.md](bot-component-runtime.md) for process composition,
lifecycle, and configuration.

## Errors and retry safety

Operations return typed success or `BotOperationError`. Errors include
`DefinitelyNotSubmitted` or `PossiblySubmitted`:

- route, validation, unsupported-action, DNS/connect failure, proxy-tunnel
  failure, and TLS-handshake failure before HTTP request writing are definite;
- once HTTP request writing begins, malformed send success, timeout,
  disconnect, or response failure is possibly submitted unless proven
  otherwise;
- malformed SDK success decoding after a side effect is also conservative:
  it cannot fabricate success/mappings or reclassify delivery as safely retryable.

Bridge automatically retries only a retryable `DefinitelyNotSubmitted` result.
A possibly submitted initial send creates no mapping and no retry row. A
possibly submitted retry is terminalized in the existing retry table by its
finite-attempt fields. There is no generic outbox or crash-safe reconciliation;
a process crash at the provider boundary remains deferred work.

## Existing Bridge pairs and schema-3 state

The following business behavior and older migration facilities are retained;
**the modular SDK change adds no database migration**. An already-schema-3
installation keeps its existing rows, pair routing and retry identities.

Bridge supports named, disjoint Telegram/OneBot installation pairs:

```toml
[actors.bridge.config]
bridge_files_dir = "/tmp/bridge_files"
legacy_state_pair = "primary"

[[actors.bridge.config.installation_pairs]]
id = "primary"
telegram_installation = "telegram_bot"
onebot11_installation = "qq_bot"

[[actors.bridge.config.installation_pairs]]
id = "secondary"
telegram_installation = "telegram_bot_2"
onebot11_installation = "qq_bot_2"

[[group_mappings.group_to_group]]
pair = "primary"
telegram_group_id = "-1001"
qq_group_id = "10001"
```

Each installation belongs to exactly one pair. Pair ids and installation ids
must be unique, and every mapping must name its pair when more than one exists.
Messages, notices, and commands carry `source_bot`; Bridge selects only that
bot's pair and never falls back by platform or fans out. The existing scalar
`telegram_installation`/`onebot11_installation` form remains valid for a single
pair and cannot be mixed with the collection form.

Bridge schema version 3 uses complete message identities consisting of exact
installation, platform, conversation, and native message id. QQ conversations
use `group:<id>` and Telegram conversations use `chat:<id>`; topic id remains
route metadata. Consequently equal native ids in two chats on one installation
remain independent for mappings, retries, media groups, replies, edits, and
recalls. User/sticker caches and heartbeats remain installation scoped.

An unversioned non-empty database is first assigned to the scalar pair, sole
named pair, or explicit `legacy_state_pair` as version 2. The startup-only
version-2-to-3 migration then resolves conversations from existing Message
Store identity and current or explicit historical routes. An exact Telegram
topic route uses stored thread evidence, while a group-to-group route remains
chat-wide even when a message carries forum-thread metadata. Strict mode rolls
back on unresolved state; explicit archive mode preserves unresolved
mapping/media rows in non-live tables that no production lookup can read.
Pending retries must migrate exactly. Typed generation preparation completes
this work before Bridge is registered for ingress and publishes the repository
only after commit. Stop OBCX and take a SQLite-consistent backup, including WAL
state, before migration. A candidate reload performs a read-only schema check
and cannot perform either incompatible migration; rollback requires restoring
both the old binary and pre-migration database snapshot.

## Explicitly deferred

This change does not add typed ingress, Telegram checkpoint changes, a Message
Store migration, an ingress journal, a generic outbox or reconciliation,
provider message lookup, rate governance, a blob gateway, history, contacts, moderation, reactions, polls, or adapters for unsupported
platforms. It also does not reconcile content that was already sent with a
wrong reply reference; that message must be explicitly removed and resent.
