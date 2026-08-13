## 1. Configuration and validation

- [x] 1.1 Add `sze_startup_warmup_signals` parsing with default 50 and validation for non-negative integers.
- [x] 1.2 Add the field to Shenzhen live/test config templates and config-guard tests.

## 2. Strategy behavior

- [x] 2.1 Replace the fixed Shenzhen warmup constant with the per-instrument configured value.
- [x] 2.2 Calculate prediction/theoretical price during warmup while suppressing T0 and test-order side effects.
- [x] 2.3 Add unit coverage for first-sample initialization, warmup boundary, and post-warmup trading.

## 3. Daily deployment audit

- [x] 3.1 Document required model, static fields, free-share data, position fields, source indexes, routing, and warmup configuration.
- [x] 3.2 Rebuild and run all SZE tests; record the resulting strategy library hash and deployment checklist.
