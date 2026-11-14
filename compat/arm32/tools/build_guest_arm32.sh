#!/usr/bin/env bash
#
# Cross-compile the ARM32 guest components of NEXUS-MCPE.
#
# The host build (nexus, nexus-runtime, ...) runs on the target machine
# (Ubuntu 22.04 x86_64).  The *guest* code below is ARM32 (armeabi-v7a) and
# is executed by QEMU user-mode.  It must be cross-compiled with the Android
# NDK, independently of the host build.
#
# Works on both supported build hosts:
#   * x86_64  : uses the NDK's prebuilt clang (GitHub Actions ubuntu-22.04).
#   * aarch64 : uses the system clang with --target/--sysroot (dev machine).
#
# Outputs into --out (default: build/guest-arm32):
#   guest_smoke_static : static bionic smoke test (portable, TLS-aligned)
#   jni_host_dynamic   : JNI_OnLoad harness (dev only; needs a bionic linker)
#
#   jni_host_dynamic is intentionally *not* shipped: on the user's PC there is
#   no Android dynamic linker.  The shipped path is the static guest plus the
#   in-tree ARM32 ELF loader (see compat/arm32/guest/).
#
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"          # compat/arm32
NDK="${ANDROID_NDK:-${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}}"
OUT="${ROOT}/../../build/guest-arm32"
API=21
TRIPLE="armv7a-linux-androideabi${API}"

usage() {
    echo "usage: $0 [--ndk <path>] [--out <dir>] [--static-only]" >&2
    exit 2
}

STATIC_ONLY=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --ndk)      NDK="$2"; shift 2 ;;
        --out)      OUT="$2"; shift 2 ;;
        --static-only) STATIC_ONLY=1; shift ;;
        -h|--help)  usage ;;
        *) echo "unknown argument: $1" >&2; usage ;;
    esac
done

if [[ -z "$NDK" ]]; then
    for candidate in /opt/android-ndk-r25c /opt/android-ndk "${HOME}/android-ndk"; do
        if [[ -d "$candidate" ]]; then NDK="$candidate"; break; fi
    done
fi
if [[ -z "$NDK" || ! -d "$NDK" ]]; then
    echo "error: Android NDK not found (use --ndk or set ANDROID_NDK_HOME)" >&2
    exit 1
fi

SYSROOT="${NDK}/toolchains/llvm/prebuilt/linux-x86_64/sysroot"
if [[ ! -d "$SYSROOT" ]]; then
    echo "error: NDK sysroot not found at $SYSROOT" >&2
    exit 1
fi

HOST_ARCH="$(uname -m)"
NDK_CLANG="${NDK}/toolchains/llvm/prebuilt/linux-x86_64/bin/clang"
if [[ "$HOST_ARCH" == "x86_64" && -x "$NDK_CLANG" ]]; then
    CC=( "$NDK_CLANG" )
    AR_BIN="${NDK}/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-ar"
else
    # Cross-arch dev host (e.g. aarch64 Termux/PRoot): the NDK prebuilt
    # compiler cannot execute here, but the system LLVM can target ARM.
    command -v clang >/dev/null 2>&1 || { echo "error: no clang on PATH" >&2; exit 1; }
    CC=( clang )
    AR_BIN="$(command -v llvm-ar || command -v ar)"
fi
CC=( "${CC[@]}" "--target=${TRIPLE}" "--sysroot=${SYSROOT}" )
[[ -n "${AR_BIN:-}" && -x "$AR_BIN" ]] || { echo "error: no ar/llvm-ar found" >&2; exit 1; }

mkdir -p "$OUT/lib"

# The NDK's compiler driver unconditionally passes -lssp_nonshared, but the
# NDK does not ship that archive.  An empty archive is sufficient because our
# guest code does not use stack-protector guard symbols.
if [[ ! -f "$OUT/lib/libssp_nonshared.a" ]]; then
    "$AR_BIN" rcs "$OUT/lib/libssp_nonshared.a"
fi

echo "== NEXUS ARM32 guest build =="
echo "  host arch : $HOST_ARCH"
echo "  NDK       : $NDK"
echo "  target    : $TRIPLE"
echo "  output    : $OUT"

# 1. Portable static guest (the shipping model).
#    -static produces a bionic executable; patch_tls_align makes it runnable
#    under qemu-arm on any host.
"${CC[@]}" -O2 -fno-stack-protector -static -L"$OUT/lib" \
    "${ROOT}/guest/guest_smoke.c" -o "${OUT}/guest_smoke_static"
python3 "${HERE}/patch_tls_align.py" "${OUT}/guest_smoke_static"

# 1b. In-tree ARM32 ELF loader (shipping guest tool).  It parses, maps and
#     relocates Android ARM32 shared objects without a device linker.
"${CC[@]}" -O2 -fno-stack-protector -static -L"$OUT/lib" \
    "${ROOT}/guest/elf_loader.c" -o "${OUT}/nexus_elf_loader"
python3 "${HERE}/patch_tls_align.py" "${OUT}/nexus_elf_loader"

# 2. JNI_OnLoad harness (development only).  This one is dynamically linked
#    and therefore needs a bionic linker at run time; it is used on the dev
#    machine only and must not be bundled into the AppImage.
if [[ "$STATIC_ONLY" -eq 0 ]]; then
    "${CC[@]}" -O2 -fPIE -pie -L"$OUT/lib" \
        "${ROOT}/guest/jni_host.c" -o "${OUT}/jni_host_dynamic"
else
    # The dynamic JNI harness needs an Android bionic linker and is a
    # development-only tool. Never leave it in a shippable guest bundle.
    rm -f "${OUT}/jni_host_dynamic"
fi

echo "== verification =="
for f in "${OUT}"/*; do
    [[ -f "$f" ]] || continue
    if file -b "$f" | grep -q "ELF"; then
        file "$f"
    fi
done

echo "== done =="
