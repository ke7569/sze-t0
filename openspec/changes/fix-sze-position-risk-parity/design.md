## Context

`static_position` is the configured baseline inventory. `pi` and `last_position` represent the live total-position delta from that baseline, while `shortable` is the broker-reported currently sellable quantity. Shenzhen must preserve these distinct meanings across startup queries, fills, pending orders, and intraday restarts.

## Goals / Non-Goals

**Goals:**

- Match the established Beijing/HP T0 capacity formulas.
- Prevent trading on an instrument until its startup position is authoritative or safely defaulted after cutoff.
- Preserve prediction processing while order routing is position-gated.
- Keep bias direction and magnitude consistent with the Beijing strategy.

**Non-Goals:**

- Redesign cash limits, TD callbacks, factor generation, or orderbook recovery.
- Change the strategy plugin ABI.
- Add short selling.

## Decisions

Buy capacity uses `min(shortable, position_limit) - pi - pending_buy`, then applies the existing per-order notional cap and clamps to zero. This permits a zero-holding instrument to buy back toward its configured baseline because `pi` is negative, while preventing additional purchases when baseline inventory exists but is not sellable.

Startup synchronization atomically assigns `pi = total_position - static_position`, `last_position = pi`, and `shortable = available_position`. Broker values are clamped to valid non-negative ranges.

An authoritative position-query final callback no longer means every omitted instrument is zero. Omitted instruments remain gated. The strategy retries the all-position query at a configurable interval and defaults unresolved instruments to zero only after a configurable local market-time cutoff, defaulting to 09:31:00.

Bias retains the Beijing formula based on current position notional, configured position baseline, and fixed configured bias factor. The Shenzhen-only afternoon multiplier is removed because it is neither present in Beijing nor correctly interpolated in minute time.

## Risks / Trade-offs

- [A broker never completes a useful query] -> unresolved instruments remain gated until cutoff and are then explicitly audited as zero defaults.
- [System clock is wrong] -> startup logs include cutoff and retry state; deployment preflight must retain clock checks.
- [The configured baseline is stale] -> live total and available values remain authoritative for `pi` and sell capacity.
- [More startup queries add load] -> retries are one all-position query per configured interval, not one query per instrument.
