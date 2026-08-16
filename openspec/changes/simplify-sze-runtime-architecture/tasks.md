## 1. Configuration boundary

- [x] 1.1 Remove implicit legacy conversion from the production launcher and add an explicit offline migration utility.
- [x] 1.2 Update preflight, services, README, and tests to use only the strict four-key daily schema.
- [x] 1.3 Verify migrated and current daily configs have identical canonical static hashes and no operational fields.

## 2. Build boundary

- [x] 2.1 Add an opt-in CMake target for the direct sharded-ring prototype and exclude it from default `sze_md`.
- [x] 2.2 Remove direct-ring parsing and lifecycle calls from the default `MDEngineSZE` path while preserving standalone tests.

## 3. Recovery lifecycle

- [x] 3.1 Add a single recovery-resource teardown helper with generation-aware unlink rules.
- [x] 3.2 Replace initialization failure cleanup and normal shutdown duplication with the helper.
- [x] 3.3 Run recovery/health component failure-path coverage for journal reserve,
  continuity, health gating, and clean resource reopen behavior.

## 4. Verification and rollout

- [x] 4.1 Run Python syntax/unit tests, manual C++ regression tests, and default/experimental CMake builds.
- [ ] 4.2 Run capture-only replay and compare events, factors, predictions, and health gates against the pre-refactor build.
- [x] 4.3 Publish a deployment note with rollback files and the explicit legacy migration command.
