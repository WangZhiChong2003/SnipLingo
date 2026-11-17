#!/usr/bin/env bash
# Builds both M0 probe binaries and verifies that the AT-SPI one is free of foreign runtime paths.
#
# Why a script: an activated conda environment exports CXXFLAGS/LDFLAGS/CMAKE_PREFIX_PATH. Those inject
# a conda RPATH and conda include paths into *any* build, so a "system" binary silently ends up loading
# conda libdbus/libglib while libatspi comes from the system — which is exactly the state in which
# atspi_init() failed during the first real run. The AT-SPI probe is therefore configured with those
# variables removed, and the script fails loudly if a conda RPATH survives.
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
build="$root/build"
jobs=$(( $(nproc) - 2 )); [ "$jobs" -lt 1 ] && jobs=1

clean_env() {
    env -u LDFLAGS -u LDFLAGS_LD -u CXXFLAGS -u CFLAGS -u CPPFLAGS \
        -u CMAKE_ARGS -u CMAKE_PREFIX_PATH -u LIBRARY_PATH -u LD_RUN_PATH -u CPATH "$@"
}

echo "== building snip-probe (Qt: caps, x11, atspi dbus-events) =="
cmake -S "$here" -B "$build/m0" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      ${QT_PREFIX:+-DCMAKE_PREFIX_PATH="$QT_PREFIX"}
cmake --build "$build/m0" --parallel "$jobs"

echo "== building snip-atspi-probe (Qt-free, system runtime) =="
clean_env cmake -S "$here/atspi-only" -B "$build/m0-atspi" -G Ninja \
    -DCMAKE_CXX_COMPILER=/usr/bin/c++ -DCMAKE_C_COMPILER=/usr/bin/cc \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo
clean_env cmake --build "$build/m0-atspi" --parallel "$jobs"

echo "== verifying runtime provenance of snip-atspi-probe =="
binary="$build/m0-atspi/snip-atspi-probe"
if readelf -d "$binary" 2>/dev/null | grep -qE "RPATH|RUNPATH"; then
    echo "FAIL: $binary carries an RPATH/RUNPATH:" >&2
    readelf -d "$binary" | grep -E "RPATH|RUNPATH" >&2
    echo "Refusing to measure with a mixed runtime. Build from a shell without conda activation:" >&2
    echo "  conda deactivate && $0" >&2
    exit 1
fi
foreign=$(ldd "$binary" | awk '/libdbus-1|libglib-2.0|libgio-2.0|libatspi|libgobject/ {print $3}' \
          | grep -vE '^(/usr/|/lib/)' || true)
if [ -n "$foreign" ]; then
    echo "FAIL: foreign libraries are resolved:" >&2
    echo "$foreign" >&2
    echo "Refusing to measure with a mixed runtime." >&2
    exit 1
fi
echo "ok: no RPATH, all runtime libraries resolve from the system"

echo
echo "binaries:"
echo "  $build/m0/snip-probe"
echo "  $build/m0-atspi/snip-atspi-probe"
echo
echo "next: $build/m0-atspi/snip-atspi-probe diag"
