# Current-only runtime and persistence

OBCX no longer ships the retired synchronous HTTP API, WebSocket-specific public
connection entry point, unused proxy tunnel, Bridge scalar-pair adapter, actor
schema upgrade chains, or pre-package-v2 release/snapshot replay tools. Build the
host and all actors together; mixing previously built SDK consumers with changed
C++ interfaces is unsupported.

## Supported schemas

| Actor | Existing schema required | Empty namespace/database |
| --- | --- | --- |
| ExHentai | v8, with its exact namespace identity | Create v8 directly |
| Bridge | v3 | Create v3 directly |
| Message Store | v4 | Create v4 directly |
| Chat LLM | v3 (`messages_v2` remains the current table name) | Create v3 directly |

Older/newer versions and partial or unversioned actor state are rejected, not
upgraded, relabeled, cleared or adopted. Namespace initialization still uses the
process transaction/locking service. Message Store's ambiguous-namespace refusal
remains a data-loss safeguard, not an alternate reader or upgrade path.

Existing ExHentai v8 tables/columns, delivery history and deduplication identities
are retained. Current API rounds, independent tag/absence observations, baseline
suppression, revision/sequence guards and fresh pre-send checks remain active.
Both direct and forward-batch submissions require qualified evidence; there is
no direct scan-to-delivery or unvalidated claim interface.

Bridge uses explicit `installation_pairs` and an explicit `pair` on every group
or topic route, including single-pair deployments. Preserve existing pair IDs
when converting configuration. The local runtime configuration retains `legacy`
as an explicitly configured identity; the code has no fallback with that name.
Core configuration contracts no longer select between scalar and collection
alternatives on behalf of Bridge.

Normal retries, media fallback, upstream pagination markers, protocol-permitted
string messages, lifecycle safeguards and historical statistics readers are not
retired-system adapters and remain supported. Git/OpenSpec history and historical
design records are retained; shipped offline bundles and patch replay inputs are
removed.

## Verification and deployment boundary

Compatibility removal was checked with:

- `cmake --preset actor-dev -B build` and the full workspace build with 20 workers.
- Separate compilation of Chat LLM's changed message repository translation unit
  using the workspace SDK/compiler flags; Chat LLM is not enabled in this profile.
- Actual `obcx --validate-config` runs for the Bridge example, ExHentai example
  and converted local runtime TOML, each in an isolated temporary directory with
  every database path replaced by an isolated path.
- TOML comparison confirming the local conversion changes only the Bridge pair
  representation and the nine route references, retaining every other value.

No unit tests were added or run. Compilation/configuration validation does not
prove SQLite initialization or live forwarding. No production database write,
service startup/reload, resend or deployment was performed. Database history was
not reset to make the new configuration pass. A separately authorized deployment
still requires a consistent shared-database backup and matching artifacts; an old
schema error cannot be fixed by restarting this binary. Reconcile sent-message
outcomes before restoring older database backups.
