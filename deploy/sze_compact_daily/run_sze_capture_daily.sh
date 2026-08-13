#!/usr/bin/env bash
set -euo pipefail

ROOT="${ROOT:-/home/zane}"
RUN_MAIN="${RUN_MAIN:-${ROOT}/run_main}"
CONFIG_DIR="${CONFIG_DIR:-${ROOT}/configs}"
MAIN_BIN="${MAIN_BIN:-${ROOT}/bin/main}"
DAILY_JSON="${DAILY_JSON:-${SZE_DAILY_JSON:-}}"
if [[ -z "$DAILY_JSON" ]]; then
  DAILY_JSON="$(ls -1t "${CONFIG_DIR}"/config_sze_daily_*.json 2>/dev/null | head -n 1 || true)"
fi
MD_TEMPLATE="${MD_TEMPLATE:-${RUN_MAIN}/deepwin_sze_template.json}"
MD_JSON="${MD_JSON:-/dev/shm/deepwin_sze_daily.json}"
MAIN_CONF="${MAIN_CONF:-${RUN_MAIN}/main_sze_capture.conf}"

[[ -x "$MAIN_BIN" ]] || { echo "missing executable: $MAIN_BIN" >&2; exit 1; }
[[ -f "$DAILY_JSON" ]] || { echo "missing daily strategy config: $DAILY_JSON" >&2; exit 1; }
[[ -f "$MD_TEMPLATE" ]] || { echo "missing fixed MD template: $MD_TEMPLATE" >&2; exit 1; }
[[ -f "$MAIN_CONF" ]] || { echo "missing fixed capture conf: $MAIN_CONF" >&2; exit 1; }
[[ -f "$RUN_MAIN/libsze_md.so" ]] || { echo "missing libsze_md.so" >&2; exit 1; }

TRADING_DAY="$(python3 - "$DAILY_JSON" "$MD_TEMPLATE" "$MD_JSON" <<'PY'
import json, sys
daily=json.load(open(sys.argv[1]))
template=json.load(open(sys.argv[2]))
day=int(daily["trading_day"])
consumer=daily["sze_recovery_consumer"]
pipe=template["md"]["sze"]["recoverable_pipeline"]
pipe["trading_day"]=day
pipe["journal_directory"]=consumer["journal_directory"]
pipe["journal_prefix"]=consumer["journal_prefix"]
pipe["journal_segment_mb"]=consumer.get("journal_segment_mb", 4096)
pipe["journal_max_payload_bytes"]=consumer.get("journal_max_payload_bytes", 128)
pipe["shm_path"]=consumer["shm_path"]
pipe["malformed_diagnostic_path"]="{}/sze_all_{}_malformed.bin".format(pipe["journal_directory"], day)
with open(sys.argv[3], "w") as out:
    json.dump(template, out, separators=(",", ":"))
    out.write("\n")
print(day)
PY
)"
export TZ="${TZ:-Asia/Shanghai}"
mkdir -p "$RUN_MAIN/log" "$ROOT/data"
install -m 0644 "$MD_JSON" /opt/deepwin/master/etc/deepwin/deepwin.json
export LD_LIBRARY_PATH="$RUN_MAIN:${ROOT}/runtime_so/deepwin_core/lib/wingchun:${ROOT}/runtime_so/deepwin_core/lib/yijinjing:${ROOT}/runtime_so/third_party/wingchun:${ROOT}/runtime_so/third_party/boost:/opt/deepwin/master/lib/wingchun:/opt/deepwin/master/lib/yijinjing:/opt/deepwin/toolchain/boost-1.62.0/lib:${LD_LIBRARY_PATH:-}"
echo "starting fixed SZE capture trading_day=${TRADING_DAY}"
if [[ "${SZE_RECEIVER_MODE:-onload}" == "onload" ]]; then
  exec /usr/bin/onload "$MAIN_BIN" "$MAIN_CONF"
fi
exec "$MAIN_BIN" "$MAIN_CONF"
