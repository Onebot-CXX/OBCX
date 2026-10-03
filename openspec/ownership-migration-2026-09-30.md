# OpenSpec ownership migration — 2026-09-30

## Scope

User-approved documentation-only split of current main specifications, active changes and two historical archives. Core retains SDK/ABI, command eligibility/ACL/propagation, generation lifecycle, bot protocols, HTTP/network, persistence infrastructure, package/build, identity/logging and TUI contracts. Actor names used as SDK examples or integration evidence do not by themselves make a specification actor-owned.

Business rules move to independent repositories; core does not track those repositories' contents or add submodules. No code, live configuration, database, deployment, commit or staging operation accompanies this move. Existing task states remain unchanged, and historical tests are not claimed as newly run.

## Current specifications

- [Bridge scope projection, forwarding and cross-actor acceptance](../local_actor/obcx-message-bridge/openspec/specs/bridge-command-routing/spec.md)
- [ExHentai destination matching and management policy](../local_actor/obcx-exhentai-fetch/openspec/specs/exhentai-command-authorization/spec.md)
- [Bridge request-local media HTTP caller lifetime](../local_actor/obcx-message-bridge/openspec/specs/bridge-http-client-lifetime/spec.md)

Core `actor-command-availability` retains five generic requirements. Core `actor-command-routing` retains generic unmatched-command propagation and terminal-effect coverage, with Bridge-specific cases moved rather than dropped. Core `http-curl-asio-transport` retains generic executor ownership and drain semantics.

## Active changes and archives

- `support-bridge-multi-installation-pairs`: core archive subsequently deleted at the user's request; current SDK requirements remain in [actor-abi-v2](specs/actor-abi-v2/spec.md).
- [declarative-package-dependencies](changes/declarative-package-dependencies/migration.md)
- [archive/2026-09-27-share-actor-command-availability](changes/archive/2026-09-27-share-actor-command-availability/migration.md)
- [archive/2026-09-17-fix-http-executor-shutdown-lifetime](changes/archive/2026-09-17-fix-http-executor-shutdown-lifetime/migration.md)

The mapper delta remains active in its library, including the pending ExHentai gallery handoff. No active change was archived. The multi-installation change had no delta spec files before migration; this pre-existing gap remains explicit in both ownership slices.

## Original wording and historical evidence

Before editing, all 31 source documents were snapshotted. Permanent byte-identical originals are partitioned among these history directories; they are evidence only, not active requirements:

- [obcx-message-bridge original records and SHA-256 manifest](../local_actor/obcx-message-bridge/openspec/history/2026-09-30-core-ownership/README.md)
- [obcx-exhentai-fetch original records and SHA-256 manifest](../local_actor/obcx-exhentai-fetch/openspec/history/2026-09-30-core-ownership/README.md)
- [obcx-path-mapping original records and SHA-256 manifest](../local_library/obcx-path-mapping/openspec/history/2026-09-30-core-ownership/README.md)

An additional local safety snapshot, including all four Git index baselines and pre-existing diffs, is `/tmp/obcx-spec-ownership-2026-09-30`. Cross-repository links use the current development-workspace layout and require those checkouts; repository-relative destination paths remain recorded in each manifest.

## Subsequent active-change cleanup

After the ownership move, the user approved core archival of `remove-package-lock`, `support-bridge-multi-installation-pairs`, and `complete-bot-component-migration` on 2026-09-30. Their destinations are `changes/archive/2026-09-30-<name>/`. The package-lock successor's four requirements were synchronized into main `direct-package-configuration`; the other two archives explicitly retain their missing-delta warnings. This supersedes the earlier active-state statements above, not their historical validation results. The separate Bridge change remains active.

Core now retains only the unfinished `declarative-package-dependencies` and `decouple-bot-platform-contracts` changes. No unfinished task was marked complete by cleanup.

## Subsequent archive deletion

The user explicitly requested deletion of the entire core archives `2026-09-30-support-bridge-multi-installation-pairs` and `2026-09-30-complete-bot-component-migration`. Those directories are no longer retained. Main specifications, other archives, active changes and owner-local original snapshots remain untouched by that deletion.

## Migration verification (before subsequent archival)

- Strict validation: all 21 core main specs and all three new actor main specs pass. Core and library `declarative-package-dependencies` slices pass. All five affected archive slices pass when validated as changes in a disposable validation workspace; the real archives were not reactivated.
- The initial core-wide run had three failures: `complete-bot-component-migration`, `decouple-bot-platform-contracts`, and `support-bridge-multi-installation-pairs`, all missing delta specs. The final core-wide run retains the same three failures. The newly separated Bridge multi-installation slice inherits that missing-delta gap and is explicitly not archive-ready. This move does not invent those missing artifacts or claim global validation is clean.
- All 31 permanent originals match their pre-edit SHA-256 hashes. Every original task line, including its checkbox state, exists exactly once across its current ownership slices. Moved requirement bodies and original joint availability acceptance records match their sources.
- Four Git index files are byte-identical to their baselines. All pre-existing files outside OpenSpec are unchanged; Markdown destination links resolve, and all four repository whitespace checks pass.
- No compilation or runtime test was rerun for this documentation-only change. Validation used 20 workers. Detailed evidence: `/tmp/obcx-spec-ownership-{core,obcx-message-bridge,obcx-exhentai-fetch,obcx-path-mapping,archives}-validation.json` and `/tmp/obcx-spec-ownership-integrity.log`.
