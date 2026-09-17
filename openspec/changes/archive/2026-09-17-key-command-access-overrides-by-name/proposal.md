## Why

The array-of-tables override syntax hides the command identity in an earlier `command` field, making nested group/user policy headings ambiguous to operators. Put the canonical command name in each table path so the affected command is visible where its policy is edited.

## What Changes

- **BREAKING**: Replace `[[command_runtime.access.overrides]]` entries with `[command_runtime.access.overrides.<command>.groups]` and `.users` tables; remove the redundant `command` field.
- Reject legacy arrays and malformed keyed policies with actionable validation errors.
- Preserve explicit policy requirements, replacement semantics, active-command validation, and help filtering.
- Migrate current examples, development configuration, and tests.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `actor-command-routing`: Specify canonical command-keyed TOML override tables instead of an array with command fields.

## Impact

Configuration snapshot parsing/validation, command access tests, operator documentation, and `dev/onebot/config/bridge_actor.toml`. No runtime routing ABI change, new dependencies, or configuration defaults.
