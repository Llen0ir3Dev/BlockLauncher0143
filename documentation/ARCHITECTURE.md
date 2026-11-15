# Architecture

> The target product model is defined in
> [`INTEGRATED-ARCHITECTURE.md`](INTEGRATED-ARCHITECTURE.md). This file keeps
> the subsystem/library overview; where the two differ, the integrated document
> wins.

## Design principle

NEXUS runs the **original Minecraft PE 0.14.3 client**. It never reimplements
the game and never embeds or redistributes Mojang code. The released AppImage
must behave as one product: one launch, one window, no terminal, no manual QEMU
or runtime configuration.

The game is ARM32 Android code. It is executed by the bundled QEMU user-mode
translator (`qemu-arm-static`), which is a separate GPLv2 process. Because a
32-bit guest cannot load the x86_64 graphics/audio libraries of the host, the
host side owns the real window, GL context, audio device and input devices, and
the guest forwards the corresponding calls over a local protocol.

## Runtime model

```
                  one visible NEXUS window
                            |
             +--------------+---------------+
             |                              |
     x86_64 NEXUS frontend          x86_64 host service
     (import, session, UI)          (EGL/GLES2, audio, input)
             |                              ^
             | spawn + supervise            | commands + shared buffers
             v                              |
     qemu-arm-static                        |
       +-- ARM32 guest runtime -------------+
            +-- in-tree ELF loader
            +-- bionic + Android shims
            +-- JNI facade
            +-- libminecraftpe.so (genuine)
```

A first run imports the user-supplied APK into `game/`; nothing proprietary is
stored in the repository or the AppImage.

## Libraries

| Target | Responsibility |
| --- | --- |
| `nexus_core` | logging, strings, SHA-256, filesystem, ZIP reader, JSON config, event bus, time |
| `nexus_shader` | GLSL lexer, preprocessor, parser, AST, analyzer, translator, pack importer, glslang wrapper |
| `nexus_texture` | PNG inspection, texture-pack discovery/import, previews/atlas |
| `nexus_worldgen` | deterministic noise/RNG, biomes, terrain, caves, ores, structures, extensions, bounded chunk cache |
| `nexus_mod` | C ABI, content registry, hooks, Duktape scripts, manifests, native loader |
| `nexus_platform` | system/capability detection, APK/AXML/ELF probe, install provisioning, execution strategy, SDL window |
| `nexus_graphics` | GL loader, shader program wrapper, renderer, PNG screenshot |
| `nexus_bridge` | JSON-lines protocol, Unix-socket agent and client |
| `nexus_mcpe` | uniforms catalog, material parser with inheritance, asset inspection, multiversion shader preparation |
| ARM32 guest | static bionic runtime, in-tree ELF loader, Android shims, EGL/GLES2 client, JNI/activity facade |

Executables:

| Binary | Purpose |
| --- | --- |
| `nexus` | headless CLI for every subsystem |
| `nexus-runtime` | bridge host and validation GL scene (host tooling) |
| `nexus-launcher` | current terminal frontend: import, profiles, launch plans |
| `nexus_elf_inspect` | host-side ARM32 ELF inspector |
| `nexus_elf_loader` | ARM32 guest loader/inspector (runs under `qemu-arm`) |
| `nexus_arm_loader` | host-side compatibility prototype |

The terminal `nexus-launcher` is **not** the final frontend. The final frontend
is a windowed session supervisor described in the integrated document.

## Installation model

```
$NEXUS_HOME/                     (default ~/.nexus-mcpe)
  installs/<id>/                 imported installations
    install.json                 metadata, hashes, ABI, timestamps
    game.apk                     copy of the source APK (optional)
    lib/<abi>/*.so               extracted native libraries
    assets/                      extracted assets (opt-in)
  game/                          the imported game used by the integrated session
  profiles/*.json                named launch profiles
  mods/                          script and native mods
  shader-packs/                  imported shader packs
  texture-packs/                 imported texture packs
  logs/                          frontend, host and guest logs
  run/                           session sockets and shared-memory files
```

`create_installation()` probes the APK first, copies it by default, and extracts
libraries when requested. `verify_installation()` re-hashes the stored APK and
compares it with `install.json`. The source APK is only read. The integrated
frontend reuses the same provisioning code; it does not introduce a second
installation format.

## ARM32 compatibility engine

1. The guest is a **static bionic ARM32 binary** so it does not need a device
   dynamic linker.
2. The in-tree ELF loader maps `libminecraftpe.so` and its non-system
   dependencies, applies ARM relocations, and resolves cross-module symbols.
3. A generated symbol registry exposes the static bionic symbols
   (`libc`, `libm`, `libdl`, `libz`) to the loaded libraries.
4. Guest shims provide the Android platform APIs (`liblog`, `libandroid`,
   `AAssetManager`, `ALooper`, `AInputQueue`) and the EGL/GLES2/OpenSLES clients.
5. A JNI facade supplies `JavaVM`/`JNIEnv`, then the guest invokes
   `JNI_OnLoad` and the `ANativeActivity` lifecycle.
6. All graphics/audio/input calls are forwarded to the x86_64 host service over
   the session protocol.

Current verified state and gaps are tracked in
[`INTEGRATED-ARCHITECTURE.md`](INTEGRATED-ARCHITECTURE.md#4-current-component-map).

## Shader pipeline

1. `Preprocessor` resolves `#define`, `#if/ifdef/elif`, `defined()` and
   `__VERSION__` branches.
2. `Lexer` → `Parser` → AST.
3. `Analyzer` collects uniforms, attributes, varyings, functions, precision,
   unsupported constructs and compatibility level.
4. `Translator` maps OptiFine/MCPE names, injects precision, rewrites ES 3
   functions for ES 1.00, and emits a target profile:
   GLSL ES 1.00 (primary), GLSL 1.20, or GLSL ES 3.00.
5. `Compiler` optionally writes the translated source to a temp file and runs
   `glslangValidator`. Multiversion sources are flattened first, because
   glslang rejects a user definition of the reserved `__VERSION__` macro.

Real MCPE shaders use `MAT4`, `POS4` and similar loader-injected aliases and
omit the fragment float precision. The compiler wrapper supplies both before
validation.

## MCPE asset model

`nexus_mcpe` parses the shipped `shaders/uniforms.json`, every `*.material`
file (tolerant of `//` and `/* */` comments) and `common.json` include lists.
Material names use `child:parent`; inheritance is resolved across files and
validated for missing parents and cycles. Abstract parents without shaders are
accepted. A material pass that references a geometry shader is reported as
unavailable on GLES 2.0 but is not counted as a missing shader.

## Mod engine

- Manifests (`mod.json`) define id, version, type, entry point, requested
  permissions and events.
- Script mods run in Duktape with a 32 MiB default heap cap and dangerous
  globals removed.
- Native mods use the exported C ABI and are disabled unless `--allow-native`
  is passed.
- Blocks/items are registered in a content registry with explicit custom ID
  ranges; hooks are cooperative pipeline hooks, not arbitrary memory patching.
- `nexus-runtime --mods DIR` discovers and enables mods, exposes them through
  `mods.list`, and forwards events via `events.emit`.

Mods/packs are only connected to the real engine after the session can reach
the game. Until then they remain host tooling.

## Trust boundaries

- APKs, packs and mods are untrusted input. Path traversal is rejected for
  archive entries and mod entries; entry sizes are bounded.
- The bridge/session socket is created with mode `0600` and is local to the
  user.
- The final frontend may spawn the host service and `qemu-arm-static` without a
  shell prompt, but only for the user-selected installation; it must never
  execute arbitrary paths from a pack or mod.
- Logs and reports are written under `$NEXUS_HOME/logs`.
