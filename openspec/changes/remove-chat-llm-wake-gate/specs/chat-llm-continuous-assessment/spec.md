> Supersession: `auto-detect-chat-llm-addressing` replaces the historical mention/reply→mandatory-direct policy and retires actor bot_username. Sync this older change before the newer addressing delta; only an authorized explicit ChatCommand is non-declinable. Its acceptance log remains historical evidence, not the current policy.

## ADDED Requirements

### Requirement: Ambient admission is independent of wake state

With explicit collection and ambient processing enabled, the actor SHALL schedule every eligible new non-bot group-message observation in its exact permitted session scope for LLM assessment. Prior replies, silence, old `active`/`declines` fields, or wake-word occurrences MUST NOT gate scheduling. Existing typed commands, permissions, deduplication, self-echo exclusion, durable pending admission, and single-session ownership SHALL remain intact. Under the superseding addressing change, only an authorized explicit ChatCommand SHALL be non-declinable; confirmed mentions/replies SHALL request declinable assessment even when ambient processing is disabled. Inputs arriving while busy SHALL remain available to a subsequent model turn, which can assess multiple newly admitted inputs together.

#### Scenario: First ordinary message without a wake word
- **WHEN** a permitted session receives an ordinary human message with ambient processing enabled and no prior command, mention, alias or active state
- **THEN** the actor commits a request for model assessment and submits a proactive turn

#### Scenario: Repeated silence never sleeps
- **WHEN** the model chooses `decline_reply` for at least six successive ambient turns
- **THEN** each subsequent eligible new message still receives model assessment without requiring a wake signal

#### Scenario: Legacy sleeping state survives restart harmlessly
- **WHEN** an existing document contains `active=false` and a decline count at or above five and the process restarts
- **THEN** a new eligible message still reaches the model without rewriting prior journal or epoch data

#### Scenario: Concurrent arrivals and repeated delivery
- **WHEN** new distinct messages arrive during an in-flight model turn and a source event is also redelivered unchanged
- **THEN** distinct content remains durably pending for later assessment, unchanged delivery causes no duplicate request, and no second owner runs concurrently for that session

#### Scenario: Explicit permissions remain authoritative
- **WHEN** collection is disabled, a message is outside exact permitted scopes, or the message is an excluded bot/self echo
- **THEN** removing wake gating does not authorize a model request or external action

#### Scenario: Explicit ambient switch remains disabled
- **WHEN** ambient processing is explicitly disabled
- **THEN** ordinary messages do not cause ambient model requests while existing authorized direct commands retain their behavior

### Requirement: Terminal actions do not reactivate or suspend future turns

A successful send or decline SHALL complete its current turn without maintaining conversational activation or decline counters. Fresh session documents SHALL omit `active` and `declines`; existing occurrences SHALL remain inert without schema migration or historical rewriting. Group think controls SHALL continue to govern only publication of proactive decline reasons.

#### Scenario: Decline followed by later reply
- **WHEN** multiple declined turns are followed by an eligible message for which the model selects `send_message`
- **THEN** the reply executes normally without an intervening wake event and prior completed turns remain unchanged

#### Scenario: Terminal action leaves legacy fields inert
- **WHEN** a send or decline executes in a document containing obsolete activation fields
- **THEN** the action does not read or modify those fields to influence later admission

### Requirement: Retired aliases confer no processing authority

The actor SHALL remove wake-word matching and SHALL NOT require `bot_names` or `bot_nickname`. If supplied for compatibility, the actor SHALL validate their former types, emit a fixed retirement diagnostic, and ignore their values. It SHALL NOT introduce implicit defaults for active configuration options.

#### Scenario: Aliases absent or present
- **WHEN** otherwise valid explicit configuration omits both retired aliases or supplies correctly typed legacy values
- **THEN** validation succeeds and ordinary-message assessment is identical in both configurations

#### Scenario: Malformed legacy aliases
- **WHEN** a retired alias option is present with an invalid type
- **THEN** configuration validation fails rather than silently interpreting that value

### Requirement: Tool timing metadata does not disclose conversation contents

The actor SHALL emit session-correlated tool execution and result-persistence timings using monotonic durations, epoch/turn/step, an allowlisted tool label and internally generated result status. It MUST NOT log arguments, native call IDs, message/profile content, raw group/user identity, credentials, transient image material, protected reasoning, or arbitrary unknown tool names as part of these timings.

#### Scenario: Lookup or send completes
- **WHEN** a tool finishes and its result is appended
- **THEN** timing metadata distinguishes execution from result persistence and can be correlated to the exact session request

#### Scenario: Model supplies an undeclared tool name
- **WHEN** a response requests an undeclared tool with a sensitive-looking name
- **THEN** the timing uses a fixed undeclared-tool label and does not reproduce that name or its arguments
