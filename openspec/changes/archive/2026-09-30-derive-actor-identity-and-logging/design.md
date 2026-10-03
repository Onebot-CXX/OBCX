## Context

Each actor already has a separately compiled artifact and registered implementation/test targets. The admitted graph contains actor.name and package.version. ReflectedActor currently reads handwritten Derived constants, while all logging macros obtain a single obcx logger. Existing runtime and command code also includes the reflection header for shared dispatch utilities, and some host tests define multiple in-memory actor identities in one translation unit.

## Goals / Non-Goals

Goals: one authoritative manifest identity, unchanged one-parameter actor authoring, actor-owned log tags even in background work, unchanged C ABI exports, and safe shared logger creation. Keep staged and unrelated work intact.

Non-goals: runtime caller-context propagation, assigning a caller's identity to precompiled common libraries, logging raw transport responses/secrets, deploying binaries, changing error routing, or changing configuration defaults.

## Decisions

- Generate a binding header in the package build directory from admitted metadata. Namespace it with a digest of package identity/name/version. Generate a metadata-only header for logging so including logger.hpp does not pull reflection into helper libraries.
- Split common reflection implementation/export machinery from the public package-bound authoring facade. The implementation takes Derived and Identity; generated aliases in distinct namespaces bind Identity, then a targeted using declaration exposes core::ReflectedActor. No macro-dependent common class-template definition.
- Package registration supplies PRIVATE generated-header definitions to actor artifact, implementation and test compilation targets. Public interface libraries do not export actor identity. Existing package source/provider ownership checks remain intact.
- Production actor classes inherit generated constants and keep existing export macros. Core-only utilities include the implementation header instead of the package facade. Host-only multi-identity tests explicitly bind fixture identities through a test helper, without introducing a production fallback.
- Log macros select actor identity at the call site; no thread-local actor selection and no global default logger switching. Shared libraries retain their own source attribution. Named loggers share host sinks, levels and flushing. Synchronize initialization and named registration; prevent init logging recursion.
- Reconfigure tracks manifest changes; unchanged generated headers retain timestamps. Missing metadata is an explicit build error. Use non-reserved macro names.
- The installed SDK fixture now includes a real internal static helper. It exposed an existing audit error: Unix Makefiles link fragments are target-binary-directory relative, unlike Ninja's build-root-relative fragments. Resolve link items/search paths against the generator's actual link working directory while retaining the same declared artifact closure checks. Do not bypass the audit or switch the fixture generator to hide this failure.
- An installed-template Ninja build also exposed duplicate missing-input rules when a provider receipt names an included SDK module through `../` segments. Normalize tracked provider input/receipt paths before adding CMAKE_CONFIGURE_DEPENDS. Extend the existing real-build fixture with that receipt alias, without adding a separate configure/build fixture.

## Risks / Trade-offs

- One package facade per translation unit → actor code must not include another actor's authoring facade; cross-actor communication uses public messages. Multi-identity host tests use explicit internal fixture binding.
- Statically compiled actor helpers need the same identity → apply binding at package target registration, not only obcx_add_actor.
- SDK consumers require the new header/helper files → extend existing installed SDK smoke and real actor tests, rebuild actors with the matching SDK.
- Common templates/inline functions must not depend on the current-package facade → keep runtime dispatch utilities and implementation identity-parameterized and macros expanded at call sites.
- Generated files sit in audited build roots → retain ownership and incrementality checks rather than bypassing target audit.

## Migration Plan

Generate bindings, split SDK implementation/facade, update logging, remove maintained actor identity declarations, migrate fixture build wiring and update documentation. Verify using focused tests and the existing installed SDK fixture; do not activate rebuilt binaries. Rollback is a source/build rollback, not a database migration.

## Open Questions

None for the agreed scope. Independent experimental actors not present in the workspace may need rebuilding with their own explicit package metadata.
