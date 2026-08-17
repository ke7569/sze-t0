## Context

The native SSE Snapshot and v0.4 tick GRUs and their 09:35 handover are
implemented, but the live strategy remains fail-closed because the SSE decoder
and factor adapters are not connected. The tick model contract admits only
CompleteOrderBookSH candidates at the end of an SSE delivery batch. Offline,
the end is known from the next local timestamp; live, it must be recognized
after local receive inactivity without advancing the GRU on repeated book
states.

The shared UDP runtime records both a kernel receive timestamp and a monotonic
timestamp, and invokes one receive worker per channel. SSE and SZE decoding and
sampling semantics must remain separate above that socket layer. The target
toolchain is CentOS 7.9, GCC 4.8.5, C++11.

## Goals / Non-Goals

**Goals:**

- Implement the strict SSE batch-end threshold as `gap > 100000ns`.
- Produce one deterministic batch-end result containing the last eligible
  CompleteOrderBookSH candidate per changed instrument.
- Make timer servicing and next-event gap detection converge on the same state
  transition, even when the timer thread is scheduled late.
- Preserve the normal turnover/time/change sampling gate after batch-end
  eligibility.
- Supply a prediction-only Shanghai configuration with native tick and
  Snapshot artifact paths and fail-closed feed-health settings.

**Non-Goals:**

- Decode an undocumented vendor SSE wire format in the offline environment.
- Claim factor parity before the live decoder and frozen 50-factor generator
  are connected and checked against a full-day capture.
- Add order routing or enable live trading.
- Change SZE sampling or the exchange-neutral UDP socket runtime.

## Decisions

### Use a trailing-edge one-shot detector, not a periodic 10kHz publisher

Each normalized SSE update rearms one deadline at `last_activity + 100000ns`.
The detector closes a batch only when the observed gap is strictly greater
than the threshold. A periodic publisher could cut through a delivery burst or
advance the recurrent state repeatedly without new data.

The state machine has a two-phase event API. It first advances to the new
event's receive timestamp and closes an older batch before that event mutates
the order book, then commits the applied event and rearms the deadline. The
same close operation is used by a timer expiry. This makes a late timer safe:
the first event whose timestamp crosses the deadline closes the old batch
before its book update is applied.

### Keep the detector deterministic and expose a Linux one-shot timer adapter

The core sampler owns no thread. It exposes the next absolute monotonic
deadline and accepts event and timer transitions, making it replay-testable.
A small `timerfd` wrapper lets the production market-data event loop arm a
one-shot `CLOCK_MONOTONIC` deadline without a fixed-frequency wakeup. A stale
timer cannot close a new batch because every transition rechecks the current
deadline.

### Coalesce candidates per instrument and keep feed health fail-closed

All updates define batch activity, while only CompleteOrderBookSH events can
become sampling candidates. The sampler keeps the latest candidate per
instrument and emits it once at batch close. Non-increasing cut indexes are
suppressed. A sequence gap or non-monotonic receive timestamp invalidates the
pending batch; sampling resumes only after an explicit recovery/reset.

### Use separate clocks for batching and model routing

Receive-gap classification and timer deadlines use local monotonic
nanoseconds. The candidate retains the last real exchange timestamp for
session checks and the 09:35 Snapshot-to-tick switch. Timer time is recorded as
diagnostic metadata and never replaces exchange time.

### Preserve the original downsample trigger after batch eligibility

Batch end is necessary but not sufficient. A per-instrument gate accepts a
valid continuous-trading cut only when accumulated turnover reaches its static
threshold, 100 seconds of exchange time have elapsed, or the mid changes with
at least 100 shares of volume. The window start advances only on an accepted
sample, matching the frozen feature reference.

### Keep live integration at the SSE market-data/factor boundary

The intended path is:

```text
UDP -> SSE decoder/sequencer -> order-book update -> batch detector
    -> CompleteOrderBookSH cut -> normal sample gate -> 50 factors
    -> native hybrid model -> prediction log
```

The detector is not added to `UdpChannelRuntime` or to the GRU runtime. Until a
target-host adapter supplies decoded updates and factor rows, HStrategy remains
fail-closed rather than producing unverified predictions.

## Risks / Trade-offs

- [Userspace scheduling can service a 100us deadline late] -> Close the old
  batch from the next event timestamp before applying the next update, and log
  timer lateness separately.
- [Multiple channels may not share one batching cadence] -> Key detector
  instances by logical sequenced feed lane and record channel/gap metrics during
  the first live session before consolidating lanes.
- [Offline has next-event lookahead while live can close on silence] -> Treat
  the difference as intentional, retain both event and emission timestamps,
  and compare sample identities in the full-day capture.
- [Packet loss can create a plausible but incomplete book] -> Invalidate the
  batch and suppress samples until the decoder reports recovery/resynchronization.
- [The live factor adapter is not available offline] -> Ship an explicit
  prediction-only candidate config and integration API, but retain
  `production_approval=false` and HStrategy fail-closed behavior.

## Migration Plan

1. Build and run deterministic boundary and sample-gate tests offline.
2. Deploy the prediction-only config, native model package, and branch to the
   target host.
3. Connect decoded SSE events to the two-phase sampler and arm its returned
   timerfd deadline from the market-data event loop.
4. Log batch sizes, local gaps, timer lateness, dirty instruments, accepted
   cuts, factors, and predictions for a full session with trading disabled.
5. Compare live rows with the frozen factor contract before enabling HStrategy
   output consumption. Roll back by disabling the SSE prediction process; SZE
   is unaffected.

## Open Questions

- Whether the vendor channels are independent logical batch lanes or share one
  cross-channel watermark must be confirmed from the first live capture.
- The final instrument universe and daily turnover/static inputs must be
  generated on the target host before the candidate config can cover all SSE
  symbols.
