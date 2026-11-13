# Execution strategies

NEXUS never assumes that an Android binary can run on a desktop just because the
CPU architecture looks compatible. The planner
(`nexus::platform::select_execution_plan`) uses the APK manifest, the ELF
headers, the host architecture and the tools actually present on the host.

## 0.14.3 build map

| Version code | Architecture | Notes |
| --- | --- | --- |
| `720` | ARMv7 | very early 0.14.x build |
| `740` | ARMv7 | 0.14.x build |
| `760140300` | ARMv7 (Jelly Bean) | **the supplied APK** |
| `760140301` | ARMv7 (Jelly Bean) | ARM variant |
| `780140300` | **x86** | required for x86/x86_64 desktop execution |
| `780140301` | **x86** | required for x86/x86_64 desktop execution |

The supplied `minecraft-0.14.3.apk` is `760140300` and contains only
`lib/armeabi-v7a/*.so`. The x86 build is a different APK and must be obtained
legitimately by the user.

## Strategy matrix

| APK | Host | Strategy | Status |
| --- | --- | --- | --- |
| x86 (`780140300/301`) | x86_64 Linux + Waydroid/Anbox | Android container, x86 | **recommended, viable when the runtime is installed** |
| x86 | x86_64 Linux without Android runtime | — | blocked: install Waydroid/Anbox + an Android image |
| ARMv7 (`760140300`) | x86_64 Linux | ARM system emulation (qemu-system + Android image) | documented, experimental, not automated |
| ARMv7 | x86_64 Linux | qemu-user + host glibc | **blocked**: bionic/EGL/GLES userspace mismatch |
| ARMv7 | aarch64 Linux | Android container with ARM image | **blocked** on this host: no ARM Android userspace |
| ARMv7 | aarch64 Linux | qemu-user | **blocked**: same bionic/ABI problem |
| either | either | Windows PC-port binary under Wine | documented external target; the port is not part of this repo |
| either | either | native x86_64 port of the game | not available; requires a full reimplementation |

The current host (Alpine Linux aarch64, no Waydroid/Anbox/adb/wine/qemu) reports
`android-container-arm` with confidence `blocked` for the supplied APK. That is
the honest result.

## Why ARM on x86_64 does not work with qemu-user

`libminecraftpe.so` is an Android shared object:

- it is linked against **bionic** (`libc.so`, `libm.so`, `liblog.so`,
  `libandroid.so`), not glibc;
- it expects Android's dynamic linker and property system;
- it loads `/system/lib/libEGL.so`, `/system/lib/libGLESv2.so` and vendor GL
  drivers;
- it uses Android `JNI_OnLoad` lifecycle and app data paths.

`qemu-arm` can translate the CPU instructions, but it cannot supply the Android
userspace those libraries require. Running it under a glibc process fails at
load time, before any NEXUS hook could help. A full ARM Android system image
under `qemu-system-aarch64`/`qemu-system-arm` is a legitimate emulation path,
but it is slow, needs a display and GPU forwarding, and was not available on
this host.

## Recommended path for a desktop user

1. Obtain the **x86** 0.14.3 APK (`780140300` or `780140301`).
2. Install Waydroid (or Anbox) on an x86_64 Linux host.
3. Check the host:

   ```sh
   nexus doctor
   nexus apk verify /path/to/x86.apk   # must exit 0
   nexus strategy /path/to/x86.apk     # must report viable
   ```

4. Import and launch:

   ```sh
   nexus install import /path/to/x86.apk --extract-assets
   nexus launch <install-id>            # prints a plan, does not execute
   nexus launch <install-id> --execute  # asks for confirmation
   ```

5. Start the bridge host for asset/shader/mod queries:

   ```sh
   nexus-runtime --headless --assets ~/.nexus-mcpe/installs/<id>/assets &
   nexus bridge status
   ```

## What the launcher automates

- `AndroidContainerX86`: `waydroid app install` followed by
  `waydroid app launch com.mojang.minecraftpe`, plus a background
  `nexus-runtime`.
- `WindowsPcPortWine`: `wine <pc-port.exe>` plus `nexus-runtime`.
- `NativePort`: executes a user-provided native binary plus `nexus-runtime`.
- Other strategies are printed with their requirements and blockers; the
  launcher does not pretend to automate them.

No command is executed unless the user passes `--execute` and confirms (or
passes `--yes` in a non-interactive context).

## In-game integration still required

Starting the game is not the same as applying a shader or a mod. Asset overlays
and cooperative bridge calls work at the file/API level. Changing the running
renderer or injecting blocks into the live world requires the game-side helper
described in [`INJECTION-LEVELS.md`](INJECTION-LEVELS.md).
