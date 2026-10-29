# Implementation notes

**Final status: 65/65 tasks implemented and audited.** See [acceptance.md](acceptance.md) for the current task-by-task evidence, final run inventories and explicitly unverified live-provider/operational boundaries. The sections below preserve earlier checkpoints; their pending-task counts are historical.

## Initial workspace baseline

All context files returned by `openspec instructions apply` were read before implementation. Initial progress: 0/52. The only untracked root path was this change's planning directory; the three actor working trees below were clean.

| Repository | HEAD |
| --- | --- |
| OBCX core | `9847d8ed09b7548c37d2cfc9489eaaf41b525f38` |
| chat_llm | `2b19edc30f30b7f6cf420a1f4f244d8c0150bacd` |
| obcx-message-store | `79a74ff5bfe3bdcf31a6e4d15b315bbf2886c2aa` |
| obcx-message-bridge | `ba189a008417aac7e2cc943f81f9a204cb307b2c` |

Initial scope was chat_llm and necessary consumer configuration/dependency declarations. The user subsequently approved the minimal core/OneBot image-access extension and moving Bridge's existing image-fetch interfaces to core. The provider is explicitly LLOneBot / OneBot 11; the earlier Lagrange attribution was incorrect. This supersedes the initial core/Bridge read-only restriction, but message-store and DbManager remain unchanged. No shared storage library, source schema/index changes, enforced source foreign keys, source backfill, or source pin/cleanup are authorized. All actor database access must use the existing core DbManager/IDbConnection interfaces.

No deployment, restart, database deletion, migration of old history.db, or new unit tests are part of this execution. Build/verification must use the actor-only helper with a frozen installed SDK and at least six workers.

## SourceMessageReader contract verified (task 1.2)

- Existing `MessageStored.payload` fields: `message_id`, `source_platform`, `source_bot`, `conversation_id`, `sender`, `group_id`, `message_type`, nested `payload`. The envelope preserves original timestamp, raw, headers and exact source routing; it does not expose the table numeric id.
- Existing per-platform message table: `id INTEGER PRIMARY KEY AUTOINCREMENT`; unique `(source_platform, source_bot, conversation_id, message_id)`. Read columns: `id`, those four identity fields, `sender`, `group_id`, `message_type`, `payload`, `raw`, `timestamp`; `updated_at` can be read diagnostically but is not a revision log.
- Source startup/read checks use only SELECT: schema version must be supported v4; namespace/platform identity must match its registered table; required columns must be queryable. A not-yet-created platform table is an empty source, not permission to create it.
- Existing upsert keeps the row id while replacing payload/raw/timestamp. New-row id pagination cannot recover all historical edits. A newly observed changed model projection must append locally rather than mutate existing input.
- Empty SQL raw text denotes the exact payload value, not JSON null/empty-object/empty-string. Distinct raw values remain distinct.
- Telegram topic extraction follows the existing parser: `message_thread_id` only selects a topic when `is_topic_message` is true. A normal reply's thread id alone must not mix topic histories.
- Parameter binding covers values; namespace/platform table identifiers must be encoded and validated exactly. No source DDL/DML/index change is needed or authorized.

Evidence: `local_actor/obcx-message-store/src/message_store_actor.cpp` (schema, event construction, upsert and reconstruction), `include/core/actor/actor_messages.hpp`, and `local_actor/chat_llm/src/command_parser.cpp`.

## Isolated SDK/build environment verified (task 1.4)

- Frozen SDK: `/home/vollate/Codes/OBCX/build/sdk-user-info`; `scripts/actor_build.py::load_sdk` verified the recorded file hashes successfully.
- Existing actor-only build: `/home/vollate/Codes/OBCX/build/actors-user-info`; `verify_imported_sdk` verified that core is imported from that frozen SDK, not built in the actor workspace.
- SDK CMake directory: `lib64/cmake/obcx-sdk`; recorded target/artifact: `chat_llm_actor` / `chat_llm.so`.
- Available CPUs: 20; observed system load was low. Subsequent compilation and parallel verification should use 20 workers.
- The current actor build exposes no CTestTestfile.cmake. Existing local integration entry points were located under `build/user-info-core/` and `build/chat-llm-selectable-replies/`; these are historical local scripts, not evidence that the new feature has passed. They must be reviewed/adapted before running against isolated data.
- No compilation, process restart, or deployment was performed for this environment check.

## Earlier download-based media audit (superseded by the user's lazy-tool requirement)

Telegram's existing `FetchTelegramFileRequest` has an explicit `maximum_bytes`, rejects oversized declared files, and passes the cap down to HTTP response-body handling. No SDK extension is needed for that byte bound.

The installed public `HttpClient`/`ProxyHttpClient` APIs expose host, timeout, proxy and body limits but no way to bind a request to a caller-validated resolved address while preserving HTTPS hostname verification. The core-private CurlRequest has no such field either and its header is not installed in this SDK. Current curl setup has no CURLOPT_RESOLVE/CONNECT_TO hook; SOCKS5 uses remote hostname resolution. Therefore a preflight DNS check followed by an ordinary hostname GET cannot satisfy the proposal's DNS-rebinding protection. This is an architecture choice, not authorization to modify core.

This downloader decision is no longer pending: the user rejected persisting expiring URLs and specified stable `<image>...</image>` markers plus a model-invoked tool that requests a temporary QQ URL when needed. The proposal/design/specs/tasks now remove QQ download/address-pinning transport, local image storage, and media GC. Telegram uses the same lazy contract with current-turn in-memory bytes. Task 1.3 remains incomplete, now for the platform resolver and provider/profile capabilities below.

## Historical lazy-image capability gap (resolved below)

- `src/onebot11/adapter/protocol_adapter.cpp:582` implements `serialize_get_image_request`, and its header declares that serializer.
- `include/onebot11/bot/actions.hpp`, typed `operations.hpp`, endpoint implementations, and the installed `build/sdk-user-info/include/obcx/onebot11/bot/` do not expose a typed get-image/resolve-image operation through BotOperationGateway.
- A serializer alone does not prove that the deployed OneBot implementation can return a fresh publicly accessible URL. The actual OneBot implementation/version and response contract must be established.
- The changed requirements keep temporary URLs and image bytes out of the durable LLM journal, including traced requests and model-echoed values. Model continuation must receive real multimodal input; a tool response containing only a URL string is insufficient.
- Stable-cache acceptance now applies to the durable text/marker prefix, not full visual wire requests across turn boundaries. Temporary visual input is discarded at turn end, and later viewing resolves again.
- Scope approval has now been given for a minimal core/OneBot image interface and Bridge image-fetch migration. No deployment/restart or replacement of the initial frozen SDK is implied. Message-store and DbManager remain out of scope.

## Superseded provider assumption

The earlier Lagrange investigation was against the wrong provider and is withdrawn as an implementation dependency, blocker, or required user decision. No Lagrange/get_rkey application code was implemented. Scope approval for core image access and Bridge migration remains valid; task 11.1 was re-audited against OneBot 11 and LLOneBot below.

## Shared core image acquisition implemented (task 11.4)

- Added `include/network/image_fetch.hpp` and `src/network/image_fetch.cpp`, exported by `src/CMakeLists.txt`. `fetch_image` accepts explicit timeout, byte cap and optional prefix Range; uses core HttpClient with direct/no-proxy transport, no redirect following, no disk storage or retry policy. HTTP status and MIME interpretation remain caller policy.
- URL validation uses the existing core-private libcurl dependency. It rejects credentials, fragments, unsupported schemes, malformed ports/control characters and oversized URL input. HTTP port 443 is explicitly rejected because the existing HttpClient would otherwise silently upgrade that scheme. This API is a trusted-caller networking primitive, not DNS-pinned SSRF protection and not a model-facing URL tool.
- Over-limit/timeout/transport errors are classified with fixed safe diagnostics; core cancellation propagates as Asio operation_aborted. The configured body cap still applies when a server ignores Range.
- Bridge `src/qq/image_url_validator.cpp` delegates probes and full downloads to core; `src/qq/media_processor/image.cpp` delegates GIF prefix acquisition. Existing byte/timeout configuration is explicitly passed rather than introducing settings/defaults. Removed unused `MediaProcessor::download_media_file` template. Retry/batch/placeholder/MIME/normalization and existing typed Telegram fetching stay in Bridge.
- This completes only the HTTP-acquisition portion. At this checkpoint no stable QQ locator or typed get_image operation was implemented yet; task 11.5 remains open for native image resolution and 11.6–11.8 remain open for the complete final SDK generation/acceptance.

Verification performed:

- Isolated core-only workspace: `/home/vollate/Codes/OBCX/build/chat-llm-image-core`; explicit empty package roots, Debug, no actors. Core compilation passed with `--parallel 20`.
- Interim frozen SDK: `/home/vollate/Codes/OBCX/build/sdk-chat-llm-image-fetch`, created by `scripts/actor_build.py snapshot-sdk`; no existing snapshot overwritten.
- Actor-only workspace: `/home/vollate/Codes/OBCX/build/actors-chat-llm-image-fetch`, configured via `scripts/actor_build.py` for `vollate.bridge` and `faccoco.chat-llm`. Build passed with 20 workers, including the unchanged transitive message-store dependency. Helper verified imported core and frozen SDK integrity before/after.
- Logs are under `build/chat-llm-image-core/{configure,build,rebuild,snapshot,actor-configure,actor-build}.log`. Source formatting and root/Bridge `git diff --check` passed. OpenSpec strict validation passed.
- Existing repository inspection found no relevant image/HTTP test target; no new unit tests were written. Byte-limit/cancellation behavior above is implementation-reviewed and compiled, not yet exercised through a new integration run. No real QQ/LLM endpoint, deployment, restart or DB deletion occurred.
- message-store and chat_llm source working trees remain unchanged. At this checkpoint progress was 5/60 (1.1, 1.2, 1.4, 11.1, 11.4); task 11.1 was subsequently re-audited against the correct implementation below.

## Corrected LLOneBot / OneBot 11 contract (task 11.1)

- Official standard: `https://github.com/botuniverse/onebot-11/blob/master/api/public.md#get_image-获取图片`. get_image accepts the received image segment's file identifier; its standard file result is a server filesystem path, not a guaranteed public URL.
- Pinned LLOneBot source: `https://github.com/LLOneBot/LuckyLilliaBot/tree/dc2b2b90092ef0f571b5546878288b83fff1e436` (the LLOneBot repository redirects to this renamed repository). This is upstream evidence, not identification or verification of the deployed version.
- `src/onebot11/action/file/GetImage.ts` extends GetFileBase with action get_image.
- `src/onebot11/action/file/GetFile.ts` looks up file UUID then filename in its own platform cache. download:false skips uri2local and returns empty file plus optional url/file_size/file_name. Core must pass false explicitly, since LLOneBot's own default is true; no OBCX configuration default is introduced.
- `src/onebot11/transform/message/incoming.ts:79–102`: native image.data.file is picElement.fileName, distinct from url; LLOneBot caches UUID/name/md5/original URL internally. OBCX needs only the received native file identifier and installation, scoped by the actor attachment registry.
- `src/ntqqapi/api/file.ts:getImageUrl`: LLOneBot handles URL generation and key refresh internally for unsigned NT origin URLs. If the origin already contains rkey, it can return that value unchanged. A repeated get_image call is not a guarantee of a new usable signature. No expiry is returned by GetFileBase, so an explicit caller reuse bound remains necessary.
- No OBCX get_rkey operation, key selection, signature reconstruction, or retained source URL is needed. Missing platform cache / path-only results / inaccessible URLs must fail explicitly for URL-only consumers. The platform's cache retention remains outside OBCX control.
- Current planning and task 11.1 now refer to this audited contract. The assistant's prior question about ambiguous Lagrange keys is withdrawn as irrelevant.

## Typed get_image and Bridge adoption completed (tasks 11.2–11.3, 11.5–11.8)

- `include/onebot11/bot/image.hpp`: OneBotImageRef (installation + native file), trusted-segment extraction, GetOneBotImageRequest with mandatory download, OneBotImageInfo and OperationTraits result association. URL/path-shaped identifiers are rejected. Results preserve standard server paths versus optional restricted QQ CDN URLs; decimal sizes normalize with overflow/conflict checks. Base64 and unrelated provider fields are omitted.
- Added `onebot11.image.get` action, registry/client membership and endpoint execution. Reused the standard serializer, adding an explicit-download overload without changing the existing standard-only call. Image request diagnostics do not dump payloads. No key API or expiry synthesis was added.
- Bridge single images, media groups and forwarded-node images use the exact installation and `download:false`. Failures preserve per-item placeholder behavior and never read the returned server path or replay the source URL. Forwarded nodes retain native file identifiers. Existing cached Telegram sticker IDs still bypass new image retrieval; GIF format-cache keys remain tied to the original observation, not a freshly resolved URL.
- SDK exports include the image types; no new actor library dependency was necessary. Documentation: `packaging/actors/onebot-image-access.md` and Bridge README. No new configuration defaults.

Build and controlled integration evidence:

- Core and isolated application built in `build/chat-llm-image-core` with 20 workers; not the live build directory.
- New immutable SDK: `/home/vollate/Codes/OBCX/build/sdk-chat-llm-get-image`.
- Matching actor-only build: `/home/vollate/Codes/OBCX/build/actors-chat-llm-get-image`, built via actor_build.py with 20 workers. Bridge, chat_llm and the unchanged transitive message-store built successfully.
- Frozen integrity checks passed for sdk-user-info, the earlier sdk-chat-llm-image-fetch, and sdk-chat-llm-get-image. Imported-core identity was verified; no snapshot was overwritten.
- Reused the existing loopback runtime fixture for a real application → message-store → Bridge → typed gateway → HTTP adapter integration exercise. Fixture scripts/artifacts remain under ignored `build/chat-llm-image-core/`; no unit tests were written.
- **12 scenarios passed**, with a 20-worker scenario pool: valid URL-only lookup; standard path-only result; unsafe URL; overflowing size; conflicting metadata; missing cached file; unsupported get_image; invalid native ID; two-installation isolation with identical native group IDs; ignored-Range over-limit album replacement; forwarded-node image resolution; GIF prefix probing.
- Every observed get_image request explicitly used download:false and the native file, never the source URL. QQ CDN test traffic was redirected to a loopback fixture; no real QQ/CDN/LLM endpoint was used. The Range-ignoring fixture sent 2048 bytes against an explicit 1024-byte cap; both album slots were replaced without losing order. Forwarded images preserved native identifiers; GIF detection requested bytes=0-31.
- Integration log: `build/chat-llm-image-core/get-image-runtime-integration.log`; final isolated runtime artifacts: `build/chat-llm-image-core/run-maecs7mh`. All fixture processes shut down cleanly. Only their isolated state.sqlite3 files were created; no production DB or old LLM history DB was read/deleted.
- Build logs: `get-image-build.log`, `get-image-snapshot.log`, `get-image-actor-configure.log`, `get-image-actor-build.log`, `get-image-app-build.log` in the isolated core build directory. OpenSpec strict validation and root/Bridge diff whitespace checks passed.

Overall progress at that checkpoint: **11/60** (1.1, 1.2, 1.4 and all eight section-11 tasks). The main chat_llm durable-session/view_image/token-compaction work is still pending; task 1.3 remains open for tokenizer/image-accounting and provider capability selection. No live deployment/restart, commit, source message-store modification, or database deletion occurred.

## Planning amendment: provider-native reasoning lifecycle

The user requires reasoning/CoT isolation and replay regardless of encryption. The proposal, design, durable-context, token-compaction, vision-context, and tasks now treat plaintext, signed/redacted blocks, and opaque/encrypted continuation as session-owned protocol state rather than a Codex-only feature. Every request-construction path must restore the reasoning allowed/required by the verified endpoint contract; unsupported cross-user-turn replay must be explicit, not silently treated as supported. No new Claude transport is implied.

This supersedes the earlier blanket claim that all provider continuation can be scrubbed of transient image data: editable echoes are sanitized, inspectable protected echoes cause an explicit conflict, and opaque blocks are retained unchanged without claiming inspection of encrypted internals. All reasoning forms are excluded from ordinary diagnostics. Compaction retains complete recent protocol units and atomically retires old-prefix reasoning from active requests without turning it into summary text.

Five new unchecked tasks (1.5, 5.7, 5.8, 8.5, 10.9) add capability, replay, compatibility, compaction, and isolation acceptance work. Prior completed-task evidence is unchanged; these planning amendments are not implementation or endpoint-verification evidence.

## Approved validation-only boundary and isolated acceptance (tasks 2.5, 9.2)

The user explicitly chose configuration/profile-only ValidationOnly and keeping core unchanged. The existing core intentionally omits the runtime BlockingExecutor in this mode. No core workaround or actor-private worker pool was added.

- `chat_llm_actor.cpp` returns from ValidationOnly after explicit option/declared-binding checks, local profile/prompt/Codex-credential validation, fixed-body budgeting and command-availability publication. It does not obtain DbManager, construct repositories, read schema/messages/sessions, or inspect a live gateway.
- `actor_configuration.cpp::prepare_profile` is shared by offline validation and startup/reload. Startup/reload runs it and the read-only source/state schema probes on the existing core BlockingExecutor. Only explicit startup initializes actor-owned tables using core's existing migration lock. Candidate initialization and DB/namespace changes return restart-required without mutation.
- Startup precedes bot installation registration, so it cannot query a live gateway's supported actions during preparation. Static API compatibility comes from the frozen SDK; reload checks registered capabilities and actual image tools report unsupported actions explicitly.
- The no-DB-call guarantee is actor-scoped. Core main already opens configured DB instances before actor preparation, issuing connection PRAGMAs (and SQLite's internal schema reads) and potentially creating empty DB/WAL files. This existing behavior was observed, documented, and not changed.
- The real CLI exercise exposed a fixed-request accounting bug: JSON Schema allows an array-valued `type`, so recursively coercing every `type` field to string failed before activation. Accounting now compares JSON values without coercion. This does not alter image/protocol charges.
- Updated `config.example.toml` with explicit separate/same-instance choices, read-only source bindings, MessageStored routing, required reasoning/profile/token/media options and retired-history rejection. Local feature/provider documentation explains the validation boundary and Codex configuration. Other feature/reasoning acceptance work remains open.

Verification:

- Actor-only build passed with **20 workers** against unchanged frozen `build/sdk-chat-llm-get-image`; log: `build/chat-llm-durable/validation-only-build.log`. The helper verified SDK hashes and imported-core identity before/after. No core build, SDK replacement, deployment or live restart occurred.
- Adapted the existing `build/chat-llm-platform-gate/run.py` loopback CLI/runtime harness, using the already-built isolated application and new actor artifact. Additional SQL observation is confined to the ignored integration workspace; no unit tests were added.
- **28 real CLI validation cases passed**: absent/unsupported source and state schemas; separate/same-instance declarations; integer/decimal-k/K capacity; invalid/missing/fractional/overflow settings; unsupported profile/contracts; missing local resources; fixed-budget/request-byte failures; undeclared source binding; retired-history rejection; explicit zero reuse; local expired Codex credentials accepted without refresh/lock/write and insecure credential permissions rejected.
- SQL audit saw no actor SELECT/DDL/DML during validation. Only unchanged core connection PRAGMAs and libsqlite's internal schema loading were present. Fixture DB logical snapshots were unchanged (apart from core creating empty configured DBs); the unreadable legacy-history sentinel remained untouched. Loopback bot/provider fixtures received no validation traffic.
- **Four isolated startup cases passed**: absent source, unsupported source, unsupported state all rejected without repair or state initialization; valid source initialized only actor-owned state plus core's existing `obcx_migration_locks` bookkeeping, leaving source schema/data unchanged.
- The valid runtime then exercised **three reload cases** through its real CLI: compatible reload preserved both DB snapshots; binding change returned restart-required; a fixture-only removal of state tables caused restart-required without recreating them. All fixture runtimes stopped cleanly. These were isolated test processes, not restarts of the live service.
- Integration log: `build/chat-llm-durable/validation-runtime-integration.log`; final artifacts: `build/chat-llm-durable/run-1g_s58ry`. Runner: `build/chat-llm-durable/validation-runtime-integration.py`; SQL observer: `validation-sql-audit.c` in that directory.
- Core runtime/BlockingExecutor/DbManager files and the message-store repository are unchanged. Earlier explicitly approved core image changes remain intact. Whitespace and strict OpenSpec checks passed.

At this checkpoint tasks 2.5 and 9.2 were complete and progress was **13/65**. That lifecycle-only evidence did not yet complete turn cancellation/visual redaction or the broader session/reasoning/vision/compaction acceptance. No real provider cache/vision/reasoning capability was claimed from it.

## Completed durable actor and final acceptance

All remaining implementation and documentation tasks are now complete. The authoritative final evidence is [acceptance.md](acceptance.md), including one row for each of the 65 checklist items. Source, fixtures and results were re-audited rather than treating the earlier checkboxes or compilation as proof.

Implementation delivered:

- Strict explicit context/source/media configuration; SHA-pinned o200k tokenizer with ICU pre-tokenization, conservative protocol/opaque/signature/image charges, complete serialized-body budgets and optional usage telemetry.
- Core-managed SessionRepository and read-only SourceMessageReader; append-only epochs/journal, bounded high-water/source-reference observations, atomic admission/cursor/wakeup, exact scopes and full logical-identity deduplication. No private SQLite API or source migration/backfill.
- Typed command and MessageStored orchestration, durable busy admission, terminal tool pairing, group-wide think snapshots, exact self-echo association and conservative sending recovery. Confirmed receipts survive a later tool-result persistence failure; uncertain sends never automatically repeat.
- Four explicit reasoning contracts, ordered native units and ownership checks, compatible restart, incompatible epoch archive, complete-unit compaction and local missing/unsupported-state failures. No Claude transport, fabricated internal CoT or reasoning-as-answer conversion.
- Stable scoped image markers and lazy QQ/TG view_image, bounded actual multimodal inputs, historical re-resolution and cancellation disposal. QQ uses only typed get_image(download:false); TG bytes/data URLs are current-turn memory only.
- Atomic token-triggered summarization with complete recent/current units, trusted image metadata and bounded no-tools chunks; timeout, bad/oversized/forged summary and transaction failures preserve the original epoch.
- Privacy guards sanitize editable access echoes before execution/storage, reject protected content/signature/native-call-identity conflicts without rewriting them, and exclude all native reasoning/error bodies from ordinary trace. Opaque payloads remain opaque, never an image cache.
- Updated feature/provider/lifecycle/acceptance and manual migration/rollback documents. Old history.db is not imported, created or deleted. Source binding changes to a different dataset require a fresh explicit state namespace, not reuse of its old cursor.

Integration uncovered and fixed real issues in Telegram conversation keys/captions/UTF-16 mentions, command idempotency, atomic wakeups, retained-reference resolution, native-state ownership/recovery, JSON Schema accounting, credential-only echoes and confirmed receipt recovery. Final runs include these fixes: **56 session scenarios, 11 Codex TLS/SSE scenarios, 28 CLI validations, four startup and three reload scenarios**. The final Bridge artifact also passed all **12 core/Bridge scenarios**. **243 real serialized requests**, including 35 visual and 10 summary calls, matched upstream tiktoken 0.14.0 accounting. These are local fixture results, not real endpoint authorization, signature acceptance, billing or cache effectiveness.

Final actor-only configure command used an unused output directory (the helper intentionally refuses to reconfigure an existing one):

```sh
python3 scripts/actor_build.py configure \
  --sdk build/sdk-chat-llm-get-image \
  --workspace packages.toml \
  --cache build/chat-llm-image-core/package-state/sources \
  --output build/actors-chat-llm-final \
  --actors faccoco.chat-llm vollate.bridge \
  --mode development --configuration Debug --debug-trace ON
python3 scripts/actor_build.py build --output build/actors-chat-llm-final --jobs 20
```

Configure/build logs: `build/chat-llm-durable/final-configure.log` and `final-build-2.log`. Workspace-specific ICU/OpenSSL evidence is under ignored `.package-state/providers/`, selected by the ignored local packages.toml; only actual dependency declarations and portable instructions belong in the repositories. The unchanged SDK's public link interface still includes SQLite, so DT_NEEDED can include it even after removing the actor-private declaration. Final audit verifies no actor SQLite C API symbols; no core linkage change was made to hide that distinction.

The final integrity check verifies immutable old/new SDK hashes, imported core, exported public header equality, unchanged message-store/DbManager/lifecycle/executor/CLI, unchanged unit-test files and whitespace. Root treefmt and strict OpenSpec validation pass. There are no registered CTest tests in these workspaces; existing runtime fixtures were used instead, with 20-worker scenario pools and no new unit tests. No live deployment/restart, old-file deletion, commit or archive was performed.
