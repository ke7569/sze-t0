#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT}/build}"

"${ROOT}/verify_build_environment.sh"
cmake3 -S "$ROOT" -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE=Release \
  -DSZE_MARCH_NATIVE="${SZE_MARCH_NATIVE:-OFF}"
cmake3 --build "$BUILD_DIR" --target \
  t0_strategy_sze sze_md sze_recovery_status sze_recovery_verify \
  sze_recoverable_test sze_protocol_test sze_config_guard_test -- -j"${BUILD_JOBS:-4}"
(cd "$BUILD_DIR" && ctest3 --output-on-failure)

nm -C "$BUILD_DIR/libt0_strategy_sze.so" | \
  grep -F 'typeinfo for kungfu::wingchun::IWCStrategy' >/dev/null
if nm -C "$BUILD_DIR/libt0_strategy_sze.so" | \
   grep -F 'typeinfo for IWCStrategy' >/dev/null; then
  echo "invalid global IWCStrategy ABI" >&2
  exit 1
fi
if ldd "$BUILD_DIR/libt0_strategy_sze.so" | grep -F 'not found'; then
  exit 1
fi
sha256sum "$BUILD_DIR/libt0_strategy_sze.so" "$BUILD_DIR/libsze_md.so"
