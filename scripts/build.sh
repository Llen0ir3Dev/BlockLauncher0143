#!/usr/bin/env bash
# Configure and build NEXUS-MCPE.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
BUILD_TYPE="${BUILD_TYPE:-RelWithDebInfo}"
GENERATOR="${GENERATOR:-Ninja}"

cmake -S "$ROOT" -B "$BUILD_DIR" -G "$GENERATOR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DNEXUS_BUILD_TESTS=ON \
    -DNEXUS_BUILD_RUNTIME=ON \
    -DNEXUS_BUILD_LAUNCHER=ON \
    "$@"

cmake --build "$BUILD_DIR" -j"$(nproc 2>/dev/null || echo 4)"
echo "Built in $BUILD_DIR"
