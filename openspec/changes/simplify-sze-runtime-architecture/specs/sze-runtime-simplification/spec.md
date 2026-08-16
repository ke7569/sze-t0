## ADDED Requirements

### Requirement: Production daily configuration is strict
The production runtime SHALL accept only `trading_day`,
`static_data_source_date`, `static_data_hash`, and `ins_params` in the daily
business JSON. Legacy all-in-one files MUST be converted explicitly before
launch.

#### Scenario: Legacy file is rejected without migration
- **WHEN** the launcher receives a daily JSON containing operational fields such as `cpu` or `last_position`
- **THEN** preflight fails with a migration error and does not create runtime files

#### Scenario: Current daily file is accepted
- **WHEN** the daily JSON contains exactly the four required keys and a matching static hash
- **THEN** preflight creates the capture/recovery runtime artifacts for the requested trading day

### Requirement: Experimental direct ring is excluded by default
The default production `libsze_md.so` build SHALL exclude direct sharded-ring
parsing, initialization, and publishing. The prototype SHALL remain available
only through an explicit opt-in build/test target.

#### Scenario: Default production build
- **WHEN** the project is configured without the experimental option
- **THEN** `sze_md` builds without direct sharded-ring objects and accepts no direct-ring runtime configuration

#### Scenario: Opt-in prototype build
- **WHEN** the experimental option is enabled
- **THEN** the standalone direct-ring tests and benchmark build and run without changing the default deployment

### Requirement: Recovery teardown has one owner
Recovery initialization failures and normal shutdown SHALL use the same resource
teardown helper, preserving health, diagnostic, SHM, and journal ownership rules.

#### Scenario: Failure during recovery initialization
- **WHEN** health-page or diagnostic initialization fails after journal/SHM creation
- **THEN** all resources created by that generation are closed and no stale readiness marker remains

#### Scenario: Clean shutdown
- **WHEN** recovery receives a clean stop
- **THEN** the flush worker stops before resource teardown and journal clean-shutdown metadata is preserved
