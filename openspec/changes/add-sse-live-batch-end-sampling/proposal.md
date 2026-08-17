## Why

The SSE v0.4 tick model was trained only on CompleteOrderBookSH samples at
market-data batch ends, identified by a strict local receive-time gap greater
than 100 microseconds. The live path needs an explicit SSE-only batch detector
before it can generate model-compatible factor rows and predictions during the
next trading session.

## What Changes

- Add an event-driven SSE batch-end sampler that emits once after more than
  100 microseconds of receive-time inactivity instead of publishing MD on a
  fixed 100-microsecond grid.
- Preserve the last real event metadata, coalesce all updates in a batch, and
  prevent duplicate samples from advancing recurrent model state.
- Define deterministic handling for exact-threshold events, stale deadlines,
  delayed timer servicing, sequence gaps, session resets, and shutdown.
- Add a deployable Shanghai live prediction configuration for the Snapshot to
  tick handover and the v0.4 native inference artifacts.
- Add offline timing and integration tests plus deployment documentation.

## Capabilities

### New Capabilities

- `sse-live-batch-end-sampling`: Detect SSE market-data batch ends from local
  receive timing and gate live CompleteOrderBookSH factor/model samples.

### Modified Capabilities


## Impact

The change affects the SSE-specific market-data/factor boundary, SSE CMake
targets and tests, and deployment configuration under `sse-t0/`. The shared
exchange-neutral UDP socket runtime and the SZE factor/model paths remain
unchanged. The native GRU runtimes continue to accept only already validated
factor vectors.
