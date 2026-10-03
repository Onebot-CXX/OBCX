## Context

Override arrays currently carry a `command` string and nested group/user policies. The snapshot converts them to `CommandAccessOverrideConfig` records; routing compiles these records into a canonical-name map. Only the TOML representation needs changing.

## Goals / Non-Goals

**Goals:** Make the command visible in every policy heading; validate keyed policies strictly; retain existing access and help behavior.

**Non-Goals:** YAML support, actor-keyed authorization, changed allowlists, new policy defaults, or HTTP shutdown fixes.

## Decisions

- Parse `command_runtime.access.overrides` as a table of command-name tables. Derive the internal record's command from its key; retain the existing internal vector/API to minimize runtime changes.
- Accept only `groups` and `users` in each command table. Keep both dimensions, their modes, and entries explicit. TOML parsing enforces duplicate key/table rejection; snapshot validation enforces canonical names, shape, and count limits; routing retains active-command checks.
- Reject the old array form with an error naming the new keyed syntax. Supporting both forms would perpetuate confusing documentation and complicate validation; do not silently ignore legacy policies.
- Keep the development configuration's permissions unchanged while rewriting its headings. Actor targets and command permission changes are separate concerns.

## Risks / Trade-offs

- Existing external configs require migration → actionable errors and documented before/after instructions.
- A keyed-table parse failure could accidentally fall back to global access → validation regressions cover arrays, scalar entries, missing dimensions, unknown fields, and invalid names.
- Existing worktree changes overlap parser/tests/docs → use targeted edits and leave unrelated changes intact.

## Migration Plan

Replace each `[[command_runtime.access.overrides]]` plus `command = "name"` with `[command_runtime.access.overrides.name]`, or include `.name` in the nested group/user headings. Remove the redundant command field. Validate configuration, then reload or restart with the new binary. Rollback requires restoring the old config syntax alongside the old binary.
