# NEXUS-MCPE distribution and first-run plan

Status: living document. This describes what the AppImage can ship, what must
stay external, the legal constraints around Minecraft, and the recommended
distribution model.

The guiding rule: **NEXUS never redistributes the game.** The user supplies a
Minecraft PE 0.14.3 APK that they are entitled to use. NEXUS detects, verifies,
imports and later executes it locally.

Three architectures are involved and must never be conflated:

| Role | Architecture | Where |
| --- | --- | --- |
| Development host | aarch64 (Termux/PRoot) | author's machine, logic validation only |
| Release host | x86_64 | Ubuntu 22.04, GitHub Actions `ubuntu-22.04` |
| Guest | ARM32 (armeabi-v7a) | real Minecraft PE 0.14.3 `libminecraftpe.so` under QEMU user-mode |

---

## 1. What can be embedded in the AppImage

| Component | Embeddable | Notes |
| --- | --- | --- |
| `nexus-launcher` (TUI launcher) | yes | primary entry point |
| `nexus` CLI | yes | import/verify/strategy/shader/texture tools |
| `nexus-runtime` | yes | SDL2 window + GL context demo, bridge host |
| `nexus_arm_loader` | yes | host-side ARM loader/preflight tool |
| `qemu-arm-static` (or `qemu-arm`) | yes | user-mode ARM32 translator, separate process |
| ARM32 guest runtime | yes | static bionic guest built with the Android NDK |
| bionic ABI shims (our code) | yes | `operator new/delete`, zlib, log, properties, etc. |
| GLES2/EGL client + IPC bridge | yes | our code, forwards to a host GL context |
| OpenSLES audio bridge | yes | our code, forwards to PipeWire/ALSA/PulseAudio |
| Keyboard/mouse input bridge | yes | our code |
| Default configs, profiles, example mods/shader packs | yes | data files, permissively licensed |
| Documentation | yes | |
| Qt/GTK/Java runtime | no | not used; the UI is dependency-free ANSI |
| Waydroid / Anbox / Android emulator / VM | no | explicitly forbidden by the project and unnecessary |

The AppImage is built on `ubuntu-22.04` with `linuxdeploy` + `appimagetool` and
bundles the internal executables via the `EXTRA_BINARIES` mechanism in
`cmake/FindAppImage.cmake`, plus `qemu-arm-static` via `QEMU_BINARY` and the
guest runtime via `EXTRA_FILES`.

---

## 2. What cannot (or must not) be embedded

| Item | Why |
| --- | --- |
| `minecraft-0.14.3.apk` | proprietary; no redistribution right |
| `libminecraftpe.so` and other Mojang libraries | proprietary binary code |
| Game assets (textures, sounds, fonts, lang files) | copyrighted content |
| `libfmod.so` | proprietary audio middleware shipped in the APK |
| Mojang/Microsoft account credentials or tokens | secrets, user-owned |
| 64-bit host GL/audio libraries copied into a 32-bit guest | cannot be linked (ABI width) |

The last point is why the architecture uses **IPC**: a 32-bit ARM guest cannot
load the host's 64-bit `libEGL.so`/`libGLESv2.so`. The guest talks to an x86_64
host process that owns the real GL/audio context.

---

## 3. Redistribution constraints

- **Minecraft PE 0.14.3 (Mojang/Microsoft).** The APK, its native libraries and
  its assets are proprietary. Redistributing them requires explicit written
  authorization that this project does not have. NEXUS must therefore only
  *consume* a user-supplied copy.
- **QEMU user-mode.** GPLv2. Distributing a binary in the AppImage is allowed,
  but the licence and a written offer/pointer to the corresponding source must
  accompany it. QEMU runs as a **separate process**; NEXUS does not link
  against it, so the rest of NEXUS is not forced to GPL by that alone. This
  must be reviewed before a public release.
- **Android NDK bionic guest.** The guest is compiled against the NDK sysroot;
  the resulting static executable includes bionic runtime code. The NDK's
  licence permits redistribution of compiled output; keep the NDK notice.
- **Third-party libraries** (SDL2, zlib, libpng, nlohmann/json, Duktape) keep
  their own licences; see `THIRD_PARTY_NOTICES.md`.
- **AppImage tooling** (linuxdeploy, appimagetool) is used at build time only.

No proprietary file is tracked in git. `.gitignore` excludes `original/`,
`*.apk`, `analysis/symbols/`, build outputs and local toolchain state.

---

## 4. Distribution models A / B / C

### Solution A — AppImage with Minecraft bundled

The AppImage ships the APK/libraries/assets and launches immediately.

- Technically simple: same pipeline as B, plus the files.
- **Legally unacceptable without written authorization.** It would redistribute
  Mojang/Microsoft proprietary code and assets.
- Also bloats the artifact and complicates updates.
- Verdict: **rejected** for public distribution. Only viable for a private,
  locally-built artifact the user creates from their own files (not published).

### Solution B — AppImage with automatic import from a predefined folder

The user drops their APK into `game/` next to the AppImage (or
`$NEXUS_GAME_DIR`, `$NEXUS_HOME/game`, `./game`). NEXUS detects and verifies it
automatically.

- No proprietary content is redistributed.
- Zero-friction for the user after the one-time file placement.
- Implemented: `detect_game_sources()` + `nexus setup` + launcher `--detect`
  and the `i` key.

### Solution C — Guided first-run import

On first launch, the TUI detects that no game is installed and explains where
to place the APK, then imports on request.

- Best discoverability; the user is never left with a cryptic `(none)`.
- Implemented: the launcher's empty-state guidance and the interactive `i`
  command.

### Recommendation: B + C hybrid

Ship B as the automatic path and C as the fallback guidance. This is the
simplest, most reliable and legally sound combination:

1. Search the predefined folders (B).
2. If found, verify and import automatically.
3. If not found, guide the user and import on request (C).
4. Never bundle or download the game.

---

## 5. Desired first-run behaviour (and current status)

| Step | Behaviour | Status |
| --- | --- | --- |
| 1 | User launches `NEXUS-MCPE-x86_64.AppImage` | works (needs `libfuse2` or `--appimage-extract-and-run`) |
| 2 | Check environment (arch, display, QEMU, data dirs) | host checks exist; `--report` shows capabilities |
| 3 | Detect an existing installation | works (`discover_installations`) |
| 4 | If absent, detect APK in known folders | works (`detect_game_sources`) |
| 5 | If still absent, guide the user | works (TUI empty state + `i`, `nexus setup`) |
| 6 | Verify files and version | works (`probe_apk`, `verify_installation`) |
| 7 | Initialise the compatibility layer | **not complete** (loader/shims/bridges in progress) |
| 8 | Show a PLAY button | TUI `l` launch action exists; no rendered UI button yet |
| 9 | Start real Minecraft PE 0.14.3 in a Linux window | **not possible yet** |

Steps 1–6 and the import UX are real. Step 9 is the remaining engineering
effort; it must not be advertised as working until a genuine frame is rendered.

---

## 6. What is operational, experimental, unimplemented

### Operational (tested)

- Host build and test suite (unit tests + integration CLI).
- APK inspection: binary `AndroidManifest.xml`, version code, ABI, ELF headers,
  shader/material inventory, SHA-256.
- Installation import/verify/list/remove; automatic detection and `nexus setup`.
- Execution-strategy planner (never assumes ARM code runs on x86).
- Shader analysis/translation/packing, texture-pack import, deterministic
  worldgen, profiles, JSON bridge.
- Static ARM32 bionic guest cross-build that runs under `qemu-arm`.
- The genuine `libminecraftpe.so` binds and `JNI_OnLoad` returns `0x10004`
  **on the development path only** (device bionic linker). This is evidence of
  the JNI surface, not proof that the shipped AppImage is autonomous.

### Experimental / not shippable

- The dynamic JNI harness depends on a device bionic linker. It is excluded
  from the shippable guest bundle (`--static-only`).
- GLES/EGL shims are validated only in same-architecture mode, not over IPC.

### Not implemented

1. In-tree ARM32 ELF loader (mapping, relocations, symbol resolution).
2. Bionic ABI shims (`operator new/delete`, zlib, `__android_log_*`,
   property service, pthread details).
3. `ANativeActivity_onCreate` + `android_app` lifecycle.
4. EGL/GLES2 IPC bridge to the x86_64 host.
5. OpenSLES audio bridge.
6. Keyboard/mouse input bridge.
7. First real rendered Minecraft menu.
8. Launcher wiring to the new compatibility layer (the strategy planner still
   reports the ARM APK as blocked, which is correct until 1–7 exist).

---

## 7. AppImage packaging strategy

- Built only on x86_64 (`CMakeLists.txt` refuses on other hosts).
- Single ordered custom target; `POST_BUILD` is invalid for custom targets.
- `EXTRA_BINARIES` copies `nexus`, `nexus-runtime`, `nexus_arm_loader`.
- `QEMU_BINARY` copies `qemu-arm-static` (preferred) or `qemu-arm`.
- `EXTRA_FILES` copies example data, documentation and the guest runtime to
  `usr/share/nexus-mcpe/compat/arm32`.
- The desktop entry uses `Terminal=true`: the current UI is a TUI and needs a
  terminal, so a double-click opens one instead of exiting with
  "interactive mode needs a TTY".
- CI extracts the produced AppImage and asserts every expected binary is
  present and has the correct architecture before uploading.

### Ubuntu 22.04 dependency

Direct AppImage execution needs FUSE 2 (`sudo apt install libfuse2`). Without
FUSE, `--appimage-extract-and-run` works. This is the only host dependency for
the launcher itself.

---

## 8. Remaining work, in order

1. **In-tree ARM32 ELF loader** — load `libminecraftpe.so`, apply relocations,
   resolve symbols, call `JNI_OnLoad` without a device linker.
2. **Bionic shims** — the symbol surface the real library imports.
3. **`ANativeActivity_onCreate` lifecycle** — construct the activity/app,
   asset manager, native window, looper.
4. **EGL/GLES2 IPC bridge** — guest client ↔ x86_64 host renderer.
5. **OpenSLES audio bridge.**
6. **Input bridge** — keyboard/mouse in, Android input events out.
7. **First frame** — render the genuine title screen.
8. Rewire the launcher/strategy so a verified ARM APK with a complete
   compatibility layer reports the ARM strategy as viable and PLAY starts it.

Each completed, verified step is committed and pushed to `origin/main`.
