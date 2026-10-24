#!/bin/bash
set -euo pipefail
native_root=$(cd "$(dirname "$0")/.." && pwd)
native_build=${ARGUS_BUILD_DIR:-"$native_root/.build/native-${SANITIZER:-none}"}
cmake -S "$native_root" -B "$native_build" -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON -DARGUS_SANITIZER="${SANITIZER:-none}"
cmake --build "$native_build" --parallel "${ARGUS_BUILD_JOBS:-4}"
ctest --test-dir "$native_build" --output-on-failure
