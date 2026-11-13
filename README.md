# NEXUS-MCPE

External launcher, bridge and modding platform for **Minecraft PE 0.14.3**.

NEXUS does **not** ship the game, a decompiled game, assets or any Mojang
binary. It manages a user-supplied APK, selects an execution strategy that is
actually possible on the host, and provides tooling around the original client:
shader translation, texture-pack import, deterministic world generation, a
script/native mod engine, profiles, logs and a JSON bridge.

> Status: the framework, CLI, runtime host, bridge and test suite are built and
> verified on this machine. The supplied APK is an **ARMv7** build and cannot
> run on an x86_64 Linux desktop. In-game shader/mod application additionally
> requires an Android runtime and a game-side helper; see
> [`documentation/EXECUTION-STRATEGIES.md`](documentation/EXECUTION-STRATEGIES.md)
> and [`documentation/INJECTION-LEVELS.md`](documentation/INJECTION-LEVELS.md).

## What works now

| Area | Status |
| --- | --- |
| APK inspection (binary AXML, version code, ABI, ELF, shader/material inventory) | verified against the supplied APK |
| Execution strategy planner (never assumes ARM code runs on x86) | verified |
| Installations (import/list/verify/remove) with hash verification | verified end-to-end |
| Shader lexer/parser/preprocessor/analyzer/translator/compiler | verified on real MCPE shaders |
| MCPE uniforms/materials (comments, `child:parent` inheritance, multiversion) | verified on the real assets |
| Texture pack inspect/import/preview | unit + integration tested |
| Deterministic world generation + extension hooks | unit + integration tested |
| Mod engine (Duktape JavaScript, native ABI, hooks, registry) | unit tested |
| Bridge agent/client over Unix socket (JSON lines) | unit + integration tested |
| Runtime host (`nexus-runtime`) with real bridge methods | verified |
| External launcher (TUI + headless modes) | verified |
| Real game launch on this host | **blocked**: ARMv7 APK, no Android runtime |

## Requirements

- Ubuntu 22.04 x86_64 (or a similar Linux distribution)
- CMake ≥ 3.16, Ninja, a C++17 compiler
- SDL2, zlib, libpng, OpenGL headers
- Optional: `glslangValidator` (from `glslang-tools`) for real shader compilation
- Optional: Waydroid or Anbox plus an authorized **x86** 0.14.3 APK
  (`versionCode` `780140300` or `780140301`)

```sh
sudo apt install build-essential cmake ninja-build \
  libsdl2-dev zlib1g-dev libpng-dev libgl1-mesa-dev libegl1-mesa-dev \
  glslang-tools
```

## Build

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DNEXUS_BUILD_TESTS=ON \
  -DNEXUS_BUILD_RUNTIME=ON \
  -DNEXUS_BUILD_LAUNCHER=ON
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

Or use the helper scripts / presets:

```sh
./scripts/build.sh
./scripts/test.sh
cmake --preset dev
```

## First steps

```sh
# What does this host actually support?
./build/nexus doctor

# Inspect an APK without modifying it
./build/nexus apk info minecraft-0.14.3.apk
./build/nexus apk verify minecraft-0.14.3.apk

# Install it into NEXUS's own directory (original APK stays untouched)
./build/nexus install import minecraft-0.14.3.apk --root ~/.nexus-mcpe/installs
./build/nexus install list --root ~/.nexus-mcpe/installs
./build/nexus strategy --json

# Analyze and translate the real assets
./build/nexus mcpe info --assets original/assets
./build/nexus shader compile original/assets/shaders/entity.vertex

# Start the runtime host and talk to it
./build/nexus-runtime --headless --socket /tmp/nexus.sock --seconds 60 &
./build/nexus bridge ping   --socket /tmp/nexus.sock
./build/nexus bridge call runtime.info --socket /tmp/nexus.sock

# Launcher (interactive TUI, or headless)
./build/nexus-launcher
./build/nexus-launcher --report --json
```

`nexus apk verify` exits with code `3` for a valid APK that is not an x86 build.
That is the correct result for the supplied ARMv7 APK, not a failure of NEXUS.

## Repository layout

```
include/nexus/        public headers (core, shader, texture, worldgen, mod,
                      platform, graphics, bridge, mcpe)
src/                  implementations and the three executables
  cli/                nexus command line
  runtime/            nexus-runtime bridge host and optional GL demo
  launcher/           nexus-launcher external launcher
tests/unit/           seven standalone unit suites
tests/integration/    end-to-end CLI/bridge/APK test
third_party/          Duktape 2.7.0 and nlohmann/json (vendored)
mods/                 bundled example mods
examples/             mod, shader-pack and profile examples
documentation/        architecture, bridge, strategy, injection, progress
```

## Legal and safety

- Bring your own legally acquired APK. The project never downloads or
  redistributes game files.
- The original APK is opened read-only, hashed and never modified. The
  integration test asserts that its SHA-256 is unchanged after import.
- `original/`, `external/`, `build/` and `*.apk` are excluded from release
  archives.
- Native mods execute arbitrary code and are disabled unless explicitly
  enabled with `--allow-native`. Script mods run in Duktape with dangerous
  globals removed and a memory cap.

## License

Original NEXUS-MCPE code is MIT licensed. See [`LICENSE`](LICENSE) and
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).
