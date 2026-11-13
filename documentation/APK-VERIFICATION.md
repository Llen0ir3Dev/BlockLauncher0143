# APK verification report

This report records the verified properties of the user-supplied APK and what
NEXUS did with it. The file is opened read-only; its SHA-256 was checked before
and after installation by the integration test.

## File

| Property | Value |
| --- | --- |
| Path used for testing | `/root/mcpelinux/minecraft-0.14.3.apk` |
| Size | 21,803,588 bytes |
| SHA-256 | `c4365666ac5c5a65af3c8a94d765e34dc2f525bbca787d9431e0aa091f70f0f6` |
| Package | `com.mojang.minecraftpe` |
| Version name | `0.14.3` |
| Version code | `760140300` |
| Min SDK | 17 |
| Target SDK | 22 |
| ABIs | `armeabi-v7a` only |
| Verdict | `valid-but-not-runnable-x86` |

`nexus apk verify` exits `3` for this file. Exit `3` means "valid APK, but not
an x86 build"; it is the expected result, not an error in NEXUS.

## Native libraries

Extracted under `original/libraries/armeabi-v7a/` for analysis only.

| Library | SHA-256 |
| --- | --- |
| `libminecraftpe.so` | `78989534f382a7b46fcecdf7e868fb254f50f80faa9b37a6a4dc66522e664520` |
| `libfmod.so` | `e8ed6009af4b0f3fb136eb3ed28a61606ff18497f97bef3b8528fd07927ccf1d` |
| `libgnustl_shared.so` | `39043dc1c83a3f5108b5a341404ea8a71d3b228db9742ed2cbf94799715a311f` |

`libminecraftpe.so`:

- ELF32, little-endian, ARM EABI5, `DYN` shared object, stripped;
- interpreter `/system/bin/linker` (Android bionic);
- `NEEDED`: `libfmod.so`, `libgnustl_shared.so`, `liblog.so`, `libandroid.so`,
  `libEGL.so`, `libGLESv2.so`, `libOpenSLES.so`, `libz.so`, `libstdc++.so`,
  `libm.so`, `libc.so`, `libdl.so`.

This is an Android ARM binary. It cannot be loaded by an x86_64 Linux glibc
process. See [`EXECUTION-STRATEGIES.md`](EXECUTION-STRATEGIES.md).

## Assets observed

Extracted for analysis into `original/assets/` (excluded from release archives):

- `shaders/uniforms.json`: 8 uniform groups;
- 26 material files, **230 material passes**;
- 39 shader files, **52 referenced shaders**, **0 missing referenced shaders**;
- material files contain `//` comments and use `child:parent` inheritance,
  including parents defined in other files;
- some materials reference geometry shaders, which GLES 2.0 cannot execute;
  these are reported as informational issues rather than missing files.

## Shader compilation

Every real `.vertex`/`.fragment` file was passed through
`nexus shader compile`:

- **38 / 39 compile** with `glslangValidator`;
- the one rejection is `normal_as_color.vertex`, which contains
  `NORMAL.xyz / 2` (vector divided by an integer literal). Lenient vendor
  compilers accept it, but strict GLSL ES 1.00 rejects the missing implicit
  conversion. This shader is **not referenced by any material** in the shipped
  asset set, so it does not affect the active pipeline. NEXUS reports the error
  instead of hiding it.

The multiversion `// __multiversion__` sources required a compiler-path fix:
glslang rejects a user definition of the reserved `__VERSION__` macro. NEXUS
now preprocesses the branches, emits a single `#version` header, injects the
default fragment float precision, and expands the MCPE `MAT4`/`POS4` shorthand
before validation. `#include "shaders/util.h"` is resolved relative to the
asset root.

## Provenance and preservation

- The original APK is never written to. `create_installation()` opens it for
  reading, hashes it, and copies it into the NEXUS installation directory.
- The integration test imports the APK into a temporary root, lists it, verifies
  it, removes the installation, and compares the original SHA-256 before and
  after.
- Extracted libraries/assets and the original APK are excluded from
  `NEXUS-MCPE-Linux.zip`.

## What would change with the x86 build

An x86 APK `780140300` or `780140301` would:

- pass `nexus apk verify` with exit `0`;
- make `nexus strategy` choose `android-container-x86` when Waydroid/Anbox is
  present;
- allow the launcher to produce an executable Android-container plan;
- still require a game-side helper for in-game shader/mod application (see
  [`INJECTION-LEVELS.md`](INJECTION-LEVELS.md)).
