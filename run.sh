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
# Build every current source and test file; no precompiled binaries or downloads.
"$CXX" -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror \
    -Iinclude src/*.cpp tests/*.cpp -o build/gpu_sim
if ./build/gpu_sim --self-test > results/test_summary.txt 2>&1; then
    cat results/test_summary.txt
else
    cat results/test_summary.txt >&2
    exit 1
fi
./build/gpu_sim --foundation-demo
./build/gpu_sim --engine-checkpoint
