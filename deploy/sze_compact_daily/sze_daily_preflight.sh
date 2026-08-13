#!/usr/bin/env bash
set -euo pipefail

ROOT="${ROOT:-/home/zane}"
CONFIG_DIR="${CONFIG_DIR:-${ROOT}/configs}"
DAILY_JSON="${1:-${DAILY_JSON:-}}"
if [[ -z "$DAILY_JSON" ]]; then
  DAILY_JSON="$(ls -1t "${CONFIG_DIR}"/config_sze_daily_*.json 2>/dev/null | head -n 1 || true)"
fi
MD_TEMPLATE="${MD_TEMPLATE:-${ROOT}/run_main/deepwin_sze_template.json}"
MODEL="${MODEL:-${ROOT}/models/mix153060_sze_v04_a3_eff60.bin}"

python3 - "$DAILY_JSON" "$MD_TEMPLATE" "$MODEL" <<'PY'
import hashlib
import json
import os
import sys

daily_path, md_path, model_path = sys.argv[1:]
daily = json.load(open(daily_path))
md = json.load(open(md_path))
day = int(daily["trading_day"])
assert daily["market"] == "SZ"
assert daily["sze_startup_warmup_signals"] >= 0
assert daily["worker_count"] == len(daily["worker_cpus"])
assert daily["worker_count"] == len(daily["worker_state_cpus"])
assert len(set(daily["worker_cpus"])) == daily["worker_count"]
assert len(set(daily["worker_state_cpus"])) == daily["worker_count"]
assert set(daily["worker_cpus"]).isdisjoint(daily["worker_state_cpus"])
for code, params in daily["ins_params"].items():
    assert int(params["Date"]) == day, (code, params["Date"], day)
    assert int(params["cpu"]) in daily["worker_cpus"], (code, params["cpu"])
assert daily["sze_recovery_consumer"]["trading_day"] == day
pipe = md["md"]["sze"]["recoverable_pipeline"]
assert pipe["journal_prefix"] == daily["sze_recovery_consumer"]["journal_prefix"]
assert int(pipe["journal_max_payload_bytes"]) == 128
assert os.path.isfile(model_path)
actual = hashlib.sha256(open(model_path, "rb").read()).hexdigest()
assert actual == daily["mix153060_model_sha256"], (actual, daily["mix153060_model_sha256"])
print(json.dumps({"ok": True, "trading_day": day,
                  "instruments": len(daily["ins_params"]),
                  "workers": daily["worker_count"],
                  "model_sha256": actual}, separators=(",", ":")))
PY
