#!/usr/bin/env bash
# Build the distributable source archive with executables.
#
# The archive intentionally excludes:
#   original/        extracted proprietary game files
#   external/        cloned third-party research repositories
#   *.apk            user-supplied game builds
#   build*/          local build trees and caches
#   analysis/symbols raw symbol dumps derived from the proprietary binary
#   .cache/          local build trees and caches
#   .idea/           IDE project files
#   .vscode/         IDE project files
#   *.log            log files
#
# It includes:
#   All source code and headers
#   Build configurations (CMakeLists.txt, CMakePresets.json)
#   GitHub Actions workflows
#   Tests
#   Documentation
#   Dependency manifests
#   Required scripts
#   Pre-built executables (nexus, nexus-runtime, nexus-launcher)

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NAME="NEXUS-MCPE-Linux"
OUT="${OUT:-$ROOT/$NAME.zip}"
VERIFY=1

for arg in "$@"; do
    case "$arg" in
        --no-verify) VERIFY=0 ;;
    esac
done
while [[ $# -gt 0 ]]; do
    case $1 in
        --out)
            [[ $# -ge 2 ]] || { echo "--out requires a path" >&2; exit 2; }
            OUT="$2"
            shift 2
            ;;
        *) shift ;;
    esac
done

cd "$ROOT"
rm -f "$OUT"
echo "Creating $OUT"

# Build executables if they don't exist
if [[ ! -f "build/nexus" || ! -f "build/nexus-runtime" || ! -f "build/nexus-launcher" ]]; then
    echo "Building executables..."
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DNEXUS_BUILD_TESTS=ON -DNEXUS_BUILD_RUNTIME=ON -DNEXUS_BUILD_LAUNCHER=ON
    cmake --build build -j$(nproc 2>/dev/null || echo 4)
fi

# Create a clean staging directory
STAGING="$(mktemp -d "${TMPDIR:-/tmp}/nexus-stage.XXXXXX")"
trap 'rm -rf "$STAGING"' EXIT

# Copy source tree (excluding build artifacts and proprietary files)
rsync -a \
    --exclude='.git' \
    --exclude='build' \
    --exclude='build-*' \
    --exclude='out' \
    --exclude='.cache' \
    --exclude='original' \
    --exclude='external' \
    --exclude='*.apk' \
    --exclude='*.zip' \
    --exclude='*.log' \
    --exclude='analysis/symbols' \
    --exclude='.idea' \
    --exclude='.vscode' \
    --exclude='*.sha256' \
    "$ROOT/" "$STAGING/"

# Copy executables to bin/ directory in staging
mkdir -p "$STAGING/bin"
cp "$ROOT/build/nexus" "$STAGING/bin/"
cp "$ROOT/build/nexus-runtime" "$STAGING/bin/"
cp "$ROOT/build/nexus-launcher" "$STAGING/bin/"

# Create the archive from staging directory
cd "$STAGING"
zip -r -q "$OUT" .

# Verify no proprietary or build files leaked
if unzip -l "$OUT" | grep -Eq '(^|/)(original|external|build[^/]*)/|\.apk$'; then
    echo "ERROR: proprietary or build files leaked into the archive" >&2
    unzip -l "$OUT" | grep -E '(^|/)(original|external|build[^/]*)/|\.apk$' >&2 || true
    rm -f "$OUT"
    exit 1
fi

SHA="$(sha256sum "$OUT" | awk '{print $1}')"
printf '%s  %s\n' "$SHA" "$(basename "$OUT")" >"$OUT.sha256"
echo "SHA256: $SHA"

if [[ "$VERIFY" == "1" ]]; then
    TMP="$(mktemp -d "${TMPDIR:-/tmp}/nexus-package.XXXXXX")"
    trap 'rm -rf "$TMP"' EXIT
    mkdir -p "$TMP/src"
    unzip -q "$OUT" -d "$TMP/src"

    echo "Verifying clean extraction in $TMP/src"
    cmake -S "$TMP/src" -B "$TMP/build" -G Ninja \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DNEXUS_BUILD_TESTS=ON \
        -DNEXUS_BUILD_RUNTIME=ON \
        -DNEXUS_BUILD_LAUNCHER=ON >/dev/null
    cmake --build "$TMP/build" -j"$(nproc 2>/dev/null || echo 4)" >/dev/null
    ctest --test-dir "$TMP/build" --output-on-failure >/dev/null
    echo "Clean extraction built and tested successfully"
fi

echo "Done: $OUT"