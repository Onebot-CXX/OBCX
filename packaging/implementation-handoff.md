# Implementation and OpenSpec cleanup handoff — 2026-11-02

## Scope and authority

The operator selected: sync root-owned core changes only; retain implemented
behavior in main specs; delete **all** root archived/unarchived changes after
sync; preserve unfinished/release/acceptance work here. Independent actor/library
OpenSpec roots are not part of that deletion. Three Chat-only root changes are
excluded from main-spec synchronization, but included in the expressly requested
root change-directory cleanup. No commit, deployment, restart, provider operation
or assistant-run database migration is authorized/performed.

## Legacy removal source completion

The legacy-removal checklist reached 19/19 for source/docs/products/scans.
SDK/core and all six maintained source minimums are 2.7.0. Generic reference
conversion, its five methods/bindings/service registration/exports, old messaging
DTOs/facades, archive compatibility/decoders, historical media imports and old
Bridge owner APIs are removed. Native protocol carriers and transient current
normalization remain private implementation, not historical restoration.

Current consumers use scoped references, named typed context/observations and
explicit configured destinations. QQ profile/member native selectors are scoped
query intent; only validated matching provider responses supply typed user facts.
QQ/TG current UserRef display views expose no reverse import or authority grant.
Chat/Store/Bridge reads no longer rebuild native event archives. No user persona
prompt or retired configuration alias was restored.

Store schema 5 and MessageStored wire 2 contain typed snapshots only. Chat session
schema 2 rejects older state. Bridge schema 5 removes scalar member/publication/
route projections; policy/EditIntent codecs require generation 2. It preserves
actual topic 1 versus absent topic separately from canonical scope, plus full
receipts, attempts, retry bodies, provenance, revisions and command fences.
The persisted legacy_unknown column is a rejection sentinel, not a supported
historical-admission API. Retired tables remain untouched.

The operator requested the standard-library offline utility
scripts/migrate_typed_databases.py. It accepts already-current Store/Chat state,
converts current typed Bridge metadata on backed-up copies, verifies canonical
binding/counts/integrity and unchanged effect/fence columns, then journals
replacement. It never promotes unsupported historical rows or invokes providers.
The assistant syntax checked it only and did **not execute it**. User-reported
execution/startup logs do not establish a complete migration/recovery acceptance
run. See actors/domain-sdk-cutover.md for explicit CLI and rollback boundaries.

## Final build evidence

Final core/SDK and actual six-consumer artifacts are outside the root build tree:

`/home/vollate/.cache/obcx/legacy-final-20261102T133326/`

- core/src/libobcx_core.so and core/src/app/obcx: 214-step isolated build passed.
- sdk/: frozen installed SDK; actor helper verified immutable integrity.
- actors-all/cmake/actors/: Bridge, Store, Chat, ExHentai, template and QQ probe.
- core-build.log / core-build.exit: exit 0; six workers under high system load.
- actors-build.log / actors-build.exit: exit 0; 132 steps, 20 workers.
- chat-compaction-build.log: subsequent 17-step actual Chat rebuild passed.
- configure-core.log, snapshot.log, configure-actors.log preserve context.

The first in-repository build also passed core and all six actors, but its
build/legacy-final-20261102T131706 directory was removed externally during an
independent root build. Do not rely on that path or its deleted checksum files.
The final core used explicit empty SDK-only roots outside the repository;
a preliminary full-workspace configure there hit an existing provider-member
provenance audit error. No provider check was weakened. Actual six-consumer
installed-SDK configuration/audits/build then passed outside the repository.

Production source and installed/export symbol scans found no removed resolver,
archive-compatibility or historical-media contracts. Both registry metadata
copies/indexes match maintained manifests and canonical checks pass. These
are source/build facts, not loader/provider/media/recovery/runtime acceptance.
No new unit tests or validation programs were written.

## Separate Chat compaction correction

User logs showed compaction rejecting summary-plus-tail and then masking the
cause with an over-capacity uncompressed fallback. The fix preflights the actual
mandatory tail/summary framing before summary I/O, derives the effective summary
text budget from remaining target capacity and passes that numeric budget as
summary input data. Fixed system/persona/tools are unchanged. Post-summary full
serialized request checks and epoch/fence atomicity remain. Required recent turns
are never silently dropped. Original context is used only if its hard budget
fits; otherwise the detailed compaction cause and text/protocol-opaque costs
survive instead of a generic replacement error. This cannot make an indivisible
mandatory tail fit an incompatible configuration.

A separate Chat+Store candidate built against the **operator's current on-disk
core**, without rebuilding it:

`/home/vollate/.cache/obcx/chat-compaction-20261102T140916/`

- actors/cmake/actors/chat_llm.so is the matching-current-core candidate.
- sdk/ is its frozen SDK; build.log records successful 20-worker construction.
- runtime-before.sha256 verified existing core and build/actors/chat_llm.so
  unchanged during this build. It is not proof of unrelated intervals.
- No candidate was copied into the runtime, and no reload or provider call ran.

## Work deliberately not imported as implemented requirements

### Declarative package/release tooling

Current v2 metadata, explicit source/provider graphs, graph/link/ownership audits,
workspace and installed-SDK build helpers, and kind-aware local registry exist.
The later remove-package-lock decision supersedes all packages.lock/frozen-lock
requirements; generated graphs are disposable and do not select sources.
Do not revive the old source-lock workflow from deleted proposals.

Still deferred: final portable build receipts/source/install inventories,
complete static/header-only provenance acceptance, inventory-driven release
packaging, complete published private DSO closures and developer packages,
remote tooling/source pins and verified binary assets, isolated clean-prefix
release/load/reload/rollback acceptance, and the complete negative matrix.
An audit is not a malicious CMake/custom-command sandbox. Older checklist
checkboxes did not distinguish local implemented registry/SDK work from these
release gates; no acceptance is inferred from those counts.

### Domain SDK runtime acceptance

Real provider/loader/ACL/notification/cancellation/lease/media/recovery acceptance
and dedicated complete standard-SDK consumer coverage remain operator-owned and
unexecuted by this assistant. Store/Chat fresh-namespace preparation-order
acceptance remains separate from upgrading existing databases. Reply-evidence
aggregate-retention design remains unresolved; no unapproved TTL/capacity option
or default was added. QQ common ordinary Audio/File and Telegram WebM/TGS limits
remain explicit.

Former legacy-decoder, v3 import/quarantine and reproducible old adaptation-bundle
acceptance items are **withdrawn**, not pending work to resurrect. Current-only
source intentionally rejects those contracts. Prior schema-2/modular and source
lock notes are superseded by input schema 3, SDK 2.7 and direct current declarations.

### Actor-owned scope

The three excluded root changes were auto-detect-chat-llm-addressing,
remove-chat-llm-wake-gate and rework-chat-llm-context-and-vision. Their Chat-specific
requirements were not imported into core main specs. Actor/library business
specifications and any remaining independent changes stay under their owning
repositories; no nested OpenSpec tree was deleted or auto-archived here.
