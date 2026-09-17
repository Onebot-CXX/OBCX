# Message Store And Bridge Database Guide

Status: current actor pipeline and persistence guide (2026-07-13)

The participating packages are:

- `local_actor/obcx-message-store`
- `local_actor/obcx-message-bridge`
- `local_actor/obcx-exhentai-fetch`

Both build against the installed SDK and declare their contract in
`actor.toml`.

## Runtime configuration

```toml
[db.instances.main]
type = "sqlite"
path = "data/obcx.sqlite3"

[actors.message_store]
library = "message_store"
enabled = true
partition = "source_platform:conversation_id"
db = "main"
db_namespace = "message_store"

[actors.bridge]
library = "bridge"
enabled = true
requires = ["message_store"]
partition = "source_platform:conversation_id"
db = "main"
db_namespace = "bridge"

[actors.exhentai_fetch]
library = "exhentai_fetch"
enabled = true
partition = "source_bot:conversation_id"
db = "main"
db_namespace = "exhentai_fetch"
```

The message-store actor owns normalized received-message persistence and emits
`obcx::message_store::events::MessageStored`. The bridge consumes that result and owns cross-platform
message mappings, media mappings, retry data, and bridge-specific migrations.
All three resolve the shared `DbManager` through `ActorContext`; none opens an
uncoordinated process-global database connection. ExHentai owns its schedules,
subscriptions, observations, and delivery ledger under tables derived from the
`exhentai_fetch` namespace. Its namespace-local schema metadata does not use or
change SQLite's database-global `PRAGMA user_version`.

## Pipeline ordering

```toml
[pipelines.message]
source = "obcx::core::events::RawMessageEvent"

[[pipelines.message.stages]]
name = "persist"
actor = "message_store"
input = "obcx::core::events::RawMessageEvent"
output = "obcx::message_store::events::MessageStored"
mode = "await"

[[pipelines.message.stages]]
name = "forward"
actor = "bridge"
input = "obcx::message_store::events::MessageStored"
output = ["bridge::events::MessageForwarded", "bridge::events::MessageForwardFailed"]
after = ["persist"]
mode = "await"
```

Use a stable conversation partition to serialize persistence and forwarding
for one conversation. Actor/database namespaces separate migration ownership;
schema changes must remain idempotent and package-owned. ExHentai additionally
uses `DbManager` writer transactions for multi-table scan and delivery-state
changes and acquires its namespace migration lock only for schema work.

## Verification

Message Store and Message Bridge each own their standalone build, schema,
behavior, and integration tests; run those suites from their respective
repositories against an installed matching OBCX SDK. Root tests do not inspect
or build either external repository. Core unit tests separately validate
pipeline references, DB routing, mailbox behavior, and shutdown using generic
root-owned fixtures.
