> Historical design: the later `auto-detect-chat-llm-addressing` change supersedes this file's unchanged direct-mention policy, required actor bot_username, and unchanged context-v2 fingerprint. Apply/sync the newer addressing contract last. Only explicit authorized /chat is mandatory after that change.

## Context

The live actor still uses the previous `actors-chat-llm-final` artifact. Source already includes the verified but undeployed context-v2 work. Preserve that work, all staged changes, the frozen SDK, live artifacts, operational TOML/custom prompt, and source/core storage interfaces.

Read-only inspection of the actor-owned production session journal matched 51 of the 52 logged turns; the remaining turn has no tool calls. The 12 two-request turns comprise ten `view_image -> send_message` sequences and two `send_message(failed: provider_rejected) -> send_message(confirmed)` sequences. The latter are turns 237 and 262, whose request gaps are 10.383 and 13.084 seconds. Their second sends retain the same text and quote target. No `get_user_info` or `decline_reply` occurs in that window. The journal records the error category but not per-tool timing or provider retcode/message, so the complete gap cannot be assigned exclusively to upstream latency.

## Goals / Non-Goals

**Goals:**
- Eliminate the session's wake/active/decline-counter gate, including dormant state surviving a restart.
- Keep each eligible message durably scheduled when ambient processing is explicitly enabled, with existing ownership and pending-input coalescing.
- Make silence local to the current turn, with no effect on later admission.
- Expose tool execution and result-persistence durations without logging arguments, message text, native call IDs, identities, images, credentials, or reasoning.
- Verify behavior using isolated real application processes and fake providers, not unit tests.

**Non-Goals:**
- Removing explicit operator switches, ACLs, typed-command semantics, or existing direct-turn terminal-action constraints.
- Changing Telegram direct-request classification, persona, model/provider, token budgets, tool retry policy, or transport/core/message-store code.
- Changing source scan behavior or promising that this change reduces latency/cost; continuous assessment can increase calls.
- Deployment, live reload/restart, production schema/data migration, commits, archival, or reactivation of any old goal.

## Decisions

### Schedule independently of conversational activation

After the existing source/ACL/self-echo/idempotency checks, request work when `direct || proactive_enabled`. Preserve `direct` metadata and the earliest queued direct input protection. Do not add a cooldown, timeout, decline threshold, new classifier, or wake word under another name. Unchanged repeated events must not request duplicate work; distinct messages received while busy remain durable and may share the next turn.

### Remove only obsolete controls

Remove `active`/`declines` creation and all reads/writes from admission and terminal tools. Leave old members inert in already-stored documents: no migration, cleanup, or archive rewrite is needed. Keep the existing renderer fingerprint because system/tools/provider serialization are unchanged relative to context-v2.

Remove wake-word detection and actor alias state. `bot_names` and `bot_nickname` are retired options: if present, validate their former types and issue a fixed diagnostic, but do not require, infer, store, or use values. Remove them from the example; keep required `bot_username` for the unchanged direct-mention classifier. This avoids forcing an operational TOML edit solely to load the candidate.

### Content-free timing at the tool boundary

Measure monotonic execution time around each tool decision and separately time journal append of the result. Log a digest of the exact session scope, epoch, turn, model step, an allowlisted tool label, internally constructed status, and integer durations. Do not log `call.id`, arguments, user/group IDs, contents, profile fields, provider bodies or arbitrary unknown tool names. Add the same session digest to request metadata for correlation. Existing send intents, failure/ambiguous classification, actual gateway actions and recovery remain unchanged.

### Historical evidence is metadata only

Use SQLite URI read-only/query-only access for this one-time diagnostic, not actor storage implementation. Read a consistent transaction, extract only counts, statuses, turn numbers, equality/length facts and timestamps, and persist no raw document/call arguments/native continuation. The actor itself continues to use only core DbManager/IDbConnection and the existing BlockingExecutor.

## Risks / Trade-offs

- [More model calls after silence] -> This is the requested behavior. Retain explicit operator switches, budgets and existing per-session serialization; do not invent a replacement inactivity gate.
- [Legacy state/configuration] -> Old fields are inert; old option types remain checked. Acceptance includes sleeping-state restart and absent/present legacy options.
- [Misinterpreting tool durations] -> Execution includes queue/gateway/internal work; persistence is logged separately. Historical gaps are not retroactively precise tool spans.
- [Sensitive output in diagnostics] -> Allowlist tool names and emit no arguments/IDs/content/error bodies. Verify fake secret and unknown-name exclusion.
- [Live SDK/core identity mismatch] -> Build only in a new workspace against the existing immutable installed SDK; no claim of live reload compatibility.
- [Pre-existing numeric QQ self identity loss] -> The first focused run exposed that the installed SDK decodes a numeric `self_id` as an empty string before message-store admission (`BaseEvent::from_json` uses string deserialization rather than ID normalization). Preserve that failed run and source-row evidence. Actor identity-exclusion acceptance uses a string ID that the SDK actually preserves; confirmed-receipt suppression is also exercised separately. No core/message-store workaround, repair or migration is made. Deployment must not assume the numeric-input normalization problem is fixed.

## Migration Plan

No data/schema migration. Produce a new isolated actor candidate and record checksums and acceptance evidence. A later explicitly authorized deployment must select the artifact and verify runtime/SDK dependency identities; the live artifact and TOML remain unchanged now. Rollback is an explicit operational action with matched artifacts and preserved state, not automatic replay of sends.

## Open Questions

The exact OneBot provider retcode/message behind the historical `provider_rejected` outcomes is not retained in the current actor journal or ordinary log. Answering that requires provider-side evidence, not speculation or replaying the production sends.

The numeric QQ `self_id` issue requires a separately scoped core/SDK correction and verification before relying on sender-based self-message exclusion for those raw envelopes. It does not authorize modifying protected source storage or the live core in this actor-only follow-up.
