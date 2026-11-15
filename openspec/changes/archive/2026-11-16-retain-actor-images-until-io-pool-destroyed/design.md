# Design

## Context

See proposal.md for motivation. The baseline reproduces SIGSEGV after two reloads plus SIGTERM, and during reload three in five-reload attempts. `thread_pool::join()` finishes execution but leaves execution-context services alive; header-only Asio can instantiate their virtual functions and vtables in actor images. Linux currently clears the manager's retired library handles immediately after joining; Apple already retains them through member destruction.

## Goals / Non-Goals

**Goals:** Keep actor code mapped through execution-context service destruction in normal retirement, failed candidate teardown, validation, and startup failure.

**Non-Goals:** Change drain semantics, reorder members, change the actor ABI, rebuild or edit actor repositories, introduce configuration options, or add unit tests.

## Decisions

Remove the platform conditional and early `finish_actor_retirement()` call from `RuntimeGeneration::shutdown()`. Keep actor instance retirement and pool joining unchanged, with a platform-independent comment explaining the Asio service lifetime constraint.

Use existing reverse member destruction order in `include/core/runtime/runtime_generation.hpp:133-159`: coordinator, orchestrator, blocking-executor reference, scheduler, services, and actor I/O pool are released before `actor_manager_` (line 137). The manager destructor performs final library retirement; staged packages and their directory owner follow. The pool shared pointer is private and is never copied: consumers receive only its executor. Lifecycle retirement clears actor callbacks before joining, and admitted background work retains generation leases. No production caller of `unload_actor()` or `unload_all_actors()` bypasses generation teardown; the latter is called by the manager destructor. Discovery-error handles belong to images that have not received a generation executor; package-stager `RTLD_NOLOAD` probes only balance their own loader references.

Alternative: explicitly reset/destroy the pool before calling `finish_actor_retirement()`. Rejected because member-order retention already protects services, scheduler, and other actor-code holders as well as the pool, matches Apple behavior, and avoids introducing a separate manual destruction sequence or invalidating executor-bearing members early.

## Risks / Trade-offs

- Retired images remain mapped until their generation object is destroyed → intentional bounded lifetime extension; not permanent process-wide retention.
- Future member reordering or a copied pool owner could invalidate the constraint → document it at shutdown and retain the ownership audit with verification results.
- Existing root CTest coverage is unavailable in this checkout (`tests/` is empty) → report the absence and use the preserved socket-probe, template, and persona integration harness without adding tests.

## Migration Plan

Rebuild the shared core and executable with 20 jobs. Existing actors dynamically link `libobcx_core.so` and need no relinking. Run available existing tests and repeat the preserved two- and five-reload scenarios using the rebuilt core, with all fixtures under `/tmp`. Rollback is reverting this implementation-only change and rebuilding; no data migration is involved.
