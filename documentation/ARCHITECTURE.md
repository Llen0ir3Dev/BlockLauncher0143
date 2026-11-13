# Architecture

## Design principle

NEXUS is an **external platform**. The original Minecraft PE client remains the
game. NEXUS never pretends to be a reimplementation and never embeds or
redistributes Mojang code. Every capability is classified as one of:

1. **host tooling** — works without the game (APK inspection, shader
   translation, texture conversion, worldgen, profiles);
2. **cooperative integration** — needs a bridge helper running next to the
   game;
3. **deep integration** — needs native hooks or binary patching inside the
   game process, and is ABI- and version-specific;
4. **blocked** — documented but not possible with the available files.

The execution-strategy planner always prefers facts over assumptions. In
particular it never assumes that ARM or ARM64 Android binaries execute natively
on an x86_64 Linux host.

## Processes

```
        +--------------------+        Unix socket         +----------------------+
        |  nexus-launcher    | <------------------------> |  nexus-runtime       |
        |  (external, TUI)   |   nexus-bridge/1 JSON     |  (bridge host, GL)   |
        +---------+----------+                            +----------+-----------+
                  |                                                |
                  | spawns / supervises                            | asset + mod APIs
                  v                                                v
        +--------------------+                            +----------------------+
        | Android runtime    |                            | shader / texture /   |
        | (Waydroid/Anbox)   |                            | worldgen / mod libs  |
        |  + game APK        |                            +----------------------+
        +---------+----------+
                  | optional cooperative helper (future)
                  v
        +--------------------+
        | Minecraft PE       |
        | 0.14.3 (original)  |
        +--------------------+
```

The launcher and `nexus-runtime` are separate processes. The launcher builds a
launch plan, asks for confirmation when it would spawn anything, and can start
the runtime host as a background process. The runtime host publishes its socket
and serves asset/shader/worldgen methods over the bridge. A future Android-side
helper can implement the same protocol from inside the game's Android runtime.
The protocol does not require the helper to be in the same process as the game.

## Libraries

| Target | Responsibility |
| --- | --- |
| `nexus_core` | logging, strings, SHA-256, filesystem, ZIP reader, JSON config, event bus, time |
| `nexus_shader` | GLSL lexer, preprocessor, parser, AST, analyzer, translator, pack importer, glslang compiler wrapper |
| `nexus_texture` | PNG inspection, texture-pack discovery/import, previews/atlas |
| `nexus_worldgen` | deterministic noise/RNG, biomes, terrain, caves, ores, structures, extensions, bounded chunk cache |
| `nexus_mod` | C ABI, content registry, hooks, Duktape scripts, manifests, native loader |
| `nexus_platform` | system/capability detection, APK/AXML/ELF probe, install provisioning, execution strategy, SDL window |
| `nexus_graphics` | GL loader, shader program wrapper, renderer, PNG screenshot |
| `nexus_bridge` | JSON-lines protocol, Unix-socket agent and client |
| `nexus_mcpe` | uniforms catalog, material parser with inheritance, asset inspection, multiversion shader preparation |

Executables:

| Binary | Purpose |
| --- | --- |
| `nexus` | headless CLI for every subsystem |
| `nexus-runtime` | bridge host; optionally renders a validation GL scene |
| `nexus-launcher` | external launcher: TUI, install/profile views, safe launch plans |

## Installation model

```
$NEXUS_HOME/                     (default ~/.nexus-mcpe)
  installs/<id>/                 imported installations
    install.json                 metadata, hashes, ABI, timestamps
    game.apk                     copy of the source APK (optional)
    lib/<abi>/*.so               extracted native libraries
    assets/                      extracted assets (opt-in)
  profiles/*.json                named launch profiles
  mods/                          script and native mods
  shader-packs/                  imported shader packs
  texture-packs/                 imported texture packs
  logs/                          CLI, launcher and runtime logs
  run/                           default bridge socket
```

`create_installation()` probes the APK first, copies it by default, and extracts
libraries when requested. `verify_installation()` re-hashes the stored APK and
compares it with `install.json`. The source APK is only read.

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
- Native mods use the exported C ABI and are disabled unless
  `--allow-native` is passed.
- Blocks/items are registered in a content registry with explicit custom ID
  ranges; hooks are cooperative pipeline hooks, not arbitrary memory patching.
- `nexus-runtime --mods DIR` discovers and enables mods, exposes them through
  `mods.list`, and forwards events via `events.emit`.

## Trust boundaries

- APKs, packs and mods are untrusted input. Path traversal is rejected for
  archive entries and mod entries; entry sizes are bounded.
- The bridge socket is created with mode `0600` and is local to the user.
- The launcher refuses to spawn processes without an interactive confirmation
  or an explicit `--execute --yes`.
- Logs and reports are written under `$NEXUS_HOME/logs`.
