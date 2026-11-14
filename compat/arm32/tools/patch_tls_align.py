#!/usr/bin/env python3
"""Patch the PT_TLS alignment of an ARM32 bionic ELF for QEMU user-mode.

QEMU refuses to load statically linked Android/bionic executables whose
PT_TLS segment is under-aligned (bionic requires 32-byte alignment; older
NDK linkers emit 8).  Aligning the program header is safe and makes the
binary runnable under `qemu-arm` on any host.

This operates on a copy in place.  It is idempotent.
"""
import struct
import sys


def patch(path: str) -> int:
    with open(path, "rb") as f:
        data = bytearray(f.read())
    if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 1:
        raise SystemExit(f"{path}: not an ELF32 little-endian file")

    e_phoff = struct.unpack_from("<I", data, 28)[0]
    e_phentsize = struct.unpack_from("<H", data, 42)[0]
    e_phnum = struct.unpack_from("<H", data, 44)[0]

    patched = 0
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        if struct.unpack_from("<I", data, off)[0] != 7:  # PT_TLS
            continue
        align = struct.unpack_from("<I", data, off + 28)[0]
        if align < 32:
            struct.pack_into("<I", data, off + 28, 32)
            patched += 1

    if patched:
        with open(path, "wb") as f:
            f.write(data)
    return patched


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit(f"usage: {sys.argv[0]} <elf32-arm-binary>")
    n = patch(sys.argv[1])
    print(f"PT_TLS alignment patched in {n} segment(s): {sys.argv[1]}")
