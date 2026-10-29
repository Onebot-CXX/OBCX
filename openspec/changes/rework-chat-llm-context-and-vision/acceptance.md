# Final implementation and acceptance audit

Date: 2026-10-23. Scope: all 65 tasks of `rework-chat-llm-context-and-vision`, including the explicitly approved core image APIs and Bridge adoption. This records source implementation and isolated acceptance, **not deployment or live-provider acceptance**. Historical checkpoints in `implementation-notes.md` are superseded by this audit, not erased.

## Authoritative final runs

All paths below are relative to `/home/vollate/Codes/OBCX`. Fixtures run the actual application, platform adapters, gateway, unchanged message-store, actor shared libraries and DB transactions. Only endpoints, credentials and data are fake. Each scenario pool uses 20 workers; suites run sequentially to avoid multiplying process/thread pressure. No unit tests were added or modified.

| Evidence | Result | Log / artifacts |
| --- | --- | --- |
| Final actor-only build | Passed; 20 workers; Debug with trace enabled; frozen SDK unchanged | `build/chat-llm-durable/final-build-2.log`; actors in `build/actors-chat-llm-final/cmake/actors/` |
| Session/storage/reasoning/vision/compaction | **56/56 scenarios passed** after the final identity/privacy fixes | `build/chat-llm-durable/final-session-integration-3.log`; `build/chat-llm-durable/run-m41k3a3u/` |
| Codex TLS/SSE | **11/11 scenarios passed** after the final fixes | `build/chat-llm-durable/final-codex-integration-3.log`; `build/chat-llm-durable/run-mrmvr445/` |
| CLI/lifecycle | **28 validation cases, 4 startup cases and 3 reload scenarios passed** | `build/chat-llm-durable/final-validation-integration-3.log`; `build/chat-llm-durable/run-bvhmslz6/` |
| Core/Bridge | **12/12 scenarios passed**, using the final actor workspace; Bridge/core unchanged by subsequent chat-only fixes | `build/chat-llm-image-core/final-image-integration-2.log`; `build/chat-llm-image-core/run-nkpithh4/` |
| Tokenizer/accounting differential | **243 actual serialized requests matched** upstream tiktoken 0.14.0, including **35 visual and 10 summary requests** | `build/chat-llm-durable/final-accounting-reference.log`; consumes the final session/Codex artifacts above |
| Formatting | Root `treefmt` passed; no changes | `build/chat-llm-durable/final-format.log` |
| SDK/scope/linkage/whitespace | Explicit integrity checks, SDK hashes/import, public headers, unchanged protected files and removed private SQLite dependency/API use | `build/chat-llm-durable/final-integrity.py` and `final-integrity-5.log` |
| OpenSpec | Strict change validation | `build/chat-llm-durable/final-openspec-validation.log` |

The application is `build/chat-llm-image-core/src/app/obcx`; the installed SDK is `build/sdk-chat-llm-get-image`. Initial isolated core/app builds, SDK snapshots and the earlier 12-case image run are recorded in `implementation-notes.md`. There are **zero registered CTest tests** in the selected core and actor workspaces (`ctest --test-dir ... -N`); this is not reported as a unit-suite pass. Existing relevant runtime/CLI fixtures were adapted instead.

Reference Python wheels were hash-verified and unpacked only in `build/chat-llm-durable/reference-python/`. Provenance: `reference-python/provenance.json`; tiktoken 0.14.0 wheel SHA-256 `e3442bbb2f0c588cec876061e37ae67b455b9df9978b003c8fe30e45f2ef5b42`, regex 2026.9.29 wheel SHA-256 `2f7f7aa47b229f2b39a2ae2596d2ad5625d77b5eb9856fac2dab3eb506cdd0a0`. The vocabulary SHA-256 is checked independently by both actor and reference runner. The comparison uses the actual serialized JSON and runtime estimates, not a duplicate unit implementation or simulated billing.

### Scenario inventories

Session runner, all included in its current default list:

```text
openai deepseek openrouter signature-only busy active-cleanup
source-same source-separate recovery invalid-identities forged-image
direct-decline unknown-reasoning no-view pure-image qq-path-only qq-missing
qq-deadline qq-expired tg-success tg-byte-limit tg-declared-limit
tg-unicode-caption tg-unsupported-format tg-body-limit image-count
lookup-and-send signed-echo signed-credential-echo signature-value-echo
call-id-echo plaintext-echo credential-echo encoded-credential-echo isolation
toggle compaction summary-failure summary-forged summary-chunks
summary-transaction summary-timeout summary-oversize oversized echo
missing-reasoning cancellation visual-cancellation image-reuse
image-no-refresh cursor-rollback source-collisions cleanup receipt-failure
confirmed-gap accounting
```

Codex runner:

```text
codex-replay codex-vision codex-tg codex-protected-echo codex-unknown
codex-truncated codex-failed codex-incomplete codex-cancelled codex-missing
codex-compaction
```

Core/Bridge runner:

```text
success path-only unsafe-url bad-size conflict unavailable unsupported
invalid-id isolation album-over-limit forward gif-probe
```

Each scenario's folder retains `config.toml`, runtime/SQL logs and fixture DBs, plus controlled provider request/response captures where applicable. These captures intentionally contain **fake** reasoning/credentials for assertions; they are not ordinary production logging. No real provider secrets were used.

## Task-by-task requirement matching

Actor paths in the following table are relative to `local_actor/chat_llm/`. The scenario names refer to the final runs above, not earlier partial passes.

| Task | Implementation / inspected contract | Requirement-matched evidence |
| --- | --- | --- |
| 1.1 | Initial baselines and approved scope in implementation notes; protected storage/core files unchanged | Final integrity audit and repository diff allowlist; message-store HEAD remains `79a74ff5bfe3bdcf31a6e4d15b315bbf2886c2aa`, clean |
| 1.2 | `SourceMessageReader`, existing v4 writer/event definition, `CommandParser` | Actual 17-row paging/JOIN/merge, raw representations, exact scopes and source mutations in source scenarios |
| 1.3 | `docs/provider-contracts.md`, core OneBot image types, typed TG fetch, explicit accounting profile | Pinned LLOneBot contract; actual QQ/TG `image_url` and Codex `input_image` bodies; missing capability/path/expiry failures. Live endpoint acceptance is explicitly not inferred |
| 1.4 | `scripts/actor_build.py`, snapshot/import checks | Initial SDK baseline plus final imported SDK audit and successful 20-worker actor-only build |
| 1.5 | `src/reasoning_contract.cpp`, versioned four-contract matrix and pinned protocol references | DeepSeek plaintext, OpenRouter signature/nullable-text/opaque units, Codex items; cross-turn/restart/switch/compaction and unsupported/missing state. No new Claude transport; redacted representations only through the declared OpenRouter opaque mapping |
| 2.1 | `ContextConfig::parse`, `load_configuration`, explicit DB declaration checks | 28 CLI cases; integer/k/K equivalence and strict missing/type/value rejection; explicit example enumerates all options |
| 2.2 | Relationship, fixed ordinary/summary body, profile and media bounds | CLI invalid/fixed-budget cases; image-count, deadline, zero reuse/refresh, declared/received TG caps and visual-body cap |
| 2.3 | `SessionRepository` schema v1 and session/epoch documents | Actual DB journal, native owners, refs, controls, receipts, attachments and summaries; privacy assertions on persisted documents |
| 2.4 | Repository operations only through DbManager/IDbConnection; callers use core BlockingExecutor | Source review, runtime SQL observer and dependency audit; deleted old SQLite repository; no declared actor SQLite dependency or undefined SQLite C API symbols (existing SDK public linkage still supplies SQLite) |
| 2.5 | Early ValidationOnly return, read-only startup/reload probes, explicit startup initialization | 28 CLI + 4 startup + 3 reload cases; SQL snapshots; no actor DB calls in validation, no candidate initialization/remote calls/legacy access. Existing core DB opening is allowed and unchanged |
| 3.1 | Complete MessageRef, validated identifiers, bound SQL; observation key includes full logical reference, not only numeric id | Source collisions across bots/platforms; actual stored deduplication keys checked against full reference hashes |
| 3.2 | Same-instance LEFT JOIN and separate-instance bounded resolution | Both source scenarios assert the actual SQL path, equivalent retained content, no ATTACH/mandatory FK/source writes |
| 3.3 | Fixed high-water bootstrap, reverse budget selection then insertion-id pages, transactional cursor | 110 messages over 17-row pages; delayed timestamp and injected admission failure leave cursor/refs/pending unchanged, retry admits once |
| 3.4 | Frozen projections, reference rescans and fingerprint comparison | Source edit/delete and existing-id delayed edit preserve old prefix; missing retained rows retain frozen input; absent unmaterialized rows fail without repair |
| 3.5 | `project_message` keeps only text/markers/identity/native locators; local admission transaction | Source snapshot comparison, journal schema and privacy assertions; no full payload/raw/archive or transient image data in actor state |
| 4.1 | Handler accepts existing MessageStored, resolves complete source identity; no RawMessageEvent handler | Real ingress→unchanged source writer→typed event exercised; exact bot/group/topic/null scope isolation |
| 4.2 | Typed ChatCommand, authorized argument projection, local optional source and Consume | Multiturn command recall; commands absent from source snapshot; existing core ACL/command interfaces unchanged |
| 4.3 | Exact scope keys, ownership/pending queue, atomic admission+wakeup | Seven concurrent scopes; busy command committed while previous provider request blocked; cursor rollback; late completion cancellation |
| 4.4 | Fixed prompt/tools and journal append, turn-local mode controls, separate request-local visuals | >100 messages, mode switch, exact old-journal prefix comparisons, same tools/system in proactive/direct, historical image re-view |
| 4.5 | Trusted identity sets in PromptBuilder and tool executor | invalid-identities, forged-image, direct-decline, summary-forged; no unauthorized lookup/quote/image network operation |
| 5.1 | Persist assistant/tools/results, explicit terminal bypasses; recovery closes pending calls in order | Pairing assertions, terminal-chain, lookup-and-send; confirmed-gap retains native receipt even when result persistence failed |
| 5.2 | Native state with scope/origin epoch/turn/ordinal/compatibility owner; contract-aware sanitization | DeepSeek/OpenRouter/Codex captured native units equal replayed units; concurrent owners checked; protected/signature/identity conflict cases |
| 5.3 | Intent before invocation, confirmed/failed/ambiguous outcome, conservative recovery | SIGKILL after acceptance, receipt-failure, confirmed-gap, cancellation and visual retry; no repeated historical sends or unpaired retained calls |
| 5.4 | Local receipts and exact pending echo removal, not text matching or fake events | echo races before receipt commit; no extra model request, no duplicate pending bot input, no source backfill |
| 5.5 | Separate group-keyed controls and stable native command IDs | toggle spans two topics; different groups/installations unaffected; persisted controls survive restart |
| 5.6 | Turn-start think snapshot; exact confirmation/decline topic targets | toggle while a model request is blocked; original turn uses its snapshot, later turns see change; thinking flags/tools/epochs unchanged |
| 5.7 | Versioned renderers, local failure for required missing state; declared no-tools projection | Actual tool/later-user/restart replay; Codex 401 retry identical body; missing-reasoning/codex-missing fail before another request. All implemented normal tool contracts allow cross-user replay; no invented forbidden-cross-turn endpoint is claimed |
| 5.8 | Fingerprint includes actual endpoint/provider/model/prompt/tools/renderer/contract; archived epoch | Compatible restart prefix equality; incompatible model and Codex→Chat switch archive original journal without replaying native state; toggle does not change epoch |
| 6.1 | Ordered native segment projection, stable IDs independent of optional TG metadata, trusted locator registry | Pure/multiple images, caption and supplementary-Unicode mention, source old URL excluded; no-view zero fetching |
| 6.2 | Stable view_image schema, trusted scope validation and bounded tool/image counts | Forged IDs rejected before I/O; image-count and direct/proactive shared tool set; no extra image trigger permission |
| 6.3 | Exact-installation typed `GetOneBotImageRequest`, download:false; no downloader/path/source fallback | QQ historical re-resolution, unavailable/path-only/deadline/expiry; core unsafe URL and invalid locator cases |
| 6.4 | Typed `FetchTelegramFileRequest` with explicit byte cap, bounded memory and transient data URL | TG success + Codex TG, declared/received byte limits, unsupported format; no token-bearing URL in model/state/logs |
| 6.5 | Turn-local Visual objects/wire associations, explicit reuse deadline and refresh bound | image-reuse, image-no-refresh, qq-expired; actual visual cancellation; next turn/restart begins without old visual parts and resolves afresh |
| 7.1 | OpenAI image_url / Codex input_image actual parts after paired tool results | Actual serialized QQ/TG visual bodies, exact call/image associations; failures never claim successful visual access or silently omit failed images |
| 7.2 | SHA-pinned o200k BPE, ICU tokenizer, separate 4096-per-low-detail-image bound | Differential 243 actual requests, 35 visual; unopened marker text only; no QQ download for accounting; metadata-unknown profile explicitly conservative |
| 7.3 | Complete body/text/protocol/opaque/signature/image/reserve admission on every request | Differential ordinary/tool/retry/summary telemetry; oversized input and actual TG data-URL body cap prevent submission; opaque bytes charged separately, not BPE or zero |
| 7.4 | Explicit supported output selector; Codex none + diagnostic; bounded image error classification | Actual request fields, identical bounded auth retry, expiry refresh/no-refresh, unsupported/failed/incomplete provider paths; Codex reserve not described as a server cap |
| 7.5 | Optional usage, safe prefix/visual telemetry, no raw response logs; editable/protected/opaque distinction | Trace enabled throughout; usage unknown versus explicit 0; full URL/credential/encoded echo, signed text/signature/native ID conflicts; opaque payload retained unchanged without internal-sanitization claim |
| 8.1 | Complete-turn boundaries and recent/current retention, threshold check | >100 below threshold; compaction, oversized, summary-chunks; no rolling trim or reduced recent-turn requirement |
| 8.2 | Read-only no-tools summary from public transcript, bounded complete-turn chunks, trusted images | Multi-chunk backlog; summary-forged rejection; all summary bodies omit native reasoning/actual visuals/tools and cause zero image resolution |
| 8.3 | Recount candidate and commit summary/tail/epoch/boundary/attachments with archive in one transaction | Successful compaction establishes epoch 2 and frozen summary; summary-transaction fault leaves epoch 1 and old prefix/archive state intact |
| 8.4 | Failure keeps old state and checks hard budget before uncompressed fallback; cancellation propagates | Provider failure, timeout, overlarge/forged summary, transaction failure and oversized indivisible input; no known overbudget submission |
| 8.5 | Complete native/assistant/tool units retained or retired together; no raw reasoning summary | OpenRouter and Codex compaction compare retained units byte-for-byte, exclude retired native items and preserve old archive; compatible post-compaction restart and append |
| 9.1 | Old MessageRepository/ConversationStateStore deleted, source lists and package deps updated | No private SQLite API or declared dependency; unchanged SDK still publicly links SQLite. Declared ICU/Crypto/toml++/message-store closure; no chat QQ HTTP/media-store/shared-storage dependency |
| 9.2 | Explicit example with same/separate DB, requires/partition and existing event pipeline | CLI parses adapted real example; source writer unchanged; no inferred 128k/URL expiry defaults |
| 9.3 | Rewritten features/provider/Codex docs with matrix and privacy/epoch boundaries | Documentation checked against actual serialized requests, native state and failure evidence, distinguishing fixture from live capabilities |
| 9.4 | `docs/migration.md` manual retirement, namespace selection, SDK release and rollback | Unreadable legacy sentinel untouched in CLI/startup/reload; no old table/file creation/import/deletion; no source migration/restart operation |
| 9.5 | Diff and dependency closure constrained to actor plus authorized image core/Bridge work | Final integrity and scoped diff review; machine-specific provider provenance stays ignored; no commits/archive/deployment |
| 10.1 | Frozen SDK actor helper, 20-worker build and available integration entries | Build/import/hash logs; CTest inventory explicitly zero, not counted as a pass; actual runtime suites above |
| 10.2 | Storage/reference integration with real repositories/SQL observer | Same/separate DB, collisions, pages, rollback/retry, raw forms, exact scopes, edit/delete and actor-only cleanup |
| 10.3 | Real command pipeline/provider/DB/sending fixture | 110 inputs, command recall, mode switch, tool pairing, self echo, durable busy admission, interrupted sending recovery |
| 10.4 | Persistent control runtime fixture | toggle concurrency/snapshot, topic destinations, restart, exact group/bot isolation and stable reasoning/tool settings |
| 10.5 | Real gateways and serialized provider requests, controlled media servers | QQ/TG lazy/history/expiry/count/byte/format/body/capability failures, authorized IDs and privacy, no persistent image cache |
| 10.6 | CLI plus upstream tokenizer and compaction evidence | Integer/k/K/invalid configuration, image-versus-marker estimates, conservative unknown size, summary trust/rollback/new-epoch append |
| 10.7 | Separate ValidationOnly/startup/reload and actual cancellation/privacy exercises | No actor validation SQL or remote actions; candidate nonmutation; old sentinel intact; text/visual cancellation and access echo checks; live cache explicitly unverified |
| 10.8 | This audit, updated implementation notes and deployment/provider docs | Complete implementation/evidence/limits/explicit operator inputs recorded; protected repositories/files unchanged; source completion not deployment |
| 10.9 | Real Chat Completions and TLS/SSE clients with controlled native state | Plaintext/signed/signature-only/opaque replay, parallel isolation, tools/restarts/switches/missing/unknown state, atomic compaction rollback, think independence and trace privacy; no live signature acceptance claim |
| 11.1 | Official OneBot contract and pinned LLOneBot/LuckyLilliaBot source | Corrected provider, explicit download:false, path versus URL and no expiry/refresh guarantee; prior Lagrange assumption withdrawn |
| 11.2 | Core OneBotImageRef extraction/validation | Invalid native ID rejected without request; identity/location/credential checks; stable file+installation, no URL persistence |
| 11.3 | Typed action/registry/client/endpoint and serializer, checked response association/metadata | Actual exact-bot get_image wires; path-only/unsafe/overflow/conflict/unsupported/cache-missing cases; no rkey action or synthetic expiry |
| 11.4 | Core ImageFetchRequest/fetch_image, explicit timeout/cap/Range, fixed safe errors | Range-ignoring 2048-byte response against 1024-byte cap; no redirects/persistence/retry policy in primitive |
| 11.5 | Bridge single/album/forward/GIF acquisition migrated; unused downloader removed | All 12 image scenarios; order/placeholders, forwarding targets, MIME/GIF behavior and native locator use preserved |
| 11.6 | Public image headers exported by explicit SDK install lists; interface/Bridge docs | Final exported-header equality; actor-only compile/link; protocol implementation/registry headers remain intentionally core-private |
| 11.7 | Isolated core/app/SDK builds and new immutable snapshot | Prior build/snapshot logs plus final hash checks of new and earlier SDKs; no live core/old SDK overwritten |
| 11.8 | Matching final Bridge/chat actor build and runtime capability/error exercises | 20-worker helper build; final 12-case Bridge run and all lazy actor image cases; no private transport fallback |

## Failures encountered and resolved (not counted as passes)

- A late recovery edit compared a C++ string directly with JSON; compilation rejected it. Explicit string extraction fixed it; the final build and receipt-gap/recovery runs include the fix.
- The old shared fixture certificate expired during acceptance. New **fake** TLS material was generated only under `build/chat-llm-durable/tls/`; no production trust or credential was changed. All final runs use this fixture certificate.
- Running several 20-worker suites simultaneously exceeded the small Bridge fixture startup deadline, and a cancelled polling response exposed fixture header-write BrokenPipe handling. Suites were rerun sequentially with 20 workers each; only expected closed-client socket errors are ignored, not application/assertion errors.
- A Codex summary assertion initially confused the top-level `include: ["reasoning.encrypted_content"]` request flag with replayed native data. Corrected assertion inspects actual `input`; final summaries contain no historical native reasoning or visuals.
- The final audit added literal signature/call-identity access-conflict rejection and complete logical-reference deduplication. The 56/11/lifecycle and accounting runs above are **after** those source changes.
- SDK header auditing initially included core-private protocol/registration headers. The corrected audit uses the explicit public install lists; no SDK content was added or silently skipped to conceal a missing public API.
- Link auditing found SQLite in DT_NEEDED because the unchanged installed core exports `SQLite::SQLite3` in its public link interface. Removing chat_llm's private SQLite dependency does not remove that core dependency. The final audit verifies the package/CMake declarations and absence of undefined SQLite C API symbols instead of falsely claiming the artifact has no transitive SQLite linkage. Core was not modified to change this.

## Reproduction and deployment boundary

```sh
# Run from /home/vollate/Codes/OBCX with the existing isolated fixture resources.
python3 scripts/actor_build.py build --output build/actors-chat-llm-final --jobs 20
python3 build/chat-llm-durable/session-runtime-integration.py
python3 build/chat-llm-durable/codex-runtime-integration.py
python3 build/chat-llm-durable/validation-runtime-integration.py
python3 build/chat-llm-image-core/image-runtime-integration.py
python3 build/chat-llm-durable/accounting-reference.py \
  build/chat-llm-durable/run-m41k3a3u build/chat-llm-durable/run-mrmvr445
python3 build/chat-llm-durable/final-integrity.py
openspec validate rework-chat-llm-context-and-vision --strict
```

The ignored fixture scripts/resources are local acceptance artifacts, not an installed unit suite. Fresh runs print their own artifact directories; use those for a new differential comparison. Fixture TLS validity is finite; renew only fixture material if needed. Do not substitute production DBs or credentials. Full actor configure command, dependency provenance and initial SDK generation evidence are in implementation notes and the actor build manifests.

Still requiring separately authorized **environment acceptance/operations**, not claimed by this implementation:

- Real endpoint/model authorization, actual cache/billing, server-side signature acceptance, and Codex external-image access.
- The deployed LLOneBot version, native-cache retention and actual CDN/signature freshness; repeated lookup is not a freshness guarantee.
- Production DB/namespaces, prompt/vocabulary/credentials, budgets and media policies must be explicitly selected. Existing source bindings/cursors must not be repurposed for a different data source; choose a fresh state namespace for that operation.
- Core/SDK publication and process restart, actor deployment, and any manual old-history deletion. **None was performed.**

No message-store/DbManager/source-schema change, private SQLite connection, new shared storage library, QQ LLM downloader, local image cache, new unit test, live deployment/restart, automatic deletion, commit or archive was introduced.
