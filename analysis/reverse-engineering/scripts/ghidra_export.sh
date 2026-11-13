#!/usr/bin/env bash
# Run Ghidra headless analysis on the supplied native libraries.
#
# Ghidra 12.x requires JDK 21+.  Set JAVA_HOME to a JDK 21 installation before
# running.  Output is written next to the analysis tree and is excluded from
# release packaging.
#
# Usage:
#   ./ghidra_export.sh [LIBRARY.so ...]
#
# Defaults to the three ARMv7 libraries from the 0.14.3 APK.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
GHIDRA_HOME="${GHIDRA_HOME:-/opt/ghidra/ghidra_12.2_PUBLIC}"
OUT="${NEXUS_GHIDRA_OUT:-$ROOT/analysis/reverse-engineering/generated/ghidra}"
PROJECT="${NEXUS_GHIDRA_PROJECT:-$OUT/project}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [[ $# -gt 0 ]]; then
    LIBS=("$@")
else
    LIBS=(
        "$ROOT/original/libraries/armeabi-v7a/libminecraftpe.so"
        "$ROOT/original/libraries/armeabi-v7a/libfmod.so"
        "$ROOT/original/libraries/armeabi-v7a/libgnustl_shared.so"
    )
fi

if [[ ! -x "$GHIDRA_HOME/support/analyzeHeadless" ]]; then
    echo "Ghidra not found at $GHIDRA_HOME (set GHIDRA_HOME)" >&2
    exit 1
fi
if [[ -z "${JAVA_HOME:-}" ]]; then
    echo "JAVA_HOME is not set; Ghidra 12 needs a JDK 21" >&2
    exit 1
fi
if ! "$JAVA_HOME/bin/java" -version 2>&1 | grep -q 'version "21'; then
    echo "Ghidra 12 requires JDK 21; $JAVA_HOME does not look like JDK 21" >&2
    exit 1
fi

mkdir -p "$OUT" "$PROJECT"
for lib in "${LIBS[@]}"; do
    [[ -f "$lib" ]] || { echo "missing: $lib" >&2; exit 1; }
    base="$(basename "$lib" .so)"
    echo "Analyzing $lib"
    NEXUS_GHIDRA_OUT="$OUT/$base" \
    "$GHIDRA_HOME/support/analyzeHeadless" "$PROJECT" "$base" \
        -import "$lib" \
        -scriptPath "$SCRIPT_DIR" \
        -postScript ExportNexusInventory.java \
        -deleteProject \
        -analysisTimeoutPerFile 1800
done

echo "Ghidra exports under $OUT"
