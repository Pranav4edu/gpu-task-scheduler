#!/bin/sh
set -eu
PROJECT_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$PROJECT_ROOT/code"
CXX=${CXX:-c++}
command -v "$CXX" >/dev/null 2>&1 || {
    printf '%s\n' 'Error: install a C++17 compiler (c++ or set CXX to its executable).' >&2
    exit 1
}
mkdir -p build results
# Windows shells (Git Bash/MSYS2): link statically. Otherwise Windows can load an
# older libstdc++-6.dll shipped with Git instead of the compiler's own, which makes
# programs that use file streams crash. Linux and macOS are unaffected.
LINK_FLAGS=""
case "$(uname -s 2>/dev/null)" in
    MINGW*|MSYS*|CYGWIN*) LINK_FLAGS="-static" ;;
esac
# Build every current source and test file; no precompiled binaries or downloads.
"$CXX" -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror $LINK_FLAGS \
    -Iinclude src/*.cpp tests/*.cpp -o build/gpu_sim
if ./build/gpu_sim --self-test > results/test_summary.txt 2>&1; then
    cat results/test_summary.txt
else
    cat results/test_summary.txt >&2
    exit 1
fi
./build/gpu_sim
{
    printf 'Compiler: '
    "$CXX" --version
    printf '\nBuild flags: -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror %s\n' "$LINK_FLAGS"
    printf 'Host: '
    uname -sm
    printf 'Source version: '
    if test -f "$PROJECT_ROOT/SOURCE_VERSION.txt"; then
        cat "$PROJECT_ROOT/SOURCE_VERSION.txt"
    else
        git -C "$PROJECT_ROOT" rev-parse HEAD 2>/dev/null || printf 'unavailable\n'
    fi
    printf '\nInput checksums: POSIX cksum (CRC and bytes); not cryptographic hashes.\n'
    find data -type f -name '*.csv' -exec cksum {} \; | LC_ALL=C sort
} > results/run_manifest.txt
