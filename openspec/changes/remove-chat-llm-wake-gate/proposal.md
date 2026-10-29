## Why

The user wants the LLM to assess every eligible new group-message turn rather than stop seeing messages after five declines. The current wake-word/active/decline-count gate changes that behavior outside the model, while existing logs do not identify which tool caused the observed inter-request delays.

## What Changes

- Remove wake-word activation and the session active/decline-count state machine. With the explicit ambient-chat switch enabled, every eligible new message requests model processing; silence ends only the current turn.
- Preserve explicit collection/ambient operator switches, exact-scope ACLs, typed commands and direct-turn policy, deduplication, self-echo exclusion, durable admission, and per-session ownership/coalescing. This does not require a separate HTTP request for every event arriving during an existing turn.
- Retire `bot_names` and `bot_nickname` as wake controls; accept and validate old occurrences only for configuration compatibility, without requiring or using them. Do not introduce option defaults.
- Ignore legacy `active`/`declines` document members without migrating or rewriting historical state. Fresh documents no longer contain these members.
- Add content-free, session-correlated tool execution/result-persistence timings and retain a metadata-only audit of the reported production turns.
- Build and verify an isolated candidate against the frozen SDK. Do not deploy, change operational configuration/custom prompt, restart the live service, or alter message-store/core storage.

## Capabilities

### New Capabilities

- `chat-llm-continuous-assessment`: Continuous model assessment of eligible ambient messages, independent of wake words and prior silence, with privacy-preserving tool timing evidence.

### Modified Capabilities

None in the main spec tree. This follow-up supersedes only the wake/activation/decline-counter behavior described in the completed, unarchived `rework-chat-llm-context-and-vision` change; its other requirements remain intact.

## Impact

Changes are confined to `local_actor/chat_llm` scheduling, trigger/configuration cleanup, fresh session construction, tool timing logs, and actor documentation/example configuration. No new dependencies, state schema, source storage changes, unit tests, or runtime deployment are introduced. Existing isolated real-runtime fixture scripts are reused and extended outside installed assets.
