#!/usr/bin/env bash
set -euo pipefail

source /etc/os-release
[[ "${ID:-}" == centos && "${VERSION_ID:-}" == 7* ]] || {
  echo "expected CentOS 7.x" >&2
  exit 1
}
[[ "$(gcc -dumpversion)" == 4.8.5 ]] || {
  echo "expected gcc 4.8.5" >&2
  exit 1
}
[[ "$(g++ -dumpversion)" == 4.8.5 ]] || {
  echo "expected g++ 4.8.5" >&2
  exit 1
}
ldd --version 2>&1 | grep -F '2.17' >/dev/null
command -v cmake3 >/dev/null
for path in \
  toolchain/deepwin_include/IWCStrategy.h \
  toolchain/boost_include/boost/version.hpp \
  toolchain/python_include/Python.h \
  runtime_so/deepwin_core/lib/wingchun/libwingchunstrategy.so \
  runtime_so/deepwin_core/lib/wingchun/libwingchunmd.so; do
  [[ -e "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/$path" ]] || {
    echo "missing bundled dependency: $path" >&2
    exit 1
  }
done
echo "SZE build environment: PASS"
