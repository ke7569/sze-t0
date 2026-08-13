# SSE live capture package

This package is for tomorrow's first live connection only. It receives raw SSE
FPGA UDP datagrams from the snapshot and tick channels, records local arrival
timestamps, and does not start a strategy or connect TD.

## Upload contents

Upload the generated `sse-live-capture-*.tar.gz` as one file. It contains:

```text
bin/sse_udp_observer
config/sse_udp_observer.example.json
config/sse_udp_observer_dongguan.example.json
docs/LV1_ENDPOINTS.md
start_sse_capture.sh
stop_sse_capture.sh
analyze_sse_capture.py
SHA256SUMS
```

## Start

On the live server, choose the actual market-data NIC IP. For Jinqiao:

```text
./start_sse_capture.sh --site jinqiao --interface-ip MARKET_DATA_NIC_IP --output-dir ./capture
```

For Dongguan:

```text
./start_sse_capture.sh --site dongguan --interface-ip MARKET_DATA_NIC_IP --output-dir ./capture
```

The physical server IPs in the supplied sheet are reference values and may not
be the multicast-facing interface. Replace `MARKET_DATA_NIC_IP` with the
actual address. The command prints the JSONL and
stderr paths. Keep the process running for the full session.

Stop after the session:

```text
./stop_sse_capture.sh ./capture
```

Analyze the relationship between snapshot and tick arrivals:

```text
./analyze_sse_capture.py ./capture/sse_udp_*.jsonl
```

The analyzer accepts one or more JSONL files, so a set of rotated files can be
passed with the same command.

Each JSONL row includes `ts_ns` (Linux `SO_TIMESTAMPNS` wall-clock Unix
nanoseconds, with a user-space fallback),
`monotonic_ns` (local monotonic nanoseconds), channel name, source endpoint,
length, and the first 64 bytes. `monotonic_ns` is the preferred field for
within-process timing; `ts_ns` is retained for correlation with server logs.

The channel names `snapshot`/`snapshot_primary` and `tick`/`tick_primary`
correspond to the two streams requested for tomorrow: snapshot (切片/快照)
and tick-by-tick (逐笔) FPGA market data.

Do not infer packet field layouts from this capture alone. Preserve the raw
JSONL and stderr log for the next decoder step.
