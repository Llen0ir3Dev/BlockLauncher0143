#!/usr/bin/env bash
# Decompile classes.dex with JADX into a throw-away directory.
#
# JADX output contains reconstructed proprietary Java source.  It is analysis
# material only: do not commit it and do not ship it in NEXUS-MCPE-Linux.zip.
#
# Usage:
#   ./jadx_decompile.sh GAME.apk [OUTDIR]
#
# Environment:
#   JADX  path to the jadx launcher (default: jadx on PATH, then /opt/jadx/bin/jadx)
set -euo pipefail

if [[ $# -lt 1 ]]; then
    echo "usage: $0 GAME.apk [OUTDIR]" >&2
    exit 2
fi

APK="$1"
OUT="${2:-${TMPDIR:-/tmp}/nexus-jadx-out}"
JADX="${JADX:-}"
if [[ -z "$JADX" ]]; then
    if command -v jadx >/dev/null 2>&1; then
        JADX="$(command -v jadx)"
    elif [[ -x /opt/jadx/bin/jadx ]]; then
        JADX=/opt/jadx/bin/jadx
    else
        echo "jadx not found; install it or set JADX=/path/to/jadx" >&2
        exit 1
    fi
fi

WORK="$(mktemp -d "${TMPDIR:-/tmp}/nexus-jadx.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

unzip -q -o "$APK" 'classes*.dex' -d "$WORK"
mapfile -t DEX < <(find "$WORK" -maxdepth 1 -name 'classes*.dex' | sort)
if [[ ${#DEX[@]} -eq 0 ]]; then
    echo "no classes.dex in $APK" >&2
    exit 1
fi

mkdir -p "$OUT"
"$JADX" --no-replace-consts -d "$OUT" "${DEX[@]}"
echo "Decompiled ${#DEX[@]} dex file(s) into $OUT"
echo "NOTE: $OUT contains proprietary decompiled source; keep it out of releases."
