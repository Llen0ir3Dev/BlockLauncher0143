# NEXUS-MCPE integrated architecture

Status: **working model, not yet a working game**. This document replaces the
old “external launcher + Waydroid” model. It describes the target product and
the exact distance between the current code and that product.

## 1. Product definition

The product is **one AppImage, one launch, one visible window**:

```
user double-clicks NEXUS-MCPE-x86_64.AppImage
        |
        v
   NEXUS frontend  (x86_64 process, owns the only window)
        |
        +-- first run: detect/import the user's APK into game/
        |
        +-- starts the session automatically (no terminal, no manual QEMU)
                |
                +-- hidden x86_64 host renderer/audio/input service
                |      (EGL/GLES2 context, SDL window, sound, keyboard/mouse)
                |
                +-- hidden qemu-arm-static process
                       (ARM32 guest: genuine libminecraftpe.so + dependencies)
```

The user never starts QEMU, the runtime, the loader or the compatibility layer.
Those are implementation details of one supervised session. The only user-facing
actions are: import the game once, then play; optionally manage mods/packs.

## 2. Non-negotiable constraints

1. The released artifact targets **Ubuntu 22.04 x86_64**.
2. Only the **genuine Minecraft PE 0.14.3** `libminecraftpe.so` is executed.
3. Proprietary APK contents are never redistributed; they stay in `game/` in the
   user's home after a first-run import.
4. No Waydroid, no Android emulator, no virtual machine. QEMU **user-mode**
   instruction translation is allowed and is bundled as a separate GPLv2
   executable.
5. Any claim about the game must be backed by a real run of the real library on
   x86_64. “It compiles” and “symbols resolve” are not “the menu appears”.

## 3. Why two processes are unavoidable (and still one product)

The game is ARM32. `qemu-arm-static` executes it as a 32-bit guest process. A
32-bit process cannot load the x86_64 EGL/GLES/OpenAL libraries of the host.
Therefore the GL context, the X11/Wayland window, the audio device and the
physical input devices must live in a **64-bit host service**.

This is not a user-visible split: the frontend spawns and supervises both
children, and the guest connects back over an anonymous local socket. If either
child exits, the frontend reports the failure inside its own window.

```
               one visible window
                       |
          +------------+-------------+
          |                          |
  x86_64 frontend             x86_64 host service
  (UI, import, session)       (EGL/GLES2, SDL audio, input)
          |                          ^
          | spawn/supervise          | length-prefixed commands
          v                          | + shared buffers
  qemu-arm-static                    |
   +-- ARM32 guest runtime ----------+
       +-- in-tree ELF loader
       +-- bionic + Android shims
       +-- EGL/GLES2/OpenSLES client
       +-- JNI environment
       +-- libminecraftpe.so
```

The host service may be a dedicated thread inside the frontend process, but the
GL context must be created on the thread that owns the window. Functionally it
is still “the host side”.

## 4. Current component map

| Layer | Component | State |
| --- | --- | --- |
| Frontend | `nexus-launcher` terminal TUI | exists; **not a GUI product** |
| Frontend | game detection/import (`nexus setup`, `game_source`) | exists and tested |
| Frontend | single supervised “play” session | **missing** |
| Packaging | x86_64 AppImage with ARM32 guest + `qemu-arm-static` | exists; CI-verified |
| Guest | static bionic ARM32 runtime under `qemu-arm` | exists |
| Guest | in-tree ELF loader, 3-module dependency tree | exists; 58 811 relocations, 0 in game deps |
| Guest | bionic symbol registry (253 libc/libm/libdl/libz symbols) | exists |
| Guest | Android platform APIs (`liblog`, `libandroid`, looper, input, config) | 36 imports implemented and registered |
| Guest | EGL/GLES2 client forwarding | exists; all 96 imports forwarded over the socket |
| Guest | OpenSLES audio client | **missing** |
| Guest | JNI `JavaVM`/`JNIEnv` environment | exists; synthetic facade that logs every lookup |
| Guest | `JNI_OnLoad` invocation through the in-tree loader | verified with the genuine library |
| Guest | `ANativeActivity_onCreate` + lifecycle | **missing** |
| Host | window + EGL/GLES2 context (`nexus_gfx_host`) | exists; headless surfaceless + SDL2/X11 window paths |
| Host | GL command execution / resource mirror | exists; dispatches the full protocol to Mesa |
| Host | audio output | **missing** |
| Host | input capture and event translation | **missing** |
| Integration | assets (`AAssetManager`) from imported APK | **missing** (opens logged and refused) |
| Integration | mods/packs wired to the running engine | **missing** |

The unresolved symbol audit of the genuine library now reports **0 imports**
after the loader registers the bionic registry, the 36 Android shims and the
96 EGL/GLES2 forwards.  Everything the three shipped modules import is resolved:

- 96 `gl*`/`egl*` entry points (guest client → host renderer),
- 36 Android platform entry points,
- `_tolower_tab_` and `bsd_signal`,
- 253 libc/libm/libdl/libz symbols from the static-bionic registry,
- plus C++/STL/FMOD symbols exported between the three modules.

## 5. Required startup sequence

This is the sequence the integrated frontend must execute, in order. Every step
is observable and testable; no step may be faked.

1. **Frontend starts** with no terminal. It locates `$APPIMAGE`, `$NEXUS_HOME`
   and `game/`.
2. **Provisioning.** If no valid installation exists, the frontend shows the
   import screen. Otherwise it validates the stored APK hash and the presence of
   `libraries/armeabi-v7a/libminecraftpe.so`.
3. **Session creation.** The frontend creates a per-session directory with a
   Unix socket and a shared-memory file, then starts the host service with the
   desired window size.
4. **Host surface.** The host service creates the window and a real GLES2
   context (Mesa `libEGL`/`libGLESv2`) and publishes readiness.
5. **Guest start.** The frontend starts the bundled `qemu-arm-static` with the
   guest runtime, the game directory, the socket path and the session id.
6. **Guest bootstrap.** The guest:
   1. registers bionic symbols and Android shims;
   2. loads `libminecraftpe.so`, `libfmod.so`, `libgnustl_shared.so` with the
      in-tree loader;
   3. runs constructors in dependency order;
   4. creates a `JavaVM`/`JNIEnv`, calls `JNI_OnLoad`, and verifies the returned
      JNI version;
   5. creates an `ANativeActivity` with a real `assetManager`, `vm`, `env`,
      `clazz`, `internalDataPath`, `externalDataPath`, `sdkVersion` and
      `obbPath`;
   6. calls `ANativeActivity_onCreate`;
   7. invokes the framework callbacks in Android order: `onStart`, `onResume`,
      `onWindowFocusChanged(true)`, `onNativeWindowCreated`, `onInputQueueCreated`,
      `onContentRectChanged`;
   8. pumps the looper until the activity finishes.
7. **Rendering.** The guest's EGL/GLES2 shim forwards the engine's calls to the
   host service; the host executes them against the real context and presents
   the frame in the frontend window.
8. **Input/audio.** The host captures keyboard/mouse/touch and audio devices;
   input events are delivered to the guest `AInputQueue`; audio buffers flow
   through a shared ring.
9. **Shutdown.** Closing the window delivers `onPause`, `onStop`,
   `onNativeWindowDestroyed`, `onDestroy`; the frontend reaps both children.

## 6. Compatibility design decisions

### 6.1 GL forwarding, not GL emulation

The guest implements the exact GLES2/EGL entry points that
`libminecraftpe.so` imports and forwards them over a binary protocol. The host
executes them with the real Mesa GLES2 driver. This keeps shader compilation,
texture formats, extensions and precision semantics on a real GL stack. The
protocol must support:

- context/surface creation and `eglSwapBuffers`;
- shader/program objects and their integer handles;
- buffers with guest pointers (copied to shared memory by offset);
- textures, including sub-image uploads and compressed formats;
- framebuffer/renderbuffer objects;
- uniform/attribute setters and queries;
- `glGetString`, `glGetIntegerv`, `glGetError` and shader precision queries;
- `eglGetProcAddress` for the engine's extension lookup.

The first milestone is deliberately narrow: a real window that clears to a
known colour through the forwarding path. The next is a textured triangle. The
game comes after the protocol survives those tests.

### 6.2 Assets are real files

The APK is read through a minimal ZIP reader in the guest `AAssetManager`
implementation. Assets are served from the imported APK (memory-mapped or
decompressed on demand). The game must see the real asset tree, not a fake empty
manager.

### 6.3 JNI is a controlled facade

`libminecraftpe.so` calls `FindClass`, `GetMethodID`, `RegisterNatives` and many
`Call*Method` entry points on the Java side. NEXUS supplies a deterministic
facade that logs and answers these calls. The facade is not a Java runtime; it
must satisfy the engine's startup expectations and expose only what the engine
actually uses. Every unimplemented entry point fails loudly instead of silently
returning garbage.

### 6.4 Mods/packs attach to the engine, not beside it

Mods, texture packs and shader packs are only meaningful once the engine is
running. The integration point is the same session protocol: the frontend asks
the guest to reload a pack; the guest maps the pack into the engine's asset
namespace, and the host recompiles/uploadsthe affected resources. Until the
engine runs, these features stay host-tooling only and are labelled as such.

## 7. Milestones and acceptance tests

| # | Milestone | Acceptance test (must be real) |
| --- | --- | --- |
| M1 | Guest JNI bootstrap | `JNI_OnLoad` of the genuine library returns `0x00010004` under `qemu-arm` through the in-tree loader |
| M2 | Android shims | the 36 Android imports resolve; no unresolved non-GL symbol remains |
| M3 | GL forwarding core | **verified headless**: guest `glClear` → host Mesa → guest readback and a host PNG with the exact expected pixels |
| M4 | Textured triangle | **verified headless**: guest compiles both shaders, uploads an interleaved VBO and a 2x2 texture, draws the triangle and reads back the exact sampled colour |
| M5 | Activity lifecycle | `ANativeActivity_onCreate` + framework callbacks reach the engine's own window/GL initialisation |
| M6 | First game frame | the genuine Minecraft title/menu is visible in the NEXUS window |
| M7 | Playable session | new world, movement, block placement, save/load on Ubuntu 22.04 x86_64 |
| M8 | Integrated packaging | a fresh user imports the APK and reaches M6 with one double-click |

M1–M4 can be validated on the development host under `qemu-arm` (they are
architecture-independent of the host CPU). M3 and M4 are covered by the
`gfx_bridge_smoke` test: `qemu-arm` runs the ARM32 guest client, which drives
the x86_64 host process and reads back real Mesa pixels. The SDL2/X11 window
path exists and is exercised under `xvfb` in CI, but has not yet been validated
on a physical Ubuntu 22.04 desktop; the dev host has no display server. M5–M8
must be validated on the x86_64 target through the AppImage in CI or on a real
Ubuntu 22.04 machine.

## 8. What is explicitly not claimed

- The repository does not contain or redistribute the APK or game libraries.
- The current AppImage is a verified packaging/loader foundation, not a playable
  game.
- Symbol resolution, relocation counts and a green CI run do not prove that the
  engine starts. Only M6/M7 do.

## 9. Immediate engineering order

1. ~~Android shims + JNI facade through the in-tree loader (M1, M2).~~
2. ~~Host service + EGL/GLES2 forwarding protocol (M3).~~
3. ~~Texture/buffer path and a textured triangle (M4).~~
4. `ANativeActivity` lifecycle and `AAssetManager` over the real APK (M5).
5. Iterate on the engine's real startup log until M6.
6. Only then expose “Play” as a first-class frontend action and wire mods/packs.
