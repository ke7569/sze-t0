#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
runtime_root=${T0_DEEPWIN_RUNTIME_ROOT:-/home/t0/runtime_so}

fail() {
    echo "build-env: FAIL: $*" >&2
    exit 1
}

[[ -f /.dockerenv ]] || fail "not running inside a Docker container"
[[ -r /etc/centos-release ]] || fail "missing /etc/centos-release"
centos_release=$(< /etc/centos-release)
[[ "${centos_release}" == *"7.9.2009"* ]] || fail "expected CentOS 7.9.2009, got: ${centos_release}"

gcc_version=$(gcc -dumpversion 2>/dev/null || true)
gxx_version=$(g++ -dumpversion 2>/dev/null || true)
[[ "${gcc_version}" == "4.8.5" ]] || fail "expected gcc 4.8.5, got: ${gcc_version:-missing}"
[[ "${gxx_version}" == "4.8.5" ]] || fail "expected g++ 4.8.5, got: ${gxx_version:-missing}"

cmake_bin=${CMAKE_BIN:-}
if [[ -z "${cmake_bin}" ]]; then
    if command -v cmake3 >/dev/null 2>&1; then
        cmake_bin=$(command -v cmake3)
    elif command -v cmake >/dev/null 2>&1; then
        cmake_bin=$(command -v cmake)
    else
        fail "cmake is not installed"
    fi
fi
cmake_version=$(${cmake_bin} --version | awk 'NR == 1 {print $3}')

[[ -x /usr/bin/python3.6m ]] || fail "missing /usr/bin/python3.6m"
[[ -d /usr/include/python3.6m ]] || fail "missing /usr/include/python3.6m"
[[ -f /usr/lib64/libpython3.6m.so.1.0 ]] || fail "missing /usr/lib64/libpython3.6m.so.1.0"

required_paths=(
    /opt/deepwin/master/include/IControlCenter.h
    /opt/deepwin/master/include/IWCStrategy.h
    /opt/deepwin/master/include/WCStrategyUtil.h
    /opt/deepwin/master/include/WCDataWrapper.h
    /opt/deepwin/master/include/KfLog.h
    "${runtime_root}/deepwin_core/lib/wingchun/libwingchunstrategy.so"
    "${runtime_root}/deepwin_core/lib/yijinjing/libjournal.so"
    "${runtime_root}/deepwin_core/lib/yijinjing/libkflog.so"
    "${runtime_root}/third_party/wingchun/libpython3.6m.so.1.0"
    "${runtime_root}/third_party/boost/libboost_system.so.1.62.0"
)
for path in "${required_paths[@]}"; do
    [[ -e "${path}" ]] || fail "missing required Deepwin artifact: ${path}"
done

glibc_version=$(ldd --version | awk 'NR == 1 {print $NF}')
python_version=$(/usr/bin/python3.6m --version 2>&1 | awk '{print $2}')
container_id=$(hostname)

echo "build-env: PASS"
echo "container=${container_id} os=centos-7.9.2009 gcc=${gcc_version} g++=${gxx_version} cmake=${cmake_version} python=${python_version} glibc=${glibc_version}"
echo "repo=${repo_dir} runtime_root=${runtime_root}"
