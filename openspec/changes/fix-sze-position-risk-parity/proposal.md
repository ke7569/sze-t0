## Why

The Shenzhen live strategy currently overstates buy capacity when sellable inventory is unavailable, leaves `last_position` stale after startup synchronization, and treats incomplete position-query results as zero holdings immediately. These differences from the proven Beijing/HP position semantics can create unintended overnight inventory after an intraday restart.

## What Changes

- Restore Shenzhen buy-cap calculation to the Beijing/HP T0 formula bounded by sellable inventory.
- Synchronize `last_position` with the live total-position delta from `static_position`.
- Keep instruments missing from a position response gated, retry the query, and default them to zero only after a configurable market-time cutoff.
- Restore the Shenzhen bias factor to the fixed Beijing baseline and remove the incorrect HHMM-based afternoon multiplier.
- Add deterministic unit tests for position formulas, startup synchronization, cutoff behavior, and bias calculations.

## Capabilities

### New Capabilities
- `sze-position-risk-parity`: Defines Shenzhen startup-position, buy/sell capacity, unresolved-position, and bias behavior against the Beijing/HP baseline.

### Modified Capabilities

None.

## Impact

The change affects `ZStrategy`, Shenzhen startup risk-state handling in `StrategyBase`, live-routing configuration, and strategy tests. It does not alter the exported strategy ABI, market-data plugins, TD adapter, model, factors, or prediction output.
