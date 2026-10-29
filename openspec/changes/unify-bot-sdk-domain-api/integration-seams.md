# Integration seams and owner gates

This is implementation coordination for the approved change, not deployment authority.
All lanes preserve the captured dirty snapshot/index; no commits, test creation,
live configuration/DB work or actor compilation against mutable core headers.

## Current reviewed evidence

- Final runtime source-identity pre-copy correction: reviewer `bdfa29b3`, OK with notes.
- Single-output effect helpers: writer `68e31b97`, reviewer `9b886343`, OK with notes;
  25 existing compile-only inputs passed. Header imported by verified preimages into
  runtime and both platforms; index contents were preserved.
- QQ local corrective delta: reviewer `d3878290`, OK with notes; three new
  archive/preflight/encoding regressions closed and borrowed-body consumption
  verified. Parent checked all 44 existing compile statuses zero. Overall QQ
  remains blocked on concrete media and final integration/runtime gates; forward
  outcome/budget corrections are now reviewed by884e37ec with47+47 verified
  compile statuses and six final hashes. Native origin is reviewed below.
- Telegram corrective delta and cached-sticker policy: reviewer `11f60e34`,
  OK with notes; six original findings closed. Origin/cursor gate remains open.
- Pure legacy compatibility facade, including transient redaction evidence:
  reviewer `8ee4f67a`, OK with notes; parent checked 15 compile statuses zero.
  Native QQ (`f67b7d56`,45 verified compile inputs) and TG (`c26689e8`,38)
  implementations are now source-reviewed. Actor fidelity remains open.
- Authoritative-origin disconnect correction: reviewer `e862b672`, OK with notes;
  queued-refresh/disconnect P1 closed. Parent verified15 zero compile statuses
  and imported the ten-file increment into core-runtime and both stopped native
  lanes. QQ consumption is now source-reviewed by `58a31d65`,47 final inputs
  verified. TG's six native-origin files and38 compile statuses are verified;
  shared cancellation P1 is corrected/reviewed by3d4f3da2 (six network TUs) and
  imported. Combined native consumption review142f2479 closes the original P1 at
  source level;44 TG inputs and unchanged six native-origin hashes are verified.
  Executed concurrency/shutdown acceptance remains open.
- Complete-reply budget (`a1c07b22`) and common-client consumption (`d54f0257`)
  are source-reviewed, with25+15 compile inputs each verified. Generic shared
  WebSocket pre-read factory (`44961055`) passed17 inputs; QQ consumes it without
  reconnecting or dropping account-transition send receipts. No runtime claim.
- Common client lazy-facade correction is reviewed by6f53b9be;25 SDK+17
  production/instantiation inputs passed. Both platform imports preserve indexes.
- Shared topic fact f59bb8b6/15 inputs and native QQ29cc1d73/47, TGfc56776f/38
  close the source topic boundary. Chat LLM's seven-file helper stage is accepted
  byba9d9b47 only as partial source, not a coherent/buildable actor. Workflow
 843b12d6's coherent history/live wiring is now accepted by5f8954f9, with11
  final hashes and both86/84-file snapshots verified. Send/media/tool operations
  and frozen-SDK builds remain later gates.
- Store source/metadata reviewd08e67f4 has no findings; five current hashes match.
  The exact tracked delta uses captured dirty baseline tree7349fc8c, not HEAD or
  current index. No actor build/runtime acceptance follows from source review.

Reports are under the parent session output directory; current closures include
ca0b2402,9a2c151b,62ccf6b8 andd736ddcd. Compilation/source review does not establish
runtime, provider, installed-SDK, actor or DB acceptance.

## Partitioned correction wave

This is multi-seam work. Each row is one contract owner; parent integrates only
reviewed handoffs. Workspace root is
`/home/vollate/Codes/OBCX-work/domain-api-20261026-224822`.

| Lane / isolated cwd | Exclusive contract and files | Gate / durable handoff | Independence |
| --- | --- | --- | --- |
| `core-runtime` | Reviewed completion/policy, parent-owned common-client reply budget/lifetime and shared WebSocket factory; reviewed HTTP correction imported | Exact reviewed increments;25+17 latest client inputs; no full-link/installed SDK claim | Shared process mechanisms, no native policy |
| `account-origin` | Origin/cache source remains frozen; narrow curl_asio_multi.cpp completion-slot/resource ownership correction plus handoff now reviewed | 3d4f3da2, six network production inputs; exact preimage/region hashes, imported into stopped consumers | Shared HTTP mechanism, no bot policy |
| `legacy-compat` | Public resolver/compatibility values, private platform binding delegation and runtime resolver; no provider/actor implementation | Existing production compile inputs, fresh review; exact API/header handoff | Pure legacy compatibility boundary |
| `sdk-contract` | Reference-based common send-body budget validation and nested request preflight in domain_messaging.hpp only | Existing 25 compile inputs, retained review; exact one-header delta | Shared input validation, not native recipes |
| `effect-budget` | Wrapper-aware shared-budget state in send_gateway_codec.hpp/validation.hpp and its process MessageOutcomeContext checkpoint propagation only | Existing SDK/runtime compile inputs, fresh review; exact API/delta | One full-envelope recovery contract, separate from body/origin/legacy files |
| `onebot-adapter` | Earlier forward/origin/topic gates retained; final POSIX-path source correction reviewed by a1bffe4c |73 foundation inputs v14; post-reader-import51/54 compile (23 native/network and3 shared production TUs pass, same three obsolete API inputs fail); runtime/full-link gates remain | QQ owning-platform boundary |
| `telegram-adapter` | Sticker/origin gates retained; partial path-boundary correction reviewed by56b038b2; HttpSource preparation reviewed by3e3bf8ff |71 foundation inputs v12; six-file LocalFile increment reviewed50e37f09 with47 compilation passes; runtime/full-link/SDK gates remain | TG cached-sticker outcome consumption |
| `chat-llm` | Reader/service/parser/live/history callsites coherently connected and source-reviewed; next send/member/media stage remains |5f8954f9,11 hashes verified;86/84-file snapshots unchanged, neither installed SDK | Actor-owned legacy identity/rendering policy |

The effect seam is now independently reviewed (`de36f7f2`, OK with notes) and its
15 existing compile statuses were verified zero. Its eight files were imported
with exact preimage/hash checks into both platforms and both new lanes. The new
lanes began with 61 verified foundation inputs in review-inputs/*.foundation.v2.json.
Telegram now uses foundation-import.v12.json with71 inputs; QQ uses v14 with73.
These include reviewed caller-owned final-path wording and reader/factory/assembler
integration; native LocalFile consumption is TG-owned, not a shared SDK policy.
Both include reviewed topic facts, common-client lifetime and shared HTTP cleanup;
QQ also includes the shared WebSocket factory. Latest imports occurred after
native writer/reviewer completion; the earlier WebSocket/client import used an
explicit no-compilation pause. Core-runtime has those reviewed seams too; inputs
are not replaced during active writing/review/compilation. Chat LLM reads the new
immutable86-file source snapshot.v2, not changing native worktrees; it cannot
compile against either this snapshot or the preserved84-file predecessor. Each import checked source/target preimages
and preserved current index bytes. The account-origin and legacy-compat lanes
separately observed physical index drift; all 390 staged mode/blob/stage entries
still matched the captured snapshot, so no index was restored and no cause was
asserted. Account-origin changes build on that accepted registry; they must not
alter the completed invocation/completion methods. Parent still integrates only exact
reviewed deltas. Full core integration and SDK freeze remain parent gates.

## Authoritative receive origins

- An origin token is process-private, owned by the existing SelfIdentityCache
  instance and its authoritative epoch. It is not an account-ID cache or authority
  supplied by actors, provider JSON, opaque public IDs or a readable snapshot.
- Capture before originating I/O or callback scheduling; carry it unchanged with
  the transport connection epoch. Re-reading the current epoch at publication is
  not an origin capture.
- Validate instance/generation/closed state and connection eligibility under the
  cache lock before observing self or capturing reply-evidence admission. The
  disconnect eligibility change must be synchronous with epoch invalidation;
  queued refresh work cannot capture a usable post-disconnect token through an
  older strand connection flag. Loader publication must also reject disconnect.
  Check and observation must be atomic.
- Expose an authoritative transition result for native getMe/login publication,
  including expected-ID conflicts without a readable snapshot. Stale refreshes
  must not mutate expected identity before being rejected.
- Native owners must retire stale I/O and prevent stale responses from mutating
  polling offsets or other transport state. A provider implementation must consume
  this seam before its account-origin finding can close.
- Preserve lock order and the existing evidence invalidation hook; no cache-locked
  callback may re-enter cache or acquire a transport lock that can enter cache.

## Explicit legacy compatibility facade

Approved pure conversions on ReferenceResolver:

```cpp
user_from_legacy(installation, std::string_view) -> BotOperationResult<UserRef>
user_to_legacy(const UserRef&) -> BotOperationResult<std::string>
message_reference_from_legacy(const ConversationRef&, std::string_view)
    -> BotOperationResult<MessageRef>
message_to_legacy(const MessageRef&) -> BotOperationResult<std::string>
compatibility_from_legacy(installation, const LegacyMessageArchive&)
    -> BotOperationResult<LegacyMessageCompatibility>
```

All are bounded, scoped, configured-installation-owned and free of I/O, reply
admission or new authorization. Native spelling/shape belongs to the registered
platform binding, never a generic-core provider switch.

The compatibility result supplies original source/display identities, original
segment and text boundaries, typed mention spelling/comparison facts, reply
placement and original image ordinals. SDK-owned opaque serializable fingerprint
values preserve the former image identity seed, complete locator fingerprint and
native-reference fingerprint. Their serialization reproduces the old JSON value;
actors may hash/store the whole value but must not inspect provider fields or use
it as a locator. An unavailable image retains its old null identity and has no
invented MediaRef. Real media access resolves a validated reference from the exact
source/ordinal and uses read_media under the actor's existing authorization.

Transient LegacyRedactionEvidence supplies the exact original image-secret
registration sequence to the existing actor scrubber: original segment order,
url before file, complete original http:/https:/base64: strings, including
unavailable images. It is not serialized, persisted or used as a locator. Apply
it before actor assembly/command substitution and retain the existing final
redaction before fingerprinting. Core/provider projection does not own redaction
markers or credential extraction. Native owners preflight the complete expanded
projection, including repeated text/fingerprint/redaction material, and validate
exact archive correspondence by reference through protected binding accessors.

Actor code continues to own img_ hashing, source fingerprints, prompt rendering,
self-relative labels, visibility, addressing, sidecars and durable keys. Preserve
source-native bot facts without adding common is_self or treating legacy Telegram
zero/synthetic sender IDs as real identity evidence. If such legacy bytes are
needed only to reproduce an old fingerprint, keep them explicitly separate from
genuine typed identity/authorization. The admitted live MessageReceived remains
the only source of provider-reply admission; restoration cannot renew it.

Archive aggregate bounds and source/time checks must precede extraction/copying.
QQ preserves both structured and currently supported CQ-string archives through
one provider-owned grammar/traversal; normalized segment ordinals and unescape
semantics cannot drift between preflight, construction and validation.
Recorded native kind can resolve an ambiguous old negative Telegram chat key;
configuration-only ambiguity must not silently become a different conversation.
Malformed historical usernames or IDs outside native validity may make the
compatibility projection explicitly unavailable, never silently rekeyed or
substituted. Valid sender_chat records remain supported: compatibility from IDs
may remain display/fingerprint bytes, but are not genuine sender/reply-author
UserRefs. Preserve the old opaque reply fingerprint separately from typed author
evidence. Actor code must not decode it to restore false identity authority.
No old key is rewritten as an obcx1 encoding.

### Actor-integration topic gap — source boundary reviewed

Store v4 rows lack separate topic-observation columns; Chat LLM's old command
parser only selected session topic when is_topic_message was true. The earlier
native reviews covered explicit caller topic evidence, not this integration gap.
Actor parsing/fabricated observed absence is not an approved workaround.

The pure-facade correction adds an explicit historical `topic_in_legacy_scope`
fact, separate from existing native topic_id/topic_observed and typed routing.
True requires an observed native topic; false may coexist with a native thread.
This preserves thread1-versus-absent legacy Scope bytes and the false/absent flag
case without changing ConversationRef. QQ/TG native followups must prove it,
charge it/derived strings before copies, and exactly validate it. Unobserved
caller hints may be resolved only at a proven full archive boundary; config-only
resolution and contradictory explicit hints remain strict. Arbitrary incomplete
objects do not establish observed main. No new configuration default or I/O.

Workflow9a2c151b's three-file shared correction is reviewed byf59bb8b6.
Workflow62ccf6b8's native QQ/TG corrections are reviewed by29cc1d73/fc56776f;
parent verified47/38 compile statuses and current hashes. QQ proves observed
absence, TG derives only after the complete canonical overlay proof and exactly
checks explicit contradictions, actual thread and historical selector.

The seven-file Chat helper remains only a scoped partial handoff. A NEW86-file
source snapshot.v2 now freezes these contracts; old84-file snapshot is unchanged.
Workflow843b12d6's coherent history/live callers passed5f8954f9 source review.
Unavailable rows fail before their page admission/cursor commit; earlier successful
pages retain predecessor behavior, not a new all-pages transaction. Live input uses actual
typed received_at, history uses recorded created_at; neither uses a clock fallback.

CommandInvocation.source_message_id is the source envelope ID, NOT necessarily
the native message key. Current coordinator sets source_event to that envelope's
payload: typed MessageReceived is under message and canonical archive under
payload. Preserve the old native command key via message_to_legacy(actual typed
source.message), not envelope-ID parsing. Match restored compatibility to the
actual typed source before live reply lookup. An unavailable historical row must
fail boundedly before admission/cursor/observation mutation; a known unavailable
local reply row remains Unknown without receipt/provider fallthrough.

## Bounded extension fallback

The process typed result policy must retain a bounded target-scoped primary-effect
snapshot before callbacks can throw. Its final make_failure contract must encode
all supplied primary evidence. Full fallback wrapper overhead must be budgeted
before recovery, within the existing shared budget; no arbitrary smaller limit or
silent removal of already verified caller effects is approved. An implementation
that cannot satisfy this is an integration blocker. A callback programming error
must not erase the process observer's already captured evidence.

QQ provider-specific node provenance may be sparse: absence means unavailable.
Physical references already live in SendOutcome; empty optional provenance must
not require duplicating every reference with null node indices. Supplied records
must be unique, scoped and refer to confirmed outputs, with valid node ranges.
This changes validation, not field/variant layout. Ordinary malformed metadata is
repaired selectively; it is not a reason to erase unrelated forward/media facts.
The originally missing wrapper-overhead seam is now implemented and reviewed.
SendOutcomeEnvelope::for_reply wraps the actual codec-derived result skeleton;
envelope-aware normalization/decoding and cumulative optional-metadata budget
propagate the actual node/byte/depth reservation into recovery and process
checkpoint BEFORE inspection. The actual process envelope includes the outer successful
OperationReply ({ok:true,value:<Result>}), not only the typed Result. Its actual
structure/depth is reserved before capture, without an unbounded fake-root copy.
Existing two-argument standalone SDK codecs keep their exact standalone limits;
process admission accounts for its real enclosing wire structure. Any additional
provider-client consumption remains an explicit integration gate; the two common
BotClient recovery sites are already reviewed against the same reply allowance. Optional
metadata consumes the remaining full-result budget after required primary/fallback
structure; this must not become a second unbounded scan, arbitrary smaller cap or
notification-only retention of evidence silently removed from the caller.
QQ's consumption is source-reviewed by884e37ec after the facade-lifetime and
rejected-key accounting fixes. Rejected/duplicate provenance inspection shares
the cumulative byte allowance before semantic rejection; exhaustion retains
primary effects and yields unknown/nonretryable failure. Both public wrappers
capture the gateway synchronously into an owned static coroutine; the gateway
must outlive execution, not the facade. TG's equivalent client/policy/initial
allowance consumption passed142f2479: direct Standalone SendOutcome under the real
reply shell, static coroutine, one SendEvidenceRecovery native checkpoint before
optional media, and unchanged two-argument standalone helpers. QQ's SDK2 forward Client has
two explicit execute overloads returning the typed
OneBotGroupForwardMessageResult directly; failures live in its SendOutcome, not
an outer C++ BotOperationResult. Queries retain their existing outer result type;
wire/result field layout is unchanged. This prevents outer success from hiding a
logical SendFailure and requires ExHentai/Bridge to inspect physical outcomes.

## Caller-owned final paths — superseding boundary

The latest user confirmation supersedes the candidate's host-only LocalFile,
MediaSourcePolicy authorization installer and shared-file registration gates.
Callers retain their external obcx.path-mapping use, existing configuration,
mounts, accessibility, business authorization, leases and cleanup. SDK/core accepts
the final path string and owns native encoding or supported upload mechanics,
not deployment mapping or a replacement registry. Provider-only paths do not
require OBCX-host stat; unknown sizes are not zero or verified byte counts.
Actual SDK reads/allocations remain bounded, with existing deadlines, credential
isolation, scope validation and truthful confirmed/possible effects preserved.
Planning/common correction passed focused review94a5c605 after residual wording
fixes. The common header's shape/codecs are unchanged; its wording/diagnostic
correction passed25 existing compile inputs (/tmp/obcx-path-boundary-sdk-check).
QQ's final absolute-POSIX-path support and removal of the policy/registration APIs
passed a1bffe4c review. TG's honest unsupported-source boundary passed56b038b2;
HttpSource acquisition from be418527 passed3e3bf8ff source review and44 existing
compilation inputs, with all67 foundation hashes unchanged at that stage. The
subsequent owned path-reader seam is acquisition, not authorization. Its3090ee39 helper passed7
production compilation inputs, but ab1f2748 found an initiation-exception
completion P1, closed by3f17bc6b after e0dd15f5's focused fix and7 production
compiles. Parent verified/imported the two files into stopped core-runtime;
process/factory wiring9b1ed9a6 passed f827de67 review. Parent verified all8
pre/postimages,20 successful core/helper compilation inputs and unchanged index;
2 stale core-worktree native recipes still lack the already-required domain
options, so the22-input pool was not wholly passing. Six shared reader/factory/
assembler files were imported at stopped native boundaries, preserving indexes:
QQ foundation v14/73 inputs, TG v12/71. Workflow59e71b87 completed six-file TG
LocalFile injection/preparation, reviewed50e37f09 OK with notes and47/47 compiled
inputs. Parent verified six pre/postimages,246 source hashes,47 objects/statuses,
71 protected inputs and unchanged index. QQ independently compiled51/54; its
three obsolete-input failure logs match the prior run byte-for-byte, and parent
verified54 input/status hashes,51 objects,73 foundation inputs and index. These
are scoped source/compilation gates only. Private reader source is registered
once in CMake and not SDK-exported;
export-preflight.v4 verifies the same61 public-header inputs remain unchanged. Blocking kernel I/O
cannot be forcibly interrupted: accepted work must drain, so logical deadline
checks prohibit further sends without promising hard wall-clock completion. Post-import compilation in
/tmp/obcx-final-path-postimport-check passed44/44 TG and48/51 QQ inputs; the three
QQ failures are unchanged fixtures naming removed APIs, not production failures.
New media_path.hpp replaces shared_media.hpp in exports;61-header direct closure
passes in package-contract.export-preflight.v3.json. No runtime/provider/link or
installed-SDK acceptance is implied.

## Integration evidence and remaining gates

Reviewed native acquisition/media recipes, including TG LocalFile, are now
integrated into an isolated full core/SDK build. Inventory371ac0e9 is complete; parent
resolved four historical TG evidence pointers through the original reviewed
inventory and corrective-stage hashes (telegram-adapter.integration-lineage.md).
Parent assembled439 exact files in integration-01/source, verified captured dirty
baseline/source/destination hashes and modes, retained two removed headers'
absence and preserved all five source indexes. Source assembly is read-only.
The core-only folder is a manifest/resolution directory, not a source worktree.
Workflow08dfb2df completed core/app configuration and full link:156 Ninja steps,
74 production TUs, core DSO/app/generic archive. Parent reverified439 source
hashes/modes, coverage/statuses and all3 artifact hashes. That generation was not
installed/frozen and no actor build/application execution occurred. Inherited
NIX_LDFLAGS/out put the main workspace outputs/out/lib in RUNPATH, requiring the
clean rebuild below rather than patching old evidence or live binaries.
Public ReferenceResolver is pure-virtual with inline destruction, not a missing
concrete SDK symbol. USER CLARIFICATION supersedes the proposed standalone SDK
requirement: the SDK includes its matching core. Definitions of11 public helpers
in core are expected, not a reason to extract platform support libraries or inline
implementations. No platform plugins or separate core-free SDK product are wanted.
Public/platform targets must propagate the core dependency automatically. Parent
therefore adds existing obcx_core to bot_common_sdk's INTERFACE link list; platform
targets retain their common dependency. Header privacy/API boundaries remain,
but runtime binary exclusion is not an acceptance requirement. No helper extraction
was implemented. Review d24e995d approved this exact one-file CMake increment.
Integration-02 clean rebuild b21463b1/5c5ec09b passed156 steps and74 production
TUs under flock/P20. Parent verified75 evidence files,499 build outputs,439 source
hashes/modes and the actual ELF: core has only Nix RUNPATH entries. The unexecuted
app retains a CMake-padding empty RUNPATH component; runtime-path acceptance is open.

Complete SDK2 is now frozen at integration-02/install via actor_build.py snapshot-sdk:
89 inventoried installed files read-only; core SHA256
7eb7be8a38761ab526308c4a6b8ec0e30121841d2af682d08b36843f48d53bb7 matches build;
manifest SHA25625e8cfb5af8d31f246e460940dccc99c13246216b2d71f5656de14cabfd46609.
Parent inspected installed exports/common->core/platforms->common, all11 public
platform function symbols, private-header absence and unchanged core ELF paths.
This proves packaging/static linkage evidence, not execution of those functions.
Store/template actor_build.py build cbdd2892 passed9 steps in sdk2-store-template-build;
only their two production TUs compiled, core imported from the frozen SDK. Parent
verified2573 build-file hashes, both DSOs, unchanged SDK/core/source/indexes and actual
ELF records (sdk2-store-template-build.accepted.json). Both actor DSOs retain an empty
RUNPATH component; runtime-path acceptance remains OPEN. No loading, actor code
changes or deployment. Task16.1 is closed for isolated core/SDK build/checks only;
At that stage11/75 tasks were accepted, not runtime acceptance. Never invent probes or rewrite fixtures.

Remaining: full actor send/member/media migrations, the isolated actor/probe builds,
TG filesystem/cancellation/origin/effects execution, historical fidelity, and existing
offline/runtime/DB acceptance where authorized. Empty build-ELF RUNPATH components
remain a separate runtime-path gate, not silently waived by CMake-padding provenance.
No checkbox is closed merely because an owner returned a partial candidate.

### Stopped actor candidates and standard-first ordering

Workflow5e3fe2e5 finished with partial Chat/ExHentai candidates, not complete actors.
Parent verified their exact deltas and preserved indexes. Relative workspace source
paths corrected the parent's initial configure inputs; retry configure succeeded,
but both real P20 builds stopped at deliberately unmigrated legacy SDK APIs before
changed bodies compiled. Chat's send/member/confirmed-receipt delta passed source
review c8e7b8d5. ExHentai review ea6a8077 blocked the new ambiguous Telegram chat:
prefix; the parent changed configured group targets to group:, and32a3e1f1 closed
that P1 by source reasoning. No new tests were written. Both source acceptance
receipts explicitly keep compiler/runtime/full-actor gates open.

The user's latest order is binding: complete OneBot11 standard first, pause ALL
nonstandard additions/changes, preserve existing extension code/behavior, and ask
about nonstandard handling only after standard completion. Do not ask for a
provider brand or add selection configuration. Provider-self author omission and
file-URL/file-message extensions are deferred; no fake author/cache workaround.
The compatibility planner's retained-media API/obcx2 codec and provider-self APIs
are unimplemented proposals, not approved amendments to frozen SDK2.

Official standard inventory a21f133e pinned botuniverse/onebot-11 commit
d4456ee706f9ada9c2dfde56a2bcfc69752600e4; source copies, exact URLs and hashes live
under review-inputs/onebot11-standard-reference in the isolated workspace. It
lists the complete segment inventory,38 public APIs, hidden/suffix forms and all event families,
with the parent correcting its distinct-type count from21 to20 against actual
normative top-level examples (onebot11-standard-segment-count.json),
separately identifying native serializers, runtime handlers and public typed APIs.
Registered forward lookup is not yet normative-response complete: code expects
plural data.messages/flat nodes; standard requires data.message/node.data. The
bounded writer2f196584 corrected that standard decoding; reviewer2ceb154d and
parent429-file/54-record verification accepted the scoped delta, preserving the
existing provider-shaped branch. Ordinary Voice/Video source writer7da32cfe and
reviewercc117768 subsequently passed the same scoped source/compile gate: Bytes,
final LocalFile and bounded core HttpSource, without Voice/Video reference support
or a universal codec claim. Both pools remain51/54 with26 production passes and
three unchanged obsolete failures. Neither change is in frozen SDK2. Ordinary Voice/Video sources are specified by
OneBot11 and do not need a provider choice. Received media URLs are standard but
currently discarded; get_image/get_record only promise provider-local paths, not
OBCX-readable files or original-byte HTTP acquisition. Evidence-preserving read
work must respect that distinction and existing no-unchecked-fallback rules.

The user explicitly approved FULL standard administration, credentials, service
management and dedicated notice/request coverage. The former scope question is
resolved; proposal/design/new onebot11-standard-sdk spec and9 added task17 items
record the expansion (current12/84 accepted, including the standard inventory).
The requested six-way workflow09dc245d completed one implementation and five
contract plans; plans still require parent synthesis and isolated writer ownership.
Parent design decisions for explicit delegated content, one-coordinator standard
login queries and opaque unscoped selectors/detached results are recorded in
review-inputs/qq-standard-parent-decisions.md. Keep common cache-only and reply
contracts unchanged; do not silently turn a selector into source/admission proof.
Do not claim all-standard completion, equate a generic serializer with complete
support, or introduce unsafe arbitrary action/params APIs. Nonstandard work stays
deferred; actual management/provider calls and deployment remain unauthorized.
