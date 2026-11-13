#!/usr/bin/env bash
# Build (unless --no-build) and run the full test suite.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"

if [[ "${1:-}" == "--no-build" ]]; then
    shift
else
    BUILD_DIR="$BUILD_DIR" "$ROOT/scripts/build.sh"
fi

ctest --test-dir "$BUILD_DIR" --output-on-failure "$@"
