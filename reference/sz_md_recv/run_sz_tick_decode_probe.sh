#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

if [[ ! -x ./sz_tick_decode_probe ]]; then
  gcc -std=gnu11 -O2 -Wall -Wextra -Wformat=2 -o sz_tick_decode_probe sz_tick_decode_probe.c
fi

GROUP="${GROUP:-239.35.81.1}"
PORT="${PORT:-37101}"
IFACE_IP="${IFACE_IP:-11.11.11.11}"
IFNAME="${IFNAME:-hqh-p1-k2}"
RUN_SECONDS="${RUN_SECONDS:-10}"
MAX_ROWS="${MAX_ROWS:-200}"
CSV="${CSV:-}"
USE_ONLOAD="${USE_ONLOAD:-1}"

cmd=(
  ./sz_tick_decode_probe
  --group "$GROUP"
  --port "$PORT"
  --iface-ip "$IFACE_IP"
  --ifname "$IFNAME"
  --seconds "$RUN_SECONDS"
  --max-rows "$MAX_ROWS"
)

if [[ -n "$CSV" ]]; then
  cmd+=(--csv "$CSV")
fi

if [[ "$USE_ONLOAD" == "1" ]]; then
  exec onload --profile=latency "${cmd[@]}" "$@"
fi

exec "${cmd[@]}" "$@"
