## Why

Bridge schema version 2 scopes message mappings by bot installation but not by conversation, even though Telegram message ids are unique only within one chat. Production has already observed equal Telegram ids in different chats causing reverse reply lookup to select an unrelated QQ message with `LIMIT 1`; the same ambiguity can make reply, edit, recall, de-duplication, or retry behavior target the wrong conversation, so these paths must fail closed now and gain complete conversation-scoped identity.

## What Changes

- Represent every Bridge source and target message mapping as an exact installation, platform, conversation, and native-message identity; retain Telegram topic metadata separately where routing requires it.
- Replace singular platform/installation/message-id mapping queries with conversation-scoped forward and reverse APIs. Ambiguous or incomplete legacy matches return a typed diagnostic and perform no send, edit, delete, poke, mapping mutation, or alternate lookup.
- Add Bridge schema version 3 for conversation-scoped direct mappings, retry identities/completion mappings, and deferred media-group mappings, with forward and reverse indexes that allow equal native ids in different groups or chats.
- Add a startup-only, transactional version-2 migration with a preflight classification of resolvable, ambiguous, and unresolved rows. Resolve ownership only from exact Message Store source identity plus explicit current or operator-supplied route history; never choose the newest, oldest, first, or arbitrary row. Invoke this preparation before a runtime generation can accept ingress, using an additive typed V2 actor preparation hook that preserves compatibility with actors lacking the hook.
- Preserve unresolved version-2 rows only through an explicit operator-selected archive path that production mutation and de-duplication APIs cannot read. Otherwise migration fails without changing the database.
- Carry source and target conversation ids through direct forwarding outcomes, actor-owned mapping commits, retries, media groups, and `MessageForwarded` payloads while leaving raw ingress and Message Store schema/events unchanged.
- Scope Telegram/QQ reply resolution, edits, recall propagation, `/recall`, and `/poke` to the invocation's exact pair and source/target conversations. Multiple media-group source mappings use an explicit persisted primary message rather than unordered selection.
- Remove the control character from Bridge's QQ-recall replacement text and sanitize generated replacement text so local validation cannot reject an otherwise valid recall propagation.
- Add production-regression fixtures reproducing the observed same-installation, cross-chat `message_id=2700` collision and proving all destructive paths fail closed on unresolved ambiguity.
- **BREAKING**: Bridge schema version 3 is not readable by the version-2 binary. Rollback requires restoring both the preceding binary and the pre-migration database snapshot.
- Do not add bot actions, provider lookups, typed ingress, a Message Store migration, an outbox, reconciliation, a blob subsystem, fan-out, or new platform support.

## Capabilities

### New Capabilities
- `bridge-conversation-scoped-message-state`: Complete Bridge message identities, schema-version-3 migration/preflight, explicit unresolved-row handling, and conversation-safe forward/reverse state access.

### Modified Capabilities
- `actor-abi-v2`: Add an optional typed generation-preparation export so startup-only actor state is ready before route activation and reload candidates can request restart without replacing the active generation.
- `bridge-forwarding-mapping-persistence`: Direct and specialized mappings carry both conversations and retain single-owner writes without unsafe recovery lookup.
- `bridge-capability-forwarding`: Replies, edits, recalls, commands, and generated recall replacement text use exact conversation-scoped mappings and fail closed on ambiguity.
- `bridge-actor-message-retry`: Durable retry identity and successful completion mappings include exact source and target conversations.

## Impact

- Core actor runtime/SDK: additive optional generation preparation, typed failed/restart-required results, and pre-ingress invocation during generation construction; the V2 interface and existing actor libraries remain compatible.
- `local_actor/obcx-message-bridge`: storage models, forwarding outcomes, state repository APIs/schema/migration, generation preparation, handlers, commands, retry queue, media-group persistence, diagnostics, configuration for migration route history/archive policy, and standalone tests.
- Bridge SQLite state: `bridge_message_mappings`, `bridge_message_retry_queue`, and `bridge_media_group_mappings` advance to conversation-scoped version 3; unresolved historical mappings require explicit operator handling.
- Existing Message Store tables are read during migration preflight through their current `source_platform`, `source_bot`, `conversation_id`, and `message_id` fields but are not altered.
- Operations remain within the existing 13-action QQ/Telegram contract and exact configured installation pairs.
- Deployment requires stopping Bridge, taking a SQLite-consistent snapshot, running migration preflight/startup, verifying migrated plus explicitly archived counts, and restoring the snapshot together with the old binary for rollback.
