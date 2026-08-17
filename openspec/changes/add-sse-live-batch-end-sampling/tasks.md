## 1. Live Sampling Core

- [x] 1.1 Implement the deterministic two-phase SSE batch-end sampler with strict `>100000ns`, per-instrument coalescing, deduplication, and fail-closed recovery.
- [x] 1.2 Implement the one-shot `CLOCK_MONOTONIC` timerfd adapter without adding a periodic worker.
- [x] 1.3 Implement the post-batch turnover/time/change sample gate from the frozen SSE reference contract.

## 2. Configuration And Integration

- [x] 2.1 Add the sampler and timer targets to CMake and expose the integration boundary to the SSE market-data/factor adapter.
- [x] 2.2 Extend SSE config validation for the strict batch policy, native hybrid artifacts, prediction-only mode, and disabled order routing.
- [x] 2.3 Generate dated Shanghai market-data, hybrid prediction, and process configs without credentials or host-specific interface values.

## 3. Verification And Delivery

- [x] 3.1 Add deterministic tests for exact/greater gaps, deadline rearming, late timer service, coalescing, duplicates, recovery, session gating, and all standard sample triggers.
- [x] 3.2 Update SSE architecture/deployment documentation and package the live sampling sources and configs with the hybrid model bundle.
- [x] 3.3 Build and run the focused SSE test suite, validate configs/scripts/manifests, and record the deployment result.
- [x] 3.4 Commit only the intended SSE/OpenSpec changes and push `feature/sse-t0` to its remote branch.
