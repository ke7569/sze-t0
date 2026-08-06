#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

if [[ ! -x ./sz_fpga_udp_probe ]]; then
  ./build_sz_fpga_udp_probe.sh
fi

# Defaults are Dongguan machine receiving local SZ hard-core market data.
# From the FPGA manual:
#   SZ snapshot primary: 239.35.80.1:37100
#   SZ tick-by-tick primary: 239.35.81.1:37101
#   local hard-core data_ip: 11.11.11.11
GROUP="${GROUP:-239.35.80.1}"
PORT="${PORT:-37100}"
IFACE_IP="${IFACE_IP:-11.11.11.11}"
IFNAME="${IFNAME:-}"
RUN_SECONDS="${RUN_SECONDS:-30}"
BIND_PORT="${BIND_PORT:-0}"
PRINT_PACKETS="${PRINT_PACKETS:-20}"
HEX_LEN="${HEX_LEN:-128}"
RCVBUF_MB="${RCVBUF_MB:-64}"
USE_ONLOAD="${USE_ONLOAD:-0}"

cmd=(
  ./sz_fpga_udp_probe
  --group "$GROUP"
  --port "$PORT"
  --iface-ip "$IFACE_IP"
  --seconds "$RUN_SECONDS"
  --bind-port "$BIND_PORT"
  --print-packets "$PRINT_PACKETS"
  --hex-len "$HEX_LEN"
  --rcvbuf-mb "$RCVBUF_MB"
)

if [[ -n "$IFNAME" ]]; then
  cmd+=(--ifname "$IFNAME")
fi

if [[ "$USE_ONLOAD" == "1" ]]; then
  exec onload --profile=latency "${cmd[@]}" "$@"
fi

exec "${cmd[@]}" "$@"
