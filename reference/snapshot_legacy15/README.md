# SZE Snapshot Legacy15 Sidecar

This is a standalone production-side process for the `snapshot-legacy15-sze`
bundle. It does not link into `sze-t0`, does not share its capture socket, and
does not publish orders. It joins the SZE snapshot multicast and writes one
15-second prediction row per accepted adjacent snapshot pair.

The model ABI is frozen at 36 factors in `factors/factors.txt`. Hidden state is
kept independently per instrument and reset at each trading-day boundary.

Build from the source tree:

```bash
bash source/reference/snapshot_legacy15/build_sze_snapshot_legacy15.sh
python3 source/reference/snapshot_legacy15/convert_snapshot_legacy15.py \
  /home/zane/snapshot-legacy15-sze \
  /home/zane/snapshot-legacy15-sze/model/snapshot_legacy15.bin
```

Run a bounded validation before installing the service:

```bash
./source/reference/snapshot_legacy15/sze_snapshot_legacy15_predictor \
  --weights /home/zane/snapshot-legacy15-sze/model/snapshot_legacy15.bin \
  --scaler /home/zane/snapshot-legacy15-sze/model/scaler.json \
  --seconds 30 --output /tmp/sze_snapshot_legacy15_predictions.csv
```

The systemd unit is intentionally supplied but should remain disabled until a
trading-session parity and latency check is complete.
