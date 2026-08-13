#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_DIR="${ROOT}/src/t0-main/build/sse-t0-native"
OUT_DIR="${ROOT}/src/t0-main/build/configs/sse-live-capture"
STAMP="${SSE_PACKAGE_STAMP:-$(date -u +%Y%m%d_%H%M%S)}"
STAGE="${OUT_DIR}/sse-live-capture-${STAMP}"
ARCHIVE="${OUT_DIR}/sse-live-capture-${STAMP}.tar.gz"

[[ -x "${BUILD_DIR}/sse_udp_observer" ]] || {
  echo "missing ${BUILD_DIR}/sse_udp_observer; build it first in the CentOS 7.9 builder" >&2
  exit 3
}
mkdir -p "${OUT_DIR}"
if [[ -e "${STAGE}" || -e "${ARCHIVE}" ]]; then
  echo "package output already exists: ${STAGE} or ${ARCHIVE}" >&2
  exit 4
fi
mkdir -p "${STAGE}/bin" "${STAGE}/config" "${STAGE}/docs"
cp "${BUILD_DIR}/sse_udp_observer" "${STAGE}/bin/"
cp "${ROOT}/sse-t0/config/sse_udp_observer.example.json" "${STAGE}/config/"
cp "${ROOT}/sse-t0/config/sse_udp_observer_dongguan.example.json" "${STAGE}/config/"
cp "${ROOT}/sse-t0/docs/LV1_ENDPOINTS.md" "${STAGE}/docs/"
cp "${ROOT}/sse-t0/deploy/start_sse_capture.sh" "${STAGE}/"
cp "${ROOT}/sse-t0/deploy/stop_sse_capture.sh" "${STAGE}/"
cp "${ROOT}/sse-t0/deploy/analyze_sse_capture.py" "${STAGE}/"
cp "${ROOT}/sse-t0/deploy/README.md" "${STAGE}/README.md"
chmod 0755 "${STAGE}/bin/sse_udp_observer" "${STAGE}/"*.sh "${STAGE}/analyze_sse_capture.py"
{
  echo "package=sse-live-capture"
  echo "created_utc=${STAMP}"
  echo "git_commit=$(cd "${ROOT}" && git rev-parse HEAD)"
  echo "git_branch=$(cd "${ROOT}" && git symbolic-ref --short HEAD 2>/dev/null || true)"
  echo "builder=centos-7.9-gcc-4.8.5"
  echo "scope=raw SSE snapshot/tick UDP capture only"
} > "${STAGE}/PACKAGE_INFO"
(cd "${STAGE}" && find . -type f ! -path './SHA256SUMS' -print0 | sort -z | xargs -0 sha256sum) > "${STAGE}/SHA256SUMS"
tar -C "${OUT_DIR}" -czf "${ARCHIVE}" "$(basename "${STAGE}")"
sha256sum "${ARCHIVE}" > "${ARCHIVE}.sha256"
echo "archive=${ARCHIVE}"
echo "sha256=$(awk '{print $1}' "${ARCHIVE}.sha256")"
