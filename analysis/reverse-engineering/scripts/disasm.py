#!/usr/bin/env python3
"""Annotated disassembly for ARM/Thumb ELF functions (read-only).

Features used by the NEXUS reverse-engineering notes:
  * disassembles a symbol or an address range with Capstone;
  * resolves direct BL/BLX targets to the nearest dynamic symbol;
  * resolves Thumb PC-relative literal loads (`ldr rX, [pc, #imm]`) and prints
    the pointed-to ASCII string when the literal target lands in a mapped
    section (this is how asset paths and Java signatures are recovered);
  * detects the PLT and prints imported symbol names next to `blx` calls.

Usage:
    python3 disasm.py LIBRARY.so --symbol android_main
    python3 disasm.py LIBRARY.so --address 0x2caa25 --length 0x200
    python3 disasm.py LIBRARY.so --symbol 'Java_com_mojang_minecraftpe_MainActivity_nativeRegisterThis'
    python3 disasm.py LIBRARY.so --list 'MinecraftClient::'

Capstone 5 is required: pip install capstone
"""

from __future__ import annotations

import argparse
import bisect
import subprocess
import sys
from dataclasses import dataclass

try:
    from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB, CS_MODE_LITTLE_ENDIAN
    from capstone.arm import ARM_OP_IMM, ARM_OP_MEM, ARM_REG_PC
    from elftools.elf.elffile import ELFFile
    from elftools.elf.sections import SymbolTableSection
except ImportError:
    sys.exit("capstone and pyelftools are required: pip install capstone pyelftools")


def demangle_all(names: list[str]) -> list[str]:
    if not names:
        return []
    try:
        out = subprocess.run(
            ["c++filt"], input="\n".join(names), text=True,
            capture_output=True, check=False,
        ).stdout.splitlines()
        return out if len(out) == len(names) else names
    except FileNotFoundError:
        return names


@dataclass
class Sym:
    name: str
    addr: int
    size: int
    kind: str


class Image:
    def __init__(self, path: str):
        self.path = path
        self.data = open(path, "rb").read()
        with open(path, "rb") as fh:
            self.elf = ELFFile(fh)
            self.sections = [
                (s["sh_addr"], s["sh_addr"] + s["sh_size"], s["sh_offset"], s.name, s["sh_type"])
                for s in self.elf.iter_sections()
            ]
            self.sections.sort()
            self.symtab = self.elf.get_section_by_name(".dynsym")
            raw = []
            if isinstance(self.symtab, SymbolTableSection):
                for s in self.symtab.iter_symbols():
                    if s.name and s["st_value"] and s["st_info"]["type"] == "FUNC":
                        raw.append(Sym(s.name, s["st_value"] & ~1, s["st_size"], "FUNC"))
            demangled = demangle_all([s.name for s in raw])
            for s, d in zip(raw, demangled):
                s.name = d
            self.symbols = sorted(raw, key=lambda s: s.addr)
            self.sym_addrs = [s.addr for s in self.symbols]
            # Imported names are useful for calls that go through the PLT.
            self.plt = {}
            plt = self.elf.get_section_by_name(".plt")
            relplt = self.elf.get_section_by_name(".rel.plt")
            if plt is not None and relplt is not None:
                try:
                    dynsym = self.elf.get_section_by_name(".dynsym")
                    base = plt["sh_addr"]
                    # ARM PLT: 4-byte header then 12-byte entries (Android/gold)
                    for i, rel in enumerate(relplt.iter_relocations()):
                        name = dynsym.get_symbol(rel["r_info_sym"]).name
                        self.plt[base + 4 + i * 12] = name
                except Exception:
                    self.plt = {}

    def section_for(self, addr: int):
        for start, end, off, name, typ in self.sections:
            if start <= addr < end and typ != "SHT_NOBITS":
                return start, end, off, name
        return None

    def bytes_at(self, addr: int, n: int) -> bytes:
        sec = self.section_for(addr)
        if not sec:
            return b""
        start, end, off, _ = sec
        delta = addr - start
        return self.data[off + delta: off + delta + n]

    def cstring_at(self, addr: int) -> str | None:
        if self.section_for(addr) is None:
            return None
        n = 0
        out = bytearray()
        while n < 200:
            b = self.bytes_at(addr + n, 1)
            if not b or b == b"\0":
                break
            if not (0x20 <= b[0] < 0x7f):
                return None
            out += b
            n += 1
        return out.decode("ascii") if len(out) >= 3 else None

    def nearest_symbol(self, addr: int, max_distance: int = 0x4000) -> str | None:
        i = bisect.bisect_right(self.sym_addrs, addr) - 1
        if i < 0:
            return None
        s = self.symbols[i]
        if addr - s.addr <= max(0, s.size) or addr - s.addr <= max_distance:
            return f"{s.name}+0x{addr - s.addr:x}" if addr != s.addr else s.name
        return None

    def find_symbol(self, query: str) -> list[Sym]:
        exact = [s for s in self.symbols if s.name == query]
        if exact:
            return exact
        return [s for s in self.symbols if query in s.name]

    def find_by_raw(self, query: str) -> list[Sym]:
        """Search raw (mangled) names as well."""
        res = []
        if isinstance(self.symtab, SymbolTableSection):
            for s in self.symtab.iter_symbols():
                if s.name and query in s.name:
                    res.append(Sym(s.name, s["st_value"] & ~1, s["st_size"], "FUNC"))
        return res


def disassemble(img: Image, addr: int, length: int) -> None:
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_LITTLE_ENDIAN)
    md.detail = True
    # find section end so we never run into unrelated data
    sec = img.section_for(addr)
    if not sec:
        print(f"address 0x{addr:x} is not inside a mapped section")
        return
    end = min(addr + length, sec[1])
    code = img.bytes_at(addr, end - addr)
    sym = img.nearest_symbol(addr)
    print(f"# {img.path}")
    print(f"# range 0x{addr:08x}..0x{end:08x}  ({sym or 'unknown'})")
    print(f"# {'address':10s} {'bytes':16s} instruction")
    for insn in md.disasm(code, addr):
        ann = ""
        if insn.mnemonic in ("bl", "blx", "b", "bx") and insn.operands:
            op = insn.operands[0]
            if op.type == ARM_OP_IMM:
                target = op.imm & 0xffffffff
                name = img.nearest_symbol(target)
                ann = f"  ; -> 0x{target:08x} {name or ''}"
                if insn.mnemonic == "blx" and target in img.plt:
                    ann += f" [PLT {img.plt[target]}]"
        if insn.mnemonic.startswith("ldr") and "[pc" in insn.op_str:
            # Thumb: PC is instruction address + 4, aligned to 4.
            pc = (insn.address + 4) & ~3
            # Capstone gives the resolved absolute address in operand.mem.disp
            # for PC-relative addressing; use it directly.
            op = insn.operands[1] if len(insn.operands) > 1 else None
            target = None
            if op is not None and op.type == ARM_OP_MEM:
                target = op.mem.disp & 0xffffffff
            if target is None:
                # Fallback: parse "#0x..." from the operand text.
                import re
                mo = re.search(r"#(0x[0-9a-f]+|-\d+|\d+)", insn.op_str)
                if mo:
                    val = int(mo.group(1), 0)
                    target = (pc + val) & 0xffffffff
            if target is not None:
                word = img.bytes_at(target, 4)
                if len(word) == 4:
                    value = int.from_bytes(word, "little")
                    ann = f"  ; [0x{target:08x}] = 0x{value:08x}"
                    s = img.cstring_at(value)
                    if s:
                        ann += f" {s!r}"
                    elif img.section_for(value):
                        ns = img.nearest_symbol(value)
                        if ns:
                            ann += f" ({ns})"
        print(f"0x{insn.address:08x}: {insn.bytes.hex():16s} {insn.mnemonic:8s} {insn.op_str}{ann}")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("library")
    ap.add_argument("--symbol")
    ap.add_argument("--address", type=lambda x: int(x, 0))
    ap.add_argument("--length", type=lambda x: int(x, 0), default=0x400)
    ap.add_argument("--list", dest="query")
    ap.add_argument("--raw", action="store_true", help="search mangled names")
    args = ap.parse_args()

    img = Image(args.library)
    if args.query is not None:
        found = img.find_by_raw(args.query) if args.raw else img.find_symbol(args.query)
        for s in found[:200]:
            print(f"0x{s.addr:08x} {s.size:6d} {s.name}")
        print(f"# {len(found)} match(es)", file=sys.stderr)
        return 0
    if args.symbol:
        found = img.find_by_raw(args.symbol) if args.raw else img.find_symbol(args.symbol)
        if not found:
            print(f"symbol not found: {args.symbol}", file=sys.stderr)
            return 1
        for s in found:
            disassemble(img, s.addr, s.size or args.length)
            print()
        return 0
    if args.address is not None:
        disassemble(img, args.address, args.length)
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
