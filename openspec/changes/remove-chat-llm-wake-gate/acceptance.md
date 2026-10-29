# Continuous assessment and tool-latency audit acceptance

## Outcome and scope

Implemented the requested actor-side removal of wake/active/decline-count gating on top of the existing undeployed context-v2 source. With the operator's explicit `proactive_enabled=true`, every eligible new ambient input schedules model assessment. `decline_reply` ends only the current turn. Existing permissions, operator switches, typed commands/direct policy, pending-input coalescing, native protocol, intent/receipt recovery and source ownership remain unchanged.

This is **not deployed**. No live restart/reload, operational TOML/custom prompt edit, production database mutation, message-store/core implementation change, commit or archive was performed. Production state was inspected through read-only/query-only SQLite transactions for metadata only; no raw journal, arguments, identities, images or native reasoning was exported.

**Known uncorrected boundary:** the selected frozen SDK loses numeric QQ `self_id` before the source writer receives the normalized event. The first focused run exposed this and is retained below. Actor exclusion is verified for preserved identity metadata and confirmed receipts; these results do not claim that the upstream numeric-ID normalization defect is fixed. Deployment requires separate consideration of that core/SDK issue and dependency identity compatibility.

## Production tool audit

Evidence: `build/chat-llm-no-wake/tool-audit.json`, correlated from
`logs/obcx-bridge-2026-10-24T01:13:24.log` and the retained actor session journal.

- 64 model-request stages across 52 turns.
- 51 turns have declared tool calls; turn 309 failed with `Codex HTTPS request via configured proxy failed or timed out` before declaring a tool.
- 53 `send_message` calls: 51 confirmed and two `provider_rejected` failures.
- 10 `view_image` calls: all available for the current turn.
- Zero `get_user_info` and zero `decline_reply` calls in this window.
- Ten two-request turns are `view_image -> send_message`. Their inter-request gaps are 20–57 ms; the additional model request, not a ten-second image lookup, accounts for the extra round.
- Turn 237: model stage 4.033 s, failed-send/result/preparation gap 10.383 s, model stage 7.150 s; 21.566 s from first request to second result.
- Turn 262: 3.521 s, 13.084 s, 8.893 s respectively; total 25.498 s.
- Both slow turns declare `send_message` again after the failure, retaining identical text and quote targets. This is a new model-selected call, not an actor transport retry added by this change.
- Historical logs have no per-tool spans or retained OneBot retcode/message. The full gap includes persistence/preparation and cannot be represented as a precise upstream-service measurement. No production send was replayed to diagnose it.

## Implementation

- Removed all conversational `active`/`declines` creation and reads/writes from admission, fresh state and terminal tools. Legacy fields remain inert without schema migration or rewriting archives.
- Removed wake-word detection and alias storage. Retired `bot_names` / `bot_nickname` are optional, type-checked compatibility inputs with a fixed warning, never defaults or triggers.
- Retained explicit `collect_enabled`, `collect_allowed_groups`, `proactive_enabled`, `bot_username`, command handling and direct-turn constraints.
- Added a session digest to request metadata and allowlisted tool timing labels with `execution_ms` and `persistence_ms`. No arguments, native call IDs, group/user identity, content, profile data, images, provider bodies or reasoning are added to these timing records.
- Updated actor feature/migration/OAuth/test-design documentation and the example configuration. No new unit tests were written or modified.

## Isolated candidate

Frozen SDK: `build/sdk-chat-llm-get-image`.

```sh
python3 scripts/actor_build.py configure \
  --sdk build/sdk-chat-llm-get-image \
  --workspace packages.toml \
  --cache build/chat-llm-image-core/package-state/sources \
  --output build/actors-chat-llm-no-wake \
  --actors faccoco.chat-llm \
  --mode development --configuration Debug --debug-trace ON
python3 scripts/actor_build.py build \
  --output build/actors-chat-llm-no-wake --jobs 20
```

Candidate: `/home/vollate/Codes/OBCX/build/actors-chat-llm-no-wake/cmake/actors/chat_llm.so`.

SHA-256: `1d33e076036e48f7830927be94186c62790778df2bca46896875ff39b67a78f4`.

Transitive fixture `message_store.so`:
`b78b21b2bb2ab5683ece67243f7263da9b1d72ac51646c2c2eeca59bb81e2301`.

Only actor targets were built; the helper verified the imported immutable SDK before/after. The previously built final and prompt-v2 actors and live core were not overwritten.

## Verification

All runtime scenario batches used 20 parallel workers and ran as sequential batches. They launched isolated real application processes against fake local providers/bots and fixture-only DBs, including fault injection and fixture restarts.

| Check | Result | Log under `build/chat-llm-no-wake/` | Artifact directory under `build/chat-llm-durable/` |
| --- | --- | --- | --- |
| Continuous-assessment/configuration scenarios | 23/23, for preserved identity metadata; numeric-ID boundary below remains open | `continuous-runtime.log` | `run-esfj7f2u/` |
| Existing session/storage/reasoning/vision scenarios | 56/56 | `session-runtime.log` | `run-xcdo6lkr/` |
| Codex TLS/SSE scenarios | 11/11 | `codex-runtime.log` | `run-uc984bx5/` |
| CLI/startup/reload scenarios | 28 CLI + 4 startup + 3 reload | `validation-runtime.log` | `run-8q6ca51a/` |
| Context-v2 layout/epoch/privacy scenarios | 12/12 | `context-runtime.log` | `run-26_ih2l_/` |
| Actual-request accounting against tiktoken 0.14.0 | 319 requests, including 39 visual and 10 summary requests | `accounting-reference.log` | Four request-capture runs above |

Focused coverage includes both providers and QQ/Telegram: first ordinary message without activation, six successive declines followed by another assessed message, legacy `active=false` / `declines=87` remaining inert across restart and later sends/declines, exact topic destinations, image-only ordinary inputs, busy-session coalescing and duplicate delivery, explicit switch/ACL guards, preserved self identity and early confirmed-receipt suppression, retired option omission/type checks, and content-free timing correlation.

The timing fixture injects a 100 ms profile lookup and a 200 ms send rejection. Recorded execution/result-persistence spans include `get_user_info=105/3 ms`, failed `send_message=204/1 ms`, and successful `send_message=3/1 ms`. An unknown model-supplied name appears only as `undeclared` in timing logs. These are local fixture observations, not live provider measurements.

### Preserved initial failure and narrowed claim

`continuous-runtime-first.log` and `run-0zfg6q_v/` preserve the initial 22-pass/one-failure run. The failed scope-guard probe supplied numeric OneBot `self_id=42`. Inspection of its real source row showed `self_id=""` while `user_id="42"`, and the resulting actor projection had `is_bot=false`; Telegram bot metadata remained intact. `src/common/message_type.cpp::BaseEvent::from_json` uses generic string deserialization for `self_id`, unlike the identifier normalization used for message/user/group IDs.

No application or core code was changed to conceal this failure. The final actor-side identity probe explicitly supplies string `self_id="42"`, which the selected SDK preserves, and retains all original exclusion assertions. The numeric-input defect is documented as uncorrected, not reclassified as an invalid OneBot input or a passing test. Receipt-based echo suppression is checked separately. Neither this follow-up nor the historical echo fixture proves sender-based suppression when upstream identity metadata has already been lost.

## Integrity and reproducibility

`build/chat-llm-no-wake/integrity.json` verifies all 13 protected hashes unchanged, unchanged root/actor staged patches, unchanged pre-existing root unstaged patch and no unit-test diffs. Protected files include live/final/prompt-v2 actor/core artifacts, the frozen SDK core, runtime TOML, custom prompt, DbManager/executor/runtime/app and message-store source/header.

Targeted root-style clang-format checks, both repository `git diff --check` checks, Python compilation of the fixture scripts, strict OpenSpec validation, and absence of actor-private `sqlite3_` undefined symbols all passed.

```sh
export OBCX_FIXTURE_ACTOR_BUILD=build/actors-chat-llm-no-wake
python3 build/chat-llm-no-wake/continuous-runtime-integration.py
python3 build/chat-llm-durable/session-runtime-integration.py
python3 build/chat-llm-durable/codex-runtime-integration.py
python3 build/chat-llm-durable/validation-runtime-integration.py
python3 build/chat-llm-prompt-v2/prompt-runtime-integration.py
python3 build/chat-llm-durable/accounting-reference.py \
  build/chat-llm-durable/run-esfj7f2u \
  build/chat-llm-durable/run-xcdo6lkr \
  build/chat-llm-durable/run-uc984bx5 \
  build/chat-llm-durable/run-26_ih2l_
```

New fixture runs print new directories; use their captures for a fresh accounting comparison. Candidate selection remains explicit and allowlisted, with no fallback to the live/final build. Local fixture success does not establish live model decision quality, authorization, cache/billing, external image reachability, OneBot rejection causes, or live SDK/core reload compatibility.
