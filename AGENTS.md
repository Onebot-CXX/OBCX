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

- Before every commit, run `treefmt` from the repository root. This formats the
  core repository and every repository under `local_actor/` with the root
  `.clang-format`.

## Commit message

- Unsigned commits are allowed, but **must remind the user to amend them with
  GPG signatures**.
- Provide a complete, copy-pasteable shell command covering every repository
  where unsigned commits were created, not just a generic
  `git commit --amend --no-edit -S`. Include `cd` to the repository root,
  `export GPG_TTY=$(tty)`, and a loop listing the actual affected repository
  paths (including `.` for core when applicable). Run `treefmt` from the root
  before each amend, use `git -C "$repo" commit --amend --no-edit -S`, and stop
  on failure.

## Unit test rule

- Do not write any unit test unless i asked
