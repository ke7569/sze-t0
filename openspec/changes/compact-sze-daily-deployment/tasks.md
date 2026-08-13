## 1. Stable daily file contract

- [x] 1.1 Define dated names and schemas for `config_sze_daily_YYYYMMDD.json` and `main_sze_daily_YYYYMMDD.conf`; keep MD config fixed.
- [x] 1.2 Update the daily generator to emit the dated pair and keep audit files outside the live package.
- [x] 1.3 Add worker_count/worker_cpus and per-instrument CPU validation.

## 2. Fixed runtime orchestration

- [x] 2.1 Add fixed main conf and fixed capture/recovery service templates.
- [x] 2.2 Add preflight that validates both daily files and rejects stale dates without requiring audit artifacts.
- [x] 2.3 Replace date-specific shard startup with one fixed launcher invocation.

## 3. Runtime recovery workers

- [x] 3.1 Implement deterministic runtime worker config generation from the single strategy config.
- [x] 3.2 Preserve per-instrument orderbook ownership and isolate worker processes from journal ingestion.
- [x] 3.3 Add worker metrics and replay/prediction output partitioning.

## 4. Verification and migration

- [x] 4.1 Add config and orchestration tests for the two-file contract.
- [ ] 4.2 Replay a representative journal and compare runtime-launched worker output with the existing shard output.
- [x] 4.3 Document deployment, rollback, and the final daily file count.
