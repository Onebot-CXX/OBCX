# SDK 2.7 current-only coordinated cutover

This is an implementation/build handoff, not a published release, deployment,
restart or provider-operation authorization. Older product sets and SDK seals
must remain immutable. A matching version label alone does not prove binary
identity or full C++ ABI compatibility.

## Matching products

The six maintained actors require SDK/core `>=2.7.0,<3.0.0`; standalone CMake
uses the same floor. Input contract schema 3, scheduler ABI V2 and package
manifest schema 2 are independent and unchanged.

| Package | Source version |
| --- | --- |
| onebot-cxx.message-store | 0.2.1 |
| vollate.bridge | 0.2.0 |
| faccoco.chat-llm | 0.2.0 |
| vollate.exhentai-fetch | 0.2.0 |
| onebot-cxx.example | 0.2.0 |
| vollate.qq-forward-probe | 0.3.0 |

Bridge and Chat require Store `>=0.2.1,<0.3.0`. Path-mapping remains an independent
library, not a media authorization service. The authoritative registry is
`local_actor/actor-registry`; root `actor-registry/` mirrors its development
metadata only. No verified downloads, source revision pins or release assets
are implied by metadata changes.

## Current contracts

Use BotClient, full scoped references, current typed observations and complete
SendOutcome values. Current configuration uses ConversationResolver with explicit
kind/native ID/topic, never old storage-key parsing. Generic archive/reference
conversion, old messaging facades and historical media imports are removed.
QQ/TG display views consume current UserRefs only. Scoped native QQ profile/member
query selectors are query intent; matching provider responses supply user facts.

Common self queries stay cache-only; explicit standard login is a separate
operation using the existing identity coordinator. Preserve every confirmed
output and unknown effect on partial failure/cancellation. Only independently
eligible, retryable, definitely-not-submitted zero-effect attempts may retry.
Correlation alone is neither idempotency nor retry permission. Card rendering,
media conversion, filenames, cache and resource lifetime stay actor-owned.

Persisted references do not renew live media/reply authority. Restored detached
file observations and URLs cannot substitute for current receipts/account fences.
QQ ordinary Audio/File and Telegram WebM/TGS limitations remain explicit; no
unrequested codec or fallback is introduced.

## Persistent boundaries

| Owner | Current version | Runtime handling of older state |
| --- | --- | --- |
| Store | SQL schema 5; MessageStored wire 2 | Reject; no raw/native-event archive column or online conversion |
| Chat | Session schema 2 | Reject; source reads use current typed Store snapshots |
| Bridge | SQL schema 5; policy/EditIntent codec 2; retry outer format 1 | Reject; no native-scalar projection restoration or historical mapping/retry/quarantine API |
| ExHentai | Subscription 8; tag evidence 4; media journal 1 | Existing owner versions unchanged |
| Template/probe | No new persistent schema | Building does not authorize probe execution |

Bridge stores typed source members, full source/target scopes, immutable send
plans, actual group/topic facts, receipts and operation fences. Actual topic 1
and absent topic remain distinct route facts even if canonical conversation IDs
are equal. MessageForwarded and durable persistence-failure notifications use
schema 2 typed references, all outputs and optional typed quote anchors rather
than exported native message IDs.

## Operator-only offline utility

`scripts/migrate_typed_databases.py` is a standalone standard-library script,
not imported by SDK/runtime. The assistant wrote and syntax-checked it only;
it has not been executed against databases. Runtime/provider acceptance remains
unverified. Review and independently authorize its use on offline copies first.

It accepts Store 4/5, Chat 1/2 and current typed Bridge 4/5. Already-current
namespaces are not reconverted. Bridge may share Store's database; identical
paths are backed up/replaced once. It converts only named metadata, retains
unsupported historical rows as unsupported, and preserves receipts, attempt
results, retry payloads, command fences and all row counts. No historical row is
promoted into a new admission. It validates binding/canonical encoding and
unchanged-column hashes before replacement. Retired tables remain untouched.

Supply every path/namespace explicitly; stop all writers and checkpoint WALs.
The backup directory must not already exist. To inspect the plan:

```bash
python3 scripts/migrate_typed_databases.py plan \
  --store-db "${STORE_DB:?}" --chat-db "${CHAT_DB:?}" \
  --bridge-db "${BRIDGE_DB:?}" \
  --store-namespace "${STORE_NAMESPACE:?}" --chat-namespace "${CHAT_NAMESPACE:?}" \
  --backup-dir "${BACKUP_DIR:?}" --workers "${JOBS:?}" --confirm-stopped
```

After separately reviewing/approving the plan, changing `plan` to `apply` creates
consistent backups, converts candidates, checks integrity/counts/fences and
replaces originals only after all conversions succeed. Multi-file replacement
is journaled with rollback on caught failures, not cross-database atomicity.
After process interruption, inspect the journal/backups before starting anything.
The script never starts or restarts OBCX, fetches media or invokes providers.

## Backup, rollback and acceptance

Retain the complete matching core/app/SDK, transitive libraries/toolchain inputs,
actor DSOs/manifests/build identities, explicit configuration, consistent database
backups including WAL state, and cache/shared-media files with journals. Protect
credentials outside build evidence. Do not assume previously reported backup
paths still exist.

Before any new external effect, rollback can restore the complete old matching
set under separate approval. After new effects, reconcile newer receipts and
uncertain journals before considering rollback; restoring an older backup alone
can duplicate effects. Never lower schema numbers, clear fences, drop evidence
or copy only old actor DSOs to force a downgrade. There is no lossy reverse
migration.

Build core in an isolated directory, freeze its installed SDK and build actors
with scripts/actor_build.py using at least six workers. Do not build actor targets
in the live core workspace. Core replacement/restart is separate from actor hot
reload. Compilation does not establish loader, ACL, cancellation, media, provider,
startup, data-migration or recovery acceptance; those remain operator-owned and
must not be reported passed without actual evidence.
