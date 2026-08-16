## Context

The current deployment has a strict fixed system configuration, a dated daily
static configuration, and a runtime generator. In parallel, the MD library
still contains parsing and lifecycle code for a direct sharded ring that the
production documentation explicitly describes as experimental and disabled.
The recovery initializer also has several hand-written failure paths that close
the same journal, SHM, health, and diagnostic resources in different orders.

## Goals / Non-Goals

**Goals:**

- Keep one strict production configuration contract and make migration explicit.
- Keep experimental direct-ring code buildable without placing it in the
  default production MD binary or configuration parser.
- Make recovery resource ownership and teardown single-path and testable.

**Non-Goals:**

- No changes to order matching, factor calculation, prediction numerics, journal
  ABI, health-page ABI, TD routing, or CPU assignments.
- No production enablement of the direct sharded ring.

## Decisions

1. **Strict daily schema.** `prepare_sze_runtime.py` accepts only the four-key
   daily schema. A separate offline migration command converts an old
   all-in-one file; launchers never pass a legacy override implicitly.

2. **Experimental ring boundary.** Add an explicit CMake option for the direct
   ring prototype. The default `sze_md` target excludes its source and
   `MDEngineSZE` does not parse or initialize it. The standalone prototype test
   remains available when the option is enabled.

3. **Recovery cleanup owner.** Add one private `reset_recovery_resources()`
   helper in `MDEngineSZE`. Every initialization failure and normal shutdown
   calls it after stopping the flush worker. The helper closes resources in the
   dependency order health/diagnostics -> SHM -> journal and removes only
   resources owned by the current generation.


## Risks / Trade-offs

- [Risk] An operator may still have an old daily JSON. -> Migration command and
  a preflight error provide an explicit conversion path; no silent fallback.
- [Risk] A caller depends on direct-ring symbols from `libsze_md.so`. -> The
  prototype was documented as disabled; standalone test/benchmark targets keep
  the implementation available for opt-in builds.
- [Risk] Cleanup refactoring changes shutdown ordering. -> Preserve current
  order in tests and add failure-injection coverage for each initialization
  stage.

## Migration Plan

1. Add the migration utility and convert one known daily config; compare its
   canonical static hash with the source.
2. Build default MD/strategy targets and run all existing CTest tests.
3. Run the preflight and a capture-only replay on a saved journal.
4. Keep the previous shared libraries available for rollback, then deploy the
   simplified build after the replay diff is empty.
