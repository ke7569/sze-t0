# SSE/T0 Runtime Split

The two-day rollout should keep three boundaries independent:

```text
UDP multicast channels
        |
        v
shared UDP channel runtime (`modules/deepwin_guoxin/md/common/UdpChannelRuntime.*`)
        +----------------------------> raw capture / diagnostics
        |
        v
exchange decoder (SSE or SZE) ------> normalized L2 event stream
        |
        v
factor engine (CompleteOrderBookSH for SSE)
        |
        v
strategy process (HStrategy or ZStrategy)
```

The socket/channel runtime can be shared by SSE and SZE because both feeds are
multi-channel UDP streams. The decoder must remain exchange-specific: packet
headers, channel sequence semantics, order/trade identifiers, cancel rules, and
batch-end sampling differ. The strategy process must consume normalized events
and must not own multicast sockets. The existing SZE receiver implementations
are the first migration target; this offline phase does not change their
production behavior.

SSE and SZE factors are separate contracts. SSE v0.4 consumes the ordered 50
factors in `sse-t0/model/factors.txt`; SZE continues to use its own factor
engine/model. The old generic `SsePredictor` path is not the SSE v0.4 runtime.

## Rollout boundary

- Tomorrow: run the raw observer and decoder in offline/loopback mode, then move
  the same capture binary and channel config to the target host. Capture first;
  no strategy or order routing is required.
- The day after: run the market-data process for the full session, persist
  normalized SSE events and factor rows, and run the 50-factor model skeleton
  with one hidden state per stock-day.
- Only after a full-day factor report is stable should `HStrategy` be connected
  to the strategy process. TD credentials and order routing remain outside the
  market-data process.

## Why not one exchange-specific script?

Use one common launcher/runtime for socket lifecycle, channel metrics, capture,
rotation, and shutdown. Pass an exchange decoder plugin and a channel manifest
to it. This avoids duplicating operational code while preserving a hard protocol
boundary where SSE and SZE behavior can be tested independently.
