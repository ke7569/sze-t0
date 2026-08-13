## ADDED Requirements

### Requirement: Two-file Shenzhen daily deployment
The Shenzhen production directory SHALL require exactly two daily-changing business files: `config_sze_daily_YYYYMMDD.json` and `main_sze_daily_YYYYMMDD.conf`. The market-data JSON, libraries, scripts, and systemd units SHALL be fixed deployment files.

#### Scenario: Daily publish
- **WHEN** a new trading day is prepared
- **THEN** the operator SHALL replace only the two dated business files in the live config directory

#### Scenario: Fixed runtime paths
- **WHEN** the capture or recovery service starts
- **THEN** it SHALL read the fixed MD JSON and the two daily business files, deriving trading-day journal, SHM, and output paths from configuration content

### Requirement: Single recovery launcher with runtime workers
The Shenzhen recovery service SHALL start one fixed launcher which SHALL create and supervise the configured number of recovery worker processes. Each instrument SHALL have one deterministic worker owner selected from `ins_params.<instrument>.cpu` or the configured hash planner.

#### Scenario: Worker assignment
- **WHEN** the strategy JSON contains `worker_count` and `worker_cpus`
- **THEN** each instrument SHALL be assigned to exactly one runtime worker and the assignment SHALL be logged before replay begins

#### Scenario: No shard files
- **WHEN** the fixed recovery service starts
- **THEN** it SHALL not require deployed shard JSON, shard main conf, dated manifest, or dated unit instances; temporary runtime configs MAY be created under `/dev/shm`

### Requirement: Audit separation
Static audit, rejected-universe, universe CSV, and artifact checksum files SHALL be optional research/archive outputs and SHALL NOT be required by the production preflight.

#### Scenario: Missing audit artifacts
- **WHEN** audit files are absent but the two daily business JSON files are valid
- **THEN** production preflight SHALL still pass

### Requirement: Safe daily validation
The fixed preflight SHALL validate that the trading day, static parameter dates, model hash, journal paths, SHM path, and worker CPU assignments are mutually consistent before starting capture or recovery.

#### Scenario: Date mismatch
- **WHEN** `trading_day` differs between the two daily JSON files
- **THEN** preflight SHALL fail without starting either process
