#!/usr/bin/env python3
"""Summarise a Dalvik `classes.dex` without third-party dependencies.

Reads the DEX header, string/type/proto/field/method/class tables, and
optionally prints the class descriptors.  This is a triage tool: use JADX for
the actual source reconstruction.

Usage:
    python3 dex_summary.py classes.dex [OUTDIR]
"""

from __future__ import annotations

import json
import os
import struct
import sys
from collections import Counter


def uleb128(data: bytes, pos: int):
    result = 0
    shift = 0
    while True:
        b = data[pos]
        pos += 1
        result |= (b & 0x7F) << shift
        if not b & 0x80:
            return result, pos
        shift += 7
        if shift > 35:
            raise ValueError("invalid uleb128")


def read_string(data: bytes, pos: int) -> str:
    strlen, pos = uleb128(data, pos)
    end = data.index(b"\0", pos)
    raw = data[pos:end]
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError:
        return raw.decode("utf-8", "replace")


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    dex, outdir = sys.argv[1], (sys.argv[2] if len(sys.argv) > 2 else ".")
    data = open(dex, "rb").read()
    if len(data) < 0x70 or data[:4] != b"dex\n":
        print("not a DEX file", file=sys.stderr)
        return 1

    checksum = struct.unpack_from("<I", data, 8)[0]
    signature = data[12:32]
    file_size, header_size = struct.unpack_from("<II", data, 32)
    string_ids_size, string_ids_off = struct.unpack_from("<II", data, 0x38)
    type_ids_size, type_ids_off = struct.unpack_from("<II", data, 0x40)
    proto_ids_size, proto_ids_off = struct.unpack_from("<II", data, 0x48)
    field_ids_size, field_ids_off = struct.unpack_from("<II", data, 0x50)
    method_ids_size, method_ids_off = struct.unpack_from("<II", data, 0x58)
    class_defs_size, class_defs_off = struct.unpack_from("<II", data, 0x60)

    strings = []
    for i in range(string_ids_size):
        off = struct.unpack_from("<I", data, string_ids_off + i * 4)[0]
        strings.append(read_string(data, off))

    type_descriptors = []
    for i in range(type_ids_size):
        idx = struct.unpack_from("<I", data, type_ids_off + i * 4)[0]
        type_descriptors.append(strings[idx] if idx < len(strings) else "")

    classes = []
    for i in range(class_defs_size):
        base = class_defs_off + i * 32
        class_idx, _access, super_idx, _interfaces, _src, _annotations, _class_data, _static = struct.unpack_from(
            "<IIIIIIII", data, base
        )
        classes.append(
            {
                "descriptor": type_descriptors[class_idx] if class_idx < len(type_descriptors) else "",
                "superclass": type_descriptors[super_idx] if super_idx < len(type_descriptors) else "",
            }
        )

    packages = Counter()
    for c in classes:
        desc = c["descriptor"].lstrip("L")
        packages[".".join(desc.split("/")[:-1]) if "/" in desc else "<default>"] += 1

    summary = {
        "path": os.path.abspath(dex),
        "size": len(data),
        "header_size": header_size,
        "file_size": file_size,
        "checksum": f"{checksum:08x}",
        "signature": signature.hex(),
        "string_ids": string_ids_size,
        "type_ids": type_ids_size,
        "proto_ids": proto_ids_size,
        "field_ids": field_ids_size,
        "method_ids": method_ids_size,
        "class_defs": class_defs_size,
        "packages": packages.most_common(40),
        "classes": classes,
    }

    os.makedirs(outdir, exist_ok=True)
    with open(os.path.join(outdir, "dex_summary.json"), "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=2)
    with open(os.path.join(outdir, "dex_classes.txt"), "w", encoding="utf-8") as f:
        for c in sorted(classes, key=lambda c: c["descriptor"]):
            f.write(f"{c['descriptor']}\t{c['superclass']}\n")

    print(f"DEX: {summary['path']}")
    print(f"strings={string_ids_size} types={type_ids_size} protos={proto_ids_size} "
          f"fields={field_ids_size} methods={method_ids_size} classes={class_defs_size}")
    print("Top packages:")
    for pkg, n in packages.most_common(10):
        print(f"  {n:4d}  {pkg}")
    print(f"Output: {outdir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
