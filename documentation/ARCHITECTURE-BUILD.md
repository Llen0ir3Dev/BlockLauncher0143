# NEXUS-MCPE — Architecture and Build Strategy

This document defines, unambiguously, the **three architectures** involved in
the project and how each artifact is produced. It exists because a successful
build on the development machine is *not* an x86_64 build.

## 1. The three architectures

| Role | Architecture | OS / runner | What it is |
| --- | --- | --- | --- |
| **Development environment** | `aarch64` (ARM64) | Android → Termux → Alpine → PRoot | Where the code is edited and where logic/guest tests run. Never the release target. |
| **Target host** | `x86_64` (AMD64) | Ubuntu 22.04 LTS | Where `NEXUS-MCPE-x86_64.AppImage` must run natively. |
| **Guest (the game)** | `armeabi-v7a` (ARM32, bionic) | Android 4.x–5.x ABI | The genuine `libminecraftpe.so` and friends from the user's APK. |

Consequences:

* The host programs (`nexus`, `nexus-runtime`, `nexus-launcher`) must be
  **ELF 64-bit x86-64**.
* The guest compatibility code must be **ELF 32-bit ARM (EABI5)**, built with
  the Android NDK.
* ARM32 guest code is executed by **QEMU user-mode** (`qemu-arm`), which is an
  *instruction translator*, not an Android emulator and not a VM. NEXUS bundles
  it; the user does not install Waydroid, Android Studio, the Android Emulator
  or a virtual machine.

## 2. Verified facts about the current environment

Checked with `uname -m`, `file`, `readelf -h`:

```
uname -m                     -> aarch64
build/nexus                  -> ELF 64-bit ... ARM aarch64
build/nexus-runtime          -> ELF 64-bit ... ARM aarch64
build/compat/arm32/nexus_arm_loader -> ELF 64-bit ... ARM aarch64
```

So a local `cmake --build build` here produces **aarch64** binaries. It validates
logic, tests and the ARM32 guest path, but it can never produce the x86_64
AppImage. The AppImage is built by GitHub Actions on `ubuntu-22.04`.

## 3. Build matrix

### 3.1 Host logic build (development machine, aarch64)

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

This is a **logic/regression** build only. Do not ship it.

### 3.2 ARM32 guest build (NDK cross-compilation)

```sh
ANDROID_NDK_HOME=/opt/android-ndk-r25c \
  bash compat/arm32/tools/build_guest_arm32.sh \
       --ndk "$ANDROID_NDK_HOME" --out build/guest-arm32
qemu-arm build/guest-arm32/guest_smoke_static
```

The script works on both an `x86_64` host (uses the NDK's prebuilt clang) and
an `aarch64` host (uses the system clang with `--target`/`--sysroot`).

Static bionic executables are rejected by QEMU because of an under-aligned
`PT_TLS` segment; `tools/patch_tls_align.py` raises its alignment to 32, after
which they run on any host. This is what makes the shipped guest independent of
any Android device.

### 3.3 Release build (GitHub Actions, x86_64 Ubuntu 22.04)

`.github/workflows/ci.yml`:

1. installs host dependencies and `qemu-user-static`;
2. installs NDK `r25c`;
3. configures with `-DNEXUS_BUILD_GUEST_ARM32=ON`;
4. builds host binaries **and** the ARM32 guest;
5. **verifies with `file`/`readelf`** that the host binaries are `X86-64` and
   that the guest is `ARM`;
6. runs `ctest`;
7. builds the AppImage, bundling `qemu-arm-static` and the guest runtime;
8. extracts the AppImage and checks the bundled translator and guest exist;
9. uploads the AppImage and guest-runtime artifacts.

### 3.4 Architecture verification commands

```sh
# Host: must be ELF64, Machine = Advanced Micro Devices X86-64
file build/nexus
readelf -h build/nexus | grep -E "Class|Machine"

# Guest: must be ELF32, Machine = ARM
file build/guest-arm32/guest_smoke_static
readelf -h build/guest-arm32/guest_smoke_static | grep -E "Class|Machine"
readelf -l build/guest-arm32/guest_smoke_static | grep -i TLS   # align 0x20
```

## 4. Runtime architecture for the x86_64 target

```
Ubuntu 22.04 x86_64
└── NEXUS-MCPE AppImage
    ├── usr/bin/nexus-launcher            (x86_64 ELF)
    ├── usr/bin/qemu-arm-static           (x86_64 ELF, translates ARM32)
    └── usr/share/nexus-mcpe/compat/arm32/
        ├── guest runtime                 (ARM32 ELF, static bionic)
        └── (user-supplied game .so files are NOT bundled)
```

The guest runtime executes the genuine ARM32 game library. It provides:

* a **self-contained ARM32 ELF loader** (own `dlopen`/relocation logic) so the
  user's PC does not need Android's dynamic linker;
* the **Bionic ABI surface** actually used by the game (`malloc`, pthreads,
  `__android_log_*`, `__system_property_*`, `dl*`, `operator new/delete`, …);
* the **JNI/JavaVM shims** for the Java classes the engine binds to;
* **EGL/GLES2/OpenSLES client stubs** that forward to the x86_64 host over IPC
  (a 32-bit process cannot load 64-bit host libraries);
* input and lifecycle glue.

Verified on the genuine library: `libminecraftpe.so` dlopens and
`JNI_OnLoad` completes (returns `0x10004`), after binding 15 Java classes,
59 methods and 10 static methods. See
`analysis/reverse-engineering/JNI_RUNTIME_TRACE.md`.

## 5. Evaluated runtime options (why this design)

| Option | Verdict |
| --- | --- |
| Ship an Android bionic linker + `/system/lib` | Most faithful, but AOSP bionic is awkward to build/bundle standalone and ties the artifact to a specific Android runtime build. |
| **Static bionic guest + in-tree ELF loader** (chosen) | Self-contained, buildable entirely with the NDK, no device dependency, no TLS in the game libraries, no `dlopen` in the engine. Proven to run under QEMU on any host. |
| ARM32 glibc + libhybris-style shims | Viable and well-trodden, but needs a full bionic/glibc ABI glue layer and a cross glibc; more moving parts than the static-bionic guest. |
| Desktop OpenGL directly from the guest | Impossible across a 32-bit/64-bit boundary; requires the IPC bridge. |
| Waydroid / Android Emulator / VM | Explicitly rejected by the project requirements. |

The game libraries contain **no `PT_TLS`**, no `__tls_get_addr`, and the engine
contains no `dlopen` (only FMOD does, and that can be stubbed), which is exactly
what makes the static-guest + custom-loader design practical.

## 6. AppImage layout and user-supplied files

Requested distribution layout:

```
NEXUS-MCPE/
├── NEXUS-MCPE-x86_64.AppImage
├── game/          # user supplies the APK / extracted game files
├── mods/
├── scripts/
├── textures/
├── shaders/
└── config/
```

The AppImage never bundles the proprietary APK, `libminecraftpe.so`,
`libfmod.so` or game assets. NEXUS detects them at run time in the documented
provisioning directory. `original/` and `*.apk` are git-ignored.

## 7. Licensing notes

* **QEMU user-mode** — GPLv2. It is bundled as a separate executable and
  invoked as a process; NEXUS does not link against it.
* **Bionic** — Apache-2.0 / BSD. The static guest runtime and the ABI shims are
  built from permissively licensed components.
* **Minecraft PE files** — proprietary, supplied only by the user; never
  redistributed.
* Any third-party source reused must be recorded in `THIRD_PARTY_NOTICES.md`
  with its licence.

## 8. Current status and next step

**Works (verified):**

* host framework, CLI, runtime, bridge and 8/8 tests on the dev host;
* GLES/EGL shims load the real host `libGLESv2`/`libEGL`;
* ARM32 static guest builds with the NDK and runs under `qemu-arm` on any host;
* the genuine `libminecraftpe.so` loads and `JNI_OnLoad` completes;
* QEMU is auto-detected as `qemu-arm-static` **or** `qemu-arm` (previously a
  false positive treated a shell "not found" message as success).

**Not yet done (the real remaining work):**

1. the in-tree ARM32 ELF loader (currently the dev harness relies on a device
   bionic linker — development only, never shipped);
2. Bionic ABI shims for the engine's imports (`operator new/delete`, zlib,
   `__android_log_*`, property service, …);
3. `ANativeActivity_onCreate` + `android_app` lifecycle;
4. the EGL/GLES2 IPC bridge to the x86_64 host GPU;
5. audio (`libOpenSLES`) and input bridges;
6. first real rendered frame of the genuine menu.

The next implementation step is the ARM32 ELF loader, because it is the piece
that removes the last dependency on the development device and makes the
x86_64 AppImage able to load the user's game library.
