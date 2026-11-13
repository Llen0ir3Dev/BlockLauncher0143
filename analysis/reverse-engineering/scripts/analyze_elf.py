#!/usr/bin/env python3
"""Static inventory of an ELF shared object (read-only).

Produces a JSON document plus a few text tables.  It intentionally relies only
on the ELF metadata that is present in the file: no symbol is invented and no
address is guessed.  Designed for the ARMv7 libminecraftpe.so shipped in the
Minecraft PE 0.14.3 APK, but works for any 32/64-bit ELF.

Usage:
    python3 analyze_elf.py LIBRARY.so OUTDIR
"""

from __future__ import annotations

import hashlib
import json
import os
import re
import subprocess
import sys
from collections import Counter, defaultdict

try:
    from elftools.elf.elffile import ELFFile
    from elftools.elf.sections import SymbolTableSection
except ImportError:
    sys.exit("pyelftools is required: pip install pyelftools")

# Subsystems are matched against demangled symbol names.  A symbol may match
# more than one bucket; the counts are therefore not additive.
SUBSYSTEM_PATTERNS = {
    "graphics": r"\b(mce::|Renderer|Shader|Texture|Material|Framebuffer|FrameBuffer|RenderChunk|LevelRenderer|Tessellator|Mesh|VertexFormat|BlockTessellator)",
    "world": r"\b(Level|LevelChunk|ChunkSource|BlockSource|Dimension|Biome|Feature|PerlinNoise|SimplexNoise|Random|Block\b|BlockID|LevelData|LevelSettings|LevelStorage)",
    "entity": r"\b(Entity|Mob|Player|LocalPlayer|ItemEntity|Projectile|Monster|Animal|Villager|Actor|AABB|Vec[23]\b|BlockPos)",
    "gui": r"\b(Screen|UIControl|ScreenChooser|GuiData|Font|ScrollingPane|InventoryScreen|UIRenderContext|UIFont|ScreenController)",
    "network": r"\b(RakNet|Packet|NetworkHandler|ServerNetworkHandler|ClientNetworkHandler|NetworkChunkSource|NetEventCallback|LoopbackPacketSender)",
    "audio": r"\b(Sound|FMOD|Audio|Music|SoundEngine|SoundPlayer|SoundSystem)",
    "storage": r"\b(leveldb|LevelStorage|RegionFile|DBChunkStorage|LegacyChunkStorage|File|Zip|Compressed|Tag\b|CompoundTag|NBT)",
    "platform": r"\b(AppPlatform|ANative|AInput|AConfiguration|ALooper|JavaUtil|JNI|Android|MC_ENGINE|GamePad)",
    "scripting": r"\b(Script|ScriptEngine|Lua|Duktape|Javascript|JS\b)",
    "modding": r"\b(Plugin|Mod\b|Hook|LoadLibrary|dlopen|dlsym)",
}

JNI_RE = re.compile(r"^Java_[\w]+$")
JNI_OVERLOAD_RE = re.compile(r"^Java_[\w]+__")


def demangle(names: list[str]) -> dict[str, str]:
    """Batch-demangle Itanium C++ names with the system c++filt."""
    if not names:
        return {}
    try:
        proc = subprocess.run(
            ["c++filt"], input="\n".join(names), text=True,
            capture_output=True, check=False,
        )
        out = proc.stdout.splitlines()
        if len(out) == len(names):
            return dict(zip(names, out))
    except FileNotFoundError:
        pass
    return {n: n for n in names}


def sha256_file(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def read_cstring(blob: bytes, off: int, limit: int = 4096) -> str | None:
    if not (0 <= off < len(blob)):
        return None
    end = blob.find(b"\0", off, min(len(blob), off + limit))
    if end < 0:
        return None
    raw = blob[off:end]
    if not raw:
        return ""
    try:
        s = raw.decode("utf-8")
    except UnicodeDecodeError:
        return None
    if any(ch < " " and ch not in "\t\r\n" for ch in s):
        return None
    return s


def analyze(path: str, outdir: str) -> dict:
    os.makedirs(outdir, exist_ok=True)
    data = open(path, "rb").read()
    result: dict = {
        "path": os.path.abspath(path),
        "sha256": sha256_file(path),
        "size": len(data),
    }

    with open(path, "rb") as fh:
        elf = ELFFile(fh)
        result["elf"] = {
            "class": elf.elfclass,
            "little_endian": elf.little_endian,
            "machine": elf["e_machine"],
            "type": elf["e_type"],
            "entry": elf["e_entry"],
            "flags": elf["e_flags"],
        }
        result["sections"] = [
            {
                "name": s.name,
                "type": s["sh_type"],
                "addr": s["sh_addr"],
                "offset": s["sh_offset"],
                "size": s["sh_size"],
                "flags": s["sh_flags"],
                "entsize": s["sh_entsize"],
            }
            for s in elf.iter_sections()
        ]
        result["segments"] = [
            {
                "type": p["p_type"],
                "offset": p["p_offset"],
                "vaddr": p["p_vaddr"],
                "filesz": p["p_filesz"],
                "memsz": p["p_memsz"],
                "flags": p["p_flags"],
                "align": p["p_align"],
            }
            for p in elf.iter_segments()
        ]

        dynamic = {}
        for tag in elf.get_section_by_name(".dynamic").iter_tags() if elf.get_section_by_name(".dynamic") else []:
            key = tag.entry.d_tag
            val = tag.entry.d_val
            if key in ("DT_NEEDED", "DT_SONAME", "DT_RPATH", "DT_RUNPATH", "DT_AUDIT"):
                dynamic.setdefault(key, []).append(val)
            else:
                dynamic[key] = val
        dynstr = elf.get_section_by_name(".dynstr")
        for tag, values in list(dynamic.items()):
            if tag in ("DT_NEEDED", "DT_SONAME", "DT_RPATH", "DT_RUNPATH", "DT_AUDIT"):
                dynamic[tag] = [dynstr.get_string(v) if dynstr else v for v in values]
        result["dynamic"] = dynamic

        symtab = elf.get_section_by_name(".dynsym")
        symbols = []
        raw_names = []
        if isinstance(symtab, SymbolTableSection):
            for sym in symtab.iter_symbols():
                name = sym.name
                if not name:
                    continue
                raw_names.append(name)
                symbols.append({
                    "name": name,
                    "value": sym["st_value"],
                    "size": sym["st_size"],
                    "bind": sym["st_info"]["bind"],
                    "type": sym["st_info"]["type"],
                    "visibility": sym["st_other"]["visibility"],
                    "shndx": sym["st_shndx"],
                    "section": (elf.get_section(sym["st_shndx"]).name
                                if isinstance(sym["st_shndx"], int)
                                and 0 < sym["st_shndx"] < elf.num_sections()
                                else str(sym["st_shndx"])),
                })
        demangled = demangle(sorted({s["name"] for s in symbols}))
        for sym in symbols:
            sym["demangled"] = demangled.get(sym["name"], sym["name"])

        defined = [s for s in symbols if s["shndx"] != "SHN_UNDEF"]
        imports = [s for s in symbols if s["shndx"] == "SHN_UNDEF"]
        funcs = [s for s in defined if s["type"] == "STT_FUNC"]
        objects = [s for s in defined if s["type"] == "STT_OBJECT"]

        result["symbol_counts"] = {
            "total": len(symbols),
            "defined": len(defined),
            "undefined": len(imports),
            "functions": len(funcs),
            "objects": len(objects),
            "jni_exports": sum(1 for s in funcs if JNI_RE.match(s["name"])),
            "vtables": sum(1 for s in objects if s["name"].startswith("_ZTV")),
            "typeinfo": sum(1 for s in objects if s["name"].startswith("_ZTI")),
        }

        result["jni_exports"] = sorted(
            (
                {
                    "name": s["name"],
                    "address": s["value"],
                    "thumb": bool(s["value"] & 1),
                }
                for s in funcs
                if JNI_RE.match(s["name"])
            ),
            key=lambda s: s["address"],
        )
        result["entry_points"] = sorted(
            (
                {
                    "name": s["name"],
                    "demangled": s["demangled"],
                    "address": s["value"],
                    "size": s["size"],
                }
                for s in funcs
                if s["name"] in {
                    "JNI_OnLoad", "JNI_OnUnload", "ANativeActivity_onCreate",
                    "android_main", "app_dummy", "AAssetManager_open",
                }
            ),
            key=lambda s: s["address"],
        )

        subsystem = defaultdict(list)
        for s in funcs:
            text = s["demangled"]
            for label, pattern in SUBSYSTEM_PATTERNS.items():
                if re.search(pattern, text):
                    subsystem[label].append(s)
        result["subsystems"] = {
            label: {
                "functions": len(items),
                "sample": [
                    {"address": x["value"], "name": x["demangled"], "size": x["size"]}
                    for x in sorted(items, key=lambda x: x["value"])[:12]
                ],
            }
            for label, items in sorted(subsystem.items())
        }

        result["imports"] = sorted(
            ({"name": s["name"], "demangled": s["demangled"]} for s in imports),
            key=lambda x: x["name"],
        )

        # Class-level inventory: strip "Class::method(...)" to the qualified
        # class name.  Perfect for spotting ownership boundaries in a binary
        # that still exports its DynSym table.
        classes = Counter()
        for s in funcs:
            mo = re.match(r"^(?:non-virtual thunk to |virtual thunk to )?"
                          r"([A-Za-z_][\w:<>, *&]*?)::[~\w]", s["demangled"])
            if mo:
                cls = re.sub(r"<.*>$", "", mo.group(1)).strip()
                classes[cls] += 1
        result["classes"] = [
            {"class": c, "functions": n} for c, n in classes.most_common()
        ]

        # Interesting strings from .rodata and .dynstr.  Offsets are file
        # offsets so they can be checked with xxd/readelf.
        rodata = elf.get_section_by_name(".rodata")
        strings = []
        if rodata is not None:
            blob = rodata.data()
            base = rodata["sh_addr"]
            for mo in re.finditer(rb"[\x20-\x7e]{4,}", blob):
                text = mo.group().decode("ascii")
                strings.append({
                    "offset": base + mo.start(),
                    "file_offset": rodata["sh_offset"] + mo.start(),
                    "text": text,
                })
        result["string_count"] = len(strings)

        path_re = re.compile(r"(?:^|/)(?:assets|shaders|materials|images|sounds|loc|minecraftWorlds|minecraftpe|games|data)/")
        findings = {
            "asset_paths": [],
            "shader_identifiers": [],
            "java_signatures": [],
            "source_paths": [],
            "gl_symbols": [],
            "storage_paths": [],
            "environment": [],
        }
        for entry in strings:
            t = entry["text"]
            if path_re.search(t) or t.endswith((".material", ".vertex", ".fragment", ".json", ".lang", ".fsb", ".tga", ".png")):
                findings["asset_paths"].append(entry)
            if re.search(r"\b(shaders?|materials?|uniforms?|gl[ A-Z]|precision |#version|multiversion)\b", t, re.I):
                findings["shader_identifiers"].append(entry)
            if re.match(r"^(?:\(|L?java/|L?android/|com/mojang)", t) or t.startswith("Lcom/"):
                findings["java_signatures"].append(entry)
            if re.search(r"(?:src-deps|src/common|src/common/client|/jni/|\.cpp$|\.h$)", t):
                findings["source_paths"].append(entry)
            if re.match(r"^(?:gl|egl)[A-Z]\w+$", t):
                findings["gl_symbols"].append(entry)
            if t in ("minecraftpe/", "minecraftWorlds/", "/external_servers.txt") or "com.mojang" in t:
                findings["storage_paths"].append(entry)
            if re.match(r"^[A-Z][A-Z0-9_]{4,}$", t):
                findings["environment"].append(entry)

        result["strings"] = {k: v[:400] for k, v in findings.items()}

    with open(os.path.join(outdir, "elf_report.json"), "w", encoding="utf-8") as f:
        json.dump(result, f, indent=2)

    with open(os.path.join(outdir, "imports.txt"), "w", encoding="utf-8") as f:
        for s in result["imports"]:
            f.write(f"{s['name']}\t{s['demangled']}\n")

    with open(os.path.join(outdir, "classes.txt"), "w", encoding="utf-8") as f:
        for c in result["classes"]:
            f.write(f"{c['functions']:5d}  {c['class']}\n")

    with open(os.path.join(outdir, "jni_exports.md"), "w", encoding="utf-8") as f:
        f.write("| Address | Symbol |\n|---|---|\n")
        for s in result["jni_exports"]:
            f.write(f"| 0x{s['address']:08x} | `{s['name']}` |\n")

    with open(os.path.join(outdir, "entry_points.md"), "w", encoding="utf-8") as f:
        f.write("| Address | Size | Symbol |\n|---|---|---|\n")
        for s in result["entry_points"]:
            f.write(f"| 0x{s['address']:08x} | {s['size']} | `{s['demangled']}` |\n")

    with open(os.path.join(outdir, "interesting_strings.md"), "w", encoding="utf-8") as f:
        for category, entries in result["strings"].items():
            f.write(f"\n## {category}\n\n")
            for e in entries[:200]:
                f.write(f"- `0x{e['offset']:08x}` {e['text']!r}\n")

    return result


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    lib, outdir = sys.argv[1], sys.argv[2]
    r = analyze(lib, outdir)
    print(f"ELF: {r['path']}")
    print(f"SHA256: {r['sha256']}")
    print(f"Sections: {len(r['sections'])}, segments: {len(r['segments'])}")
    print("Symbols:", json.dumps(r["symbol_counts"]))
    print(f"JNI exports: {len(r['jni_exports'])}")
    print(f"Entry points: {[e['name'] for e in r['entry_points']]}")
    print(f"Strings: {r['string_count']}")
    print(f"Output: {outdir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
