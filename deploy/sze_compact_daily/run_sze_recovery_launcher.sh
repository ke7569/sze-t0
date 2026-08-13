#!/usr/bin/env bash
set -euo pipefail

ROOT="${ROOT:-/home/zane}"
RUN_MAIN="${RUN_MAIN:-${ROOT}/run_main}"
CONFIG_DIR="${CONFIG_DIR:-${ROOT}/configs}"
MAIN_BIN="${MAIN_BIN:-${ROOT}/bin/main}"
DAILY_JSON="${1:-${SZE_DAILY_JSON:-}}"
if [[ -z "$DAILY_JSON" ]]; then
  DAILY_JSON="$(ls -1t "${CONFIG_DIR}"/config_sze_daily_*.json 2>/dev/null | head -n 1 || true)"
fi
[[ -f "$DAILY_JSON" ]] || { echo "missing daily SZE config: $DAILY_JSON" >&2; exit 1; }
[[ -x "$MAIN_BIN" ]] || { echo "missing executable: $MAIN_BIN" >&2; exit 1; }
[[ -f "$RUN_MAIN/libt0_strategy_sze.so" ]] || { echo "missing libt0_strategy_sze.so" >&2; exit 1; }

read -r TRADING_DAY WORKER_COUNT CAPTURE_DIR <<EOF
$(python3 - "$DAILY_JSON" <<'PY'
import json, sys
d=json.load(open(sys.argv[1]))
r=d.get("sze_prediction_capture", {})
print(d["trading_day"], d.get("worker_count", 8), r.get("directory", "/home/zane/run_main/log/sze_all_{}".format(d["trading_day"])))
PY
)
EOF

RUNTIME_DIR="${SZE_RUNTIME_DIR:-/dev/shm/sze_recovery_${TRADING_DAY}}"
rm -rf "$RUNTIME_DIR"
mkdir -p "$CAPTURE_DIR"
export SZE_RUN_MAIN="$RUN_MAIN"
python3 "$RUN_MAIN/plan_sze_recovery_runtime.py" \
  "$DAILY_JSON" "$RUNTIME_DIR"

export TZ="${TZ:-Asia/Shanghai}"
export LD_LIBRARY_PATH="$RUN_MAIN:${ROOT}/runtime_so/deepwin_core/lib/wingchun:${ROOT}/runtime_so/deepwin_core/lib/yijinjing:${ROOT}/runtime_so/third_party/wingchun:${ROOT}/runtime_so/third_party/boost:/opt/deepwin/master/lib/wingchun:/opt/deepwin/master/lib/yijinjing:/opt/deepwin/toolchain/boost-1.62.0/lib:${LD_LIBRARY_PATH:-}"
PIDS=()
cleanup() {
  trap - TERM INT EXIT
  for pid in "${PIDS[@]:-}"; do kill -TERM "$pid" 2>/dev/null || true; done
  for pid in "${PIDS[@]:-}"; do wait "$pid" 2>/dev/null || true; done
  rm -rf "$RUNTIME_DIR"
}
trap cleanup TERM INT EXIT

for conf in "$RUNTIME_DIR"/configs/main_worker_*.conf; do
  [[ -f "$conf" ]] || continue
  (cd "$RUN_MAIN" && "$MAIN_BIN" "$conf") >"$RUNTIME_DIR/$(basename "$conf").log" 2>&1 &
  PIDS+=("$!")
done
[[ "${#PIDS[@]}" -eq "$WORKER_COUNT" ]] || { echo "expected $WORKER_COUNT workers, started ${#PIDS[@]}" >&2; exit 1; }
echo "SZE recovery launcher started trading_day=${TRADING_DAY} workers=${#PIDS[@]} runtime_dir=${RUNTIME_DIR}"
while :; do
  alive=0
  for pid in "${PIDS[@]}"; do
    if kill -0 "$pid" 2>/dev/null; then alive=$((alive + 1)); fi
  done
  [[ "$alive" -eq "${#PIDS[@]}" ]] || { echo "SZE recovery worker exited" >&2; exit 1; }
  sleep 1
done
