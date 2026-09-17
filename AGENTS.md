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
