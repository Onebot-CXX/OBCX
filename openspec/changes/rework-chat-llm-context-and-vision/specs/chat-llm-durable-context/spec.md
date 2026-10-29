## ADDED Requirements

### Requirement: Conversations are isolated by installation and topic
Chat LLM SHALL key conversation journals, source cursors, activation, in-flight ownership, and decline counters by platform, bot installation, conversation, and exact optional topic. Only one writer/model turn SHALL own a conversation at a time. Group controls SHALL use their separately defined group scope rather than merging conversation histories.

#### Scenario: Two bots receive the same group ID
- **WHEN** two installations observe matching native group and message IDs
- **THEN** their journals, locks, activation, and reply targets remain independent

#### Scenario: Two topics are active
- **WHEN** two topics in one Telegram group have active conversations
- **THEN** each uses only its own journal and source eligibility, and null-topic history is not mixed into either

### Requirement: Model context is append-only within an epoch
A session SHALL persist an ordered journal of frozen text/image-marker projections, per-turn control messages, assistant output, provider-native reasoning state, and sanitized tool results. Within an unchanged epoch, the serialized canonical journal MUST preserve its preceding committed serialization as an exact prefix. Request rendering SHALL preserve that durable prefix except for explicitly declared endpoint-required reasoning projections; arbitrary omission or rewriting is forbidden. Temporary visual attachments SHALL be request-local suffix material, not durable journal entries. Full wire-request prefix equality across a visual turn boundary or a required reasoning projection change is not guaranteed, but the canonical durable prefix MUST NOT be rewritten to refresh an image or implement that projection. The actor MUST NOT reconstruct a moving last-N-message window or remove previous user inputs because they originated from commands.

#### Scenario: Conversation exceeds the former message limit
- **WHEN** a session exceeds 100 source messages while remaining below its configured token trigger
- **THEN** earlier committed messages remain in the same order and the next request appends new content rather than shifting the beginning

#### Scenario: Follow-up asks about a prior command
- **WHEN** a user follows up on an earlier direct chat command
- **THEN** the retained context contains that user question and the associated complete assistant/tool turn

### Requirement: Stable system instructions and tool definitions precede turn-local policy
System instructions and tool definitions SHALL remain stable within an epoch. Direct and proactive modes SHALL use the same declared tool set, with mode-specific policy appended as a current-turn control entry that remains in its original journal position. Runtime validation MUST still enforce allowed terminal actions and exact visible lookup/quote identities.

#### Scenario: Direct follows proactive
- **WHEN** a direct request follows a proactive decision without a semantic configuration change
- **THEN** the actor appends direct-mode control without replacing the system prefix or tool definitions

#### Scenario: Direct request tries to decline
- **WHEN** the model calls decline_reply during a direct turn
- **THEN** the executor rejects that action even though the tool exists in the stable tool set

#### Scenario: Text invents an identity
- **WHEN** summary text or user content mentions an ID absent from the trusted retained identity set
- **THEN** it does not authorize a profile lookup or platform quote

### Requirement: Complete tool interactions survive turn boundaries
The journal SHALL retain assistant call IDs, arguments, corresponding tool results, terminal send/decline results, and provider-native continuation necessary for replay. Every declared call in a retained completed turn SHALL have an explicit result, including not-executed results for calls bypassed after a terminal action. Provider-specific continuation MUST only be replayed with a compatible provider/model fingerprint. A view_image result SHALL retain only the stable image ID and outcome, never the resolved URL or image bytes; a later turn MUST call the tool again when actual pixels are needed.

#### Scenario: Successful send ends the loop
- **WHEN** send_message succeeds and the current tool loop ends
- **THEN** the confirmed receipt and terminal tool result are persisted before the turn is considered complete and are available in the next request

#### Scenario: Response contains calls after a terminal action
- **WHEN** one assistant response contains several calls and an early call ends the turn
- **THEN** the remaining calls are not executed and receive explicit not-executed journal results rather than leaving orphan call IDs

#### Scenario: Codex session resumes
- **WHEN** a compatible Codex session resumes after a restart
- **THEN** required output items, phases, call IDs, and encrypted reasoning are restored without being logged or sent to the group

### Requirement: Reasoning is durable protocol state isolated by conversation
The actor SHALL persist provider-returned reasoning regardless of whether its representation is plaintext, signed thinking, redacted blocks, or opaque/encrypted continuation. Each item SHALL belong to the exact platform, installation, conversation, optional topic, epoch, turn, and ordered journal position. Its native type, content, signature, phase, item/call associations, and required ordering SHALL be preserved and committed transactionally with the associated assistant/tool state. No actor-global buffer, asynchronous completion, retry, or cache SHALL supply another session's reasoning. Internal reasoning not returned by a provider MUST NOT be fabricated or claimed to have been saved.

#### Scenario: Plaintext reasoning is returned
- **WHEN** a supported endpoint returns a plaintext reasoning field with an assistant/tool response
- **THEN** the field is persisted as that turn's native reasoning state rather than discarded or merged into ordinary assistant content

#### Scenario: Signed or opaque thinking is returned
- **WHEN** a supported endpoint returns signed thinking, redacted blocks, or encrypted continuation
- **THEN** the actor preserves the complete protocol units and signatures without decrypting, rewriting, or truncating their protected contents

#### Scenario: Concurrent conversations have matching native IDs
- **WHEN** two installations, groups, or topics produce reasoning concurrently, including delayed callbacks or retries
- **THEN** each completion is checked against its owning session and epoch and only that session can persist or replay its reasoning

### Requirement: Every request restores reasoning according to an explicit endpoint contract
For every tool follow-up, later user turn, retry, and compatible restart, request construction SHALL retrieve the current session's retained reasoning and include all units permitted and required by the endpoint's verified replay contract. Adapters SHALL explicitly distinguish same-turn tool continuation, cross-user-turn replay, restart replay, and dependencies on accompanying messages/tool results. These rules SHALL be versioned with the provider/endpoint/model compatibility fingerprint, not inferred merely from a brand name or encryption status. A supported replay path MUST NOT silently drop reasoning and retain only final answers. A protocol-forbidden historical field SHALL remain local rather than be sent as an invalid field or converted to ordinary assistant text; this limitation SHALL be explicitly reported without exposing its contents. Unknown capabilities or missing required state SHALL fail the affected continuation rather than send an invalid request. Protocol projection MUST NOT rewrite the canonical journal.

#### Scenario: Plaintext endpoint permits cross-turn replay
- **WHEN** a later user turn follows a committed response containing reasoning_content and the endpoint permits that field in history
- **THEN** the next request includes it in the prescribed position with the associated assistant/tool messages

#### Scenario: Tool loop requires signed thinking
- **WHEN** an endpoint requires intact signed thinking with a subsequent tool result
- **THEN** the next request includes the original blocks, signatures, and required ordering without converting thinking to answer text

#### Scenario: Compatible restart resumes a conversation
- **WHEN** the actor restarts under the same compatible endpoint contract
- **THEN** the next request restores the session's replayable reasoning as well as its answers and tool history, without re-executing historical tools

#### Scenario: Endpoint forbids reasoning across user turns
- **WHEN** a verified endpoint permits same-turn reasoning continuation but forbids the corresponding field after a new user input
- **THEN** the actor preserves the local record, applies the declared request projection, and explicitly reports that cross-turn replay limitation rather than silently claiming full replay support

#### Scenario: Required reasoning is missing
- **WHEN** a pending tool continuation lacks a required reasoning block or its replay capability is unknown
- **THEN** the actor reports a bounded compatibility failure without inventing a block, submitting an incomplete sequence, or repeating an already executed action

### Requirement: Reasoning persistence is separate from display and redaction
All reasoning forms SHALL be excluded from ordinary logs, traces, metrics payloads, group messages, and toggle_think output. Native reasoning SHALL NOT become system policy or authorize actions. Editable plaintext fields that echo transient image access data SHALL be sanitized before persistence. Signed or otherwise integrity-protected contents MUST NOT be modified by sanitization; a detectable conflict with transient-data exclusion SHALL produce an explicit persistence/continuation failure without committing a contaminated or invalid block. Opaque continuation MAY be stored unchanged as protocol state, but the actor MUST NOT claim to inspect or sanitize its encrypted internals or use it to restore image URLs or pixels. Logs SHALL redact entire reasoning units rather than attempt to expose safe excerpts.

#### Scenario: Logging is enabled for a plaintext provider
- **WHEN** a response includes plaintext reasoning and tracing or error diagnostics are enabled
- **THEN** durable state retains the protocol-valid reasoning but ordinary diagnostics contain no reasoning content or raw response dump

#### Scenario: Signed thinking echoes a temporary image URL
- **WHEN** inspectable signed content contains a current-turn temporary image URL that cannot be removed without invalidating the block
- **THEN** the actor reports the conflict, does not persist that block or alter its signature, and does not repeat any already executed send

#### Scenario: Opaque continuation follows a visual turn
- **WHEN** the provider returns encrypted reasoning after viewing an image
- **THEN** it may be persisted and replayed unchanged under the compatible protocol, without claiming its internals are image-data-free or restoring a visual attachment from it; actual pixels still require view_image

### Requirement: Busy conversations do not lose admitted input
The actor SHALL retain admitted direct inputs locally and leave source rows unconsumed until their derived entries commit. In-flight exclusion SHALL schedule or merge later wakeups without dropping admitted content or advancing its cursor prematurely. Repeated source delivery SHALL be idempotent for an unchanged normalized projection.

#### Scenario: A command arrives during generation
- **WHEN** an authorized ChatCommand arrives while that conversation has an in-flight request
- **THEN** its input is durably recorded for subsequent processing rather than discarded by the in-flight guard

#### Scenario: An event is redelivered
- **WHEN** the same MessageStored observation with the same normalized content is handled twice
- **THEN** it creates at most one corresponding journal input

### Requirement: Recovery does not replay external side effects
The actor SHALL persist action intent before sending and record confirmation afterward. Recovery MUST distinguish confirmed, not-executed, failed, and ambiguous effects. It MUST NOT automatically resend an action that might already have reached a provider, fabricate a successful result, or submit an invalid incomplete tool sequence to the model.

#### Scenario: Crash occurs after provider acceptance
- **WHEN** a send intent survives but its confirmation was not committed before a crash
- **THEN** recovery reports an ambiguous effect and performs no automatic repeat send

#### Scenario: Completed session resumes
- **WHEN** a committed session is restored under the same fingerprint
- **THEN** its input order and epoch are restored without re-executing previous tools

### Requirement: Semantic changes create explicit epoch boundaries
A session SHALL fingerprint provider, endpoint, model, system instructions, tool definitions, rendering protocol, and the versioned reasoning replay contract. Changes that make replay incompatible SHALL establish an explicit new epoch rather than silently rewrite retained inputs. Compatible reloads SHALL preserve state after the prior generation drains; candidate preparation MUST remain side-effect-free.

#### Scenario: Provider is switched
- **WHEN** configuration switches a retained Codex session to OpenAI-compatible
- **THEN** the actor establishes a documented epoch boundary and does not forward Codex-only continuation to the new provider

#### Scenario: Reasoning contract becomes incompatible
- **WHEN** an endpoint, model, or reasoning protocol change invalidates retained blocks
- **THEN** the actor establishes an explicit epoch boundary, excludes the old reasoning from new requests, and retains old local records only under the normal actor-owned lifecycle rather than silently reusing or rewriting them

#### Scenario: Candidate is rejected
- **WHEN** reload validation fails
- **THEN** the active journal, epoch, and group controls remain unchanged

### Requirement: Toggle think is a group-scoped decline-reason control
The toggle_think command SHALL use the key `(platform, bot installation, group_id)` without topic. It SHALL atomically toggle and persist only whether proactive decline reasons are sent to that group, preserving the existing off behavior for an untoggled group. It MUST NOT be global, change model reasoning settings, alter the tool schema, or merge topic journals. A turn SHALL use a consistent control snapshot; later turns observe committed toggles.

#### Scenario: Toggle in one topic affects another
- **WHEN** toggle_think enables reason output in topic A
- **THEN** a subsequent proactive decline in topic B of the same installation and group uses the enabled control while its context remains topic B only

#### Scenario: Another bot or group declines
- **WHEN** a different installation or group performs a proactive decision
- **THEN** its reason-output setting is unaffected by the first group's toggle

#### Scenario: Control responses retain their destination
- **WHEN** a group-scoped toggle is invoked inside a topic and a later decline occurs in another topic
- **THEN** the toggle confirmation goes only to the command's topic and the reason goes only to the declining turn's topic, without broadcasting across topics

#### Scenario: State is restarted
- **WHEN** the actor restarts with the same core-managed state DB
- **THEN** it restores the group control independently of per-topic journals and does not modify reasoning_effort or invalidate their prefixes merely to read the control

### Requirement: Session cleanup is confined to actor-owned state
Expiry and compaction SHALL release only Chat LLM-owned journal projections and attachment locators no longer needed by retained sessions or active turns. They MUST NOT delete or pin message-store rows, modify source retention, or require source records to remain present for frozen text/marker replay. There SHALL be no persistent local image cache to reclaim; temporary visual material SHALL be discarded when its turn ends or is cancelled.

#### Scenario: Session expires during an active request
- **WHEN** a cleanup pass encounters attachment locators used by an active request
- **THEN** active-turn ownership prevents their reclamation until the request completes, without acquiring a source-table pin

#### Scenario: Another service deletes a source message
- **WHEN** source maintenance removes a row referenced by a retained LLM snapshot
- **THEN** source deletion is not blocked by Chat LLM and its text/marker snapshot remains usable; on-demand image retrieval can fail explicitly if the platform no longer serves the file
