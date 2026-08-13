## ADDED Requirements

### Requirement: Shenzhen startup prediction warmup
The Shenzhen strategy SHALL calculate and expose prediction/theoretical-price outputs for each configured startup warmup sample, while SHALL NOT submit, cancel, or otherwise execute trading actions during those samples.

#### Scenario: Default warmup
- **WHEN** `sze_startup_warmup_signals` is absent
- **THEN** the strategy SHALL use 50 dispatched samples as the warmup window

#### Scenario: Warmup prediction output
- **WHEN** a sample arrives and its per-instrument count is within the warmup window
- **THEN** the strategy SHALL update the market context and calculate the prediction/theoretical price, but SHALL skip `handleT0` and order submission

#### Scenario: Post-warmup trading
- **WHEN** the per-instrument sample count exceeds the configured warmup window
- **THEN** the strategy SHALL use the normal T0 decision and order-routing path

#### Scenario: Configured warmup
- **WHEN** `sze_startup_warmup_signals` is a non-negative integer
- **THEN** the strategy SHALL use that value for the per-instrument warmup window

#### Scenario: Invalid warmup configuration
- **WHEN** `sze_startup_warmup_signals` is not an integer or is negative
- **THEN** Shenzhen config validation SHALL reject the configuration before strategy startup
