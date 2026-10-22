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
- For hot-reloadable actor builds, use `scripts/actor_build.py` with a frozen
  installed SDK; see `packaging/actors/README.md`. Do not build actor targets in
  the live core's workspace: their transitive dependencies can rebuild and
  replace `libobcx_core.so`. Core/SDK updates and process restarts are separate,
  explicit operations.

## Commit Checks

- Before every commit, run `treefmt` from the repository root. This formats the
  core repository and every repository under `local_actor/` with the root
  `.clang-format`.

## Commit message

- Unsigned commits are allowed, but **must remind the user to amend them with
  GPG signatures**.
- Provide a complete, copy-pasteable shell command covering every repository
  where unsigned commits were created, not just a generic
  `git commit --amend --no-edit -S`. Include a 'for loop' meet user using shell
  as the `git -C` workdir.

## Unit test rule

- Do not write any unit test unless i asked
