# Repository Guidance

## C++ Style

- Do not use `using namespace` outside test code. Use fully qualified names,
  namespace aliases, or targeted `using` declarations instead.

## Configuration

- Do not introduce default values for configuration options; require every
  option to be specified explicitly.
- If a configuration default is desired, ask the user for explicit approval
  before adding or documenting it.

## Build and Test Performance

- Use at least 6 parallel workers for compilation and test execution.
- When system load is low, use all available CPU cores to reduce build and test
  time.

## Commit Checks

- Before every commit, run `nix fmt` from the repository root. This formats the
  core repository and every repository under `local_actor/` with the root
  `.clang-format`.

## Commit message

- Allow to commit without gpg sign but **must remind user to amend them with gpg sign**

## Unit test rule

- Test current observable behavior and meaningful failure boundaries, not the
  absence of deleted features. When A is replaced by B, update or replace A's
  tests; do not keep a historical `not A` suite alongside B.
- Do not add tests solely to reject retired names, assert deleted files/symbols
  are absent, or repeat plain data-member assignments. Keep negative tests for
  actual risks such as invalid current inputs, authorization, data loss and
  lifecycle safety.
- Check existing coverage before adding a case. Prefer extending a relevant
  behavior test and reusing its expensive fixture over duplicate configure/build
  cycles or source-text checks already covered by real compilation/execution.
- Reduce redundant work, not just reported counts: do not hide unchanged test
  volume inside loops or giant combined tests. Test totals are not a quality goal.
