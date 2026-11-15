#!/usr/bin/env bash
#
# End-to-end graphics bridge test.
#
#   host (x86_64, real Mesa GLES2)  <-- Unix socket -->  guest (ARM32, qemu-arm)
#
# The guest drives clear + shader + VBO + draw and checks the pixels it reads
# back.  The host independently writes the last presented frame as a PNG, so a
# successful run proves both sides really rendered.
#
# usage: run_gfx_smoke.sh HOST_BINARY QEMU GUEST_SMOKE [PNG_OUT] [--window]
set -euo pipefail

HOST_BIN="${1:?host binary}"
QEMU="${2:?qemu-arm}"
GUEST_BIN="${3:?guest smoke binary}"
PNG_OUT="${4:-}"
MODE="${5:-}"
if [[ -n "${MODE}" && "${MODE}" != "--window" ]]; then
    echo "usage: $0 HOST QEMU GUEST [PNG_OUT] [--window]" >&2
    exit 2
fi

WORK="$(mktemp -d "${TMPDIR:-/tmp}/nexus-gfx.XXXXXX")"
SOCK="${WORK}/gfx.sock"

cleanup() {
    if [[ -n "${HOST_PID:-}" ]]; then
        kill "${HOST_PID}" 2>/dev/null || true
        wait "${HOST_PID}" 2>/dev/null || true
    fi
    rm -rf "${WORK}"
}
trap cleanup EXIT

if [[ "${MODE}" == "--window" ]]; then
    HOST_ARGS=( --socket "${SOCK}" --window --size 320x240 )
else
    HOST_ARGS=( --socket "${SOCK}" --headless --size 320x240 )
fi
if [[ -n "${PNG_OUT}" ]]; then
    HOST_ARGS+=( --dump "${PNG_OUT}" )
fi

"${HOST_BIN}" "${HOST_ARGS[@]}" >"${WORK}/host.log" 2>&1 &
HOST_PID=$!

# Wait for the listening socket instead of sleeping a fixed amount.
for _ in $(seq 1 100); do
    [[ -S "${SOCK}" ]] && break
    if ! kill -0 "${HOST_PID}" 2>/dev/null; then
        echo "gfx smoke: host exited during startup" >&2
        cat "${WORK}/host.log" >&2
        exit 1
    fi
    sleep 0.05
done
if [[ ! -S "${SOCK}" ]]; then
    echo "gfx smoke: host did not create ${SOCK}" >&2
    cat "${WORK}/host.log" >&2
    exit 1
fi

set +e
"${QEMU}" "${GUEST_BIN}" "${SOCK}" >"${WORK}/guest.log" 2>&1
GUEST_RC=$?
set -e

wait "${HOST_PID}" 2>/dev/null || true
HOST_PID=""

cat "${WORK}/host.log"
cat "${WORK}/guest.log"

if [[ ${GUEST_RC} -ne 0 ]]; then
    echo "gfx smoke: guest failed with status ${GUEST_RC}" >&2
    exit ${GUEST_RC}
fi
if ! grep -q "gfx_smoke: PASS" "${WORK}/guest.log"; then
    echo "gfx smoke: guest did not report PASS" >&2
    exit 1
fi
if [[ -n "${PNG_OUT}" && ! -s "${PNG_OUT}" ]]; then
    echo "gfx smoke: host did not write ${PNG_OUT}" >&2
    exit 1
fi

echo "gfx smoke: PASS (host <-> ARM32 guest frame verified)"
