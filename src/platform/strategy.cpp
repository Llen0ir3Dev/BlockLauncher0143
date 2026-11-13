#include <nexus/platform/strategy.hpp>

#include <algorithm>
#include <sstream>

#include <nexus/core/strings.hpp>
#include <nexus/core/version.hpp>

namespace nexus::platform {

const char* execution_strategy_name(ExecutionStrategy strategy) {
    switch (strategy) {
        case ExecutionStrategy::AndroidContainerX86: return "android-container-x86";
        case ExecutionStrategy::AndroidContainerArm: return "android-container-arm";
        case ExecutionStrategy::ArmSystemEmulation: return "arm-system-emulation";
        case ExecutionStrategy::ArmUserModeShim: return "arm-user-mode-shim";
        case ExecutionStrategy::WindowsPcPortWine: return "windows-pc-port-wine";
        case ExecutionStrategy::NativePort: return "native-port";
        case ExecutionStrategy::None: break;
    }
    return "none";
}

namespace {

bool host_is_x86_64(const SystemInfo& system) {
    const std::string arch = to_lower(system.host_architecture);
    return arch == "x86_64" || arch == "amd64";
}

bool host_is_aarch64(const SystemInfo& system) {
    const std::string arch = to_lower(system.host_architecture);
    return arch == "aarch64" || arch == "arm64";
}

void add_common_integration(ExecutionPlan& plan) {
    plan.integration_points = {
        "Launcher <-> bridge agent: newline-delimited JSON over a Unix socket "
        "(nexus bridge protocol v1).",
        "Asset overlay: translated shader/material/texture files are mounted or "
        "copied into the game's asset search path without modifying the APK.",
        "GLES interception: an EGL/GLES shim can replace glShaderSource and "
        "texture uploads to apply translated shaders at runtime.",
        "Mod API: the bridge agent exposes the NEXUS mod ABI to the running "
        "process; native hooks require ABI-matched binary instrumentation.",
        "Launcher-side worldgen extensions run in NEXUS and can produce region "
        "files only where the target runtime supports importing them."
    };
}

ExecutionPlan plan_for_x86_x86_64(const ApkInfo& apk, const SystemInfo& system,
                                  const RuntimeCapabilities& caps) {
    ExecutionPlan plan;
    plan.strategy = ExecutionStrategy::AndroidContainerX86;
    plan.title = "x86 Android container on x86_64 Linux";
    plan.confidence = "documented";
    plan.summary =
        "The APK contains x86 native libraries (version code " +
        std::to_string(apk.manifest.version_code) + "). On an x86_64 host those "
        "32-bit x86 libraries can execute under an Android userspace such as "
        "Waydroid or Anbox, with the host GLES driver passed through. This is the "
        "most realistic path: the original game binary remains the game client.";
    plan.requirements = {
        "x86_64 Linux host with 32-bit x86 support enabled in the kernel",
        "Waydroid (preferred) or Anbox installed and initialised",
        "A host GLES 2.0/3.0 driver (Mesa llvmpipe is sufficient for validation)",
        "The user-supplied x86 0.14.3 APK (version code 780140300 or 780140301)"
    };
    plan.steps = {
        "Verify the APK with `nexus apk verify <apk>` and confirm the x86 ABI.",
        "Initialise the Android runtime (`waydroid init`) and start the session.",
        "Install the APK (`waydroid app install <apk>`); never modify the APK.",
        "Launch com.mojang.minecraftpe.MainActivity from the launcher.",
        "Attach the NEXUS bridge agent to the game process; the agent exposes "
        "events, asset overlays and the shader/texture integration points.",
        "Translate a shader pack, write it to the overlay, and request a reload."
    };
    plan.needs_android_runtime = true;
    plan.needs_bridge_agent = true;
    plan.needs_binary_hooks = true;
    plan.game_files_present = true;
    if (!caps.waydroid && !caps.anbox) {
        plan.viable = false;
        plan.blockers.push_back(
            "No Android runtime (waydroid/anbox) was found on this host. Install "
            "Waydroid and a matching x86 Android image; `nexus doctor` reports the "
            "detected paths.");
    } else {
        plan.viable = true;
    }
    if (apk.manifest.version_code != 780140300 && apk.manifest.version_code != 780140301) {
        plan.viable = false;
        plan.blockers.push_back(
            "x86 libraries are present but the version code is not a verified x86 "
            "0.14.3 code (expected 780140300 or 780140301).");
    }
    if (!host_is_x86_64(system)) {
        plan.viable = false;
        plan.blockers.push_back(
            "The host is " + system.host_architecture +
            ", so x86 Android libraries cannot execute natively; use an x86_64 host "
            "or full-system emulation.");
    }
    add_common_integration(plan);
    return plan;
}

ExecutionPlan plan_for_arm_on_x86_64(const ApkInfo& apk, const SystemInfo& system,
                                     const RuntimeCapabilities& caps) {
    ExecutionPlan plan;
    plan.strategy = ExecutionStrategy::ArmSystemEmulation;
    plan.title = "ARM APK on x86_64: emulation or an x86 APK";
    plan.confidence = "blocked";
    plan.summary =
        "The supplied APK is an " +
        std::string(apk.has_abi("armeabi-v7a") ? "armeabi-v7a" : "ARM") +
        " build (version code " + std::to_string(apk.manifest.version_code) +
        "). Its libminecraftpe.so is an ELF32 ARM binary linked against Android "
        "bionic libraries. It cannot be loaded by an x86_64 Linux process, and "
        "qemu-user alone is insufficient because the Android linker, libc, EGL, "
        "GLESv2 and OpenSLES dependencies are not glibc-compatible.";
    plan.requirements = {
        "Preferred: a legitimate x86 0.14.3 APK (versionCode 780140300/780140301)",
        "Alternative: an ARM Android system image and full-system emulator "
        "(qemu-system-arm/aarch64) with a working GPU or software GLES stack",
        "For desktop integration: an ABI-matched bridge agent built inside the "
        "Android runtime"
    };
    plan.steps = {
        "Keep the ARM APK as analysis input; it is not a runnable Linux target.",
        "Obtain the x86 0.14.3 APK from an authorized source and re-run "
        "`nexus apk verify`.",
        "If x86 files cannot be obtained, create an ARM Android VM and install "
        "the ARM APK there; expect low performance and no direct host integration.",
        "Do not attempt to link libminecraftpe.so into a glibc process."
    };
    plan.blockers.push_back(
        "Host architecture (" + system.host_architecture +
        ") does not match the APK ABI (ARM 32-bit).");
    plan.blockers.push_back(
        "The ARM binary depends on Android bionic libraries; qemu-user with a "
        "glibc rootfs cannot satisfy those dependencies.");
    plan.blockers.push_back(
        "The public 0.14.3 PC port ships only a Windows executable; no Linux "
        "0.14.3 source or x86_64 library is available in this project.");
    if (caps.qemu_system_arm) {
        plan.steps.push_back(
            "qemu-system-arm was detected. A full ARM Android image is still "
            "required; none is bundled or downloaded by NEXUS.");
        plan.integration_points.push_back(
            "Emulated runtime: ADB forwarding to the bridge agent inside the VM.");
    } else {
        plan.steps.push_back(
            "Install qemu-system-arm/aarch64 only if full-system emulation is "
            "acceptable; it is not a substitute for x86 native execution.");
    }
    plan.needs_emulation = true;
    plan.needs_android_runtime = true;
    plan.needs_bridge_agent = true;
    plan.needs_binary_hooks = true;
    plan.game_files_present = true;
    add_common_integration(plan);
    return plan;
}

ExecutionPlan plan_for_arm_on_aarch64(const ApkInfo& apk, const SystemInfo& system,
                                      const RuntimeCapabilities& caps) {
    ExecutionPlan plan;
    plan.strategy = ExecutionStrategy::AndroidContainerArm;
    plan.title = "32-bit ARM APK on aarch64 Linux";
    plan.confidence = "blocked";
    plan.summary =
        "The host is aarch64 but the APK is a 32-bit ARM Android build. A native "
        "aarch64 Linux process still cannot load it because it is linked against "
        "Android bionic, not glibc, and the ABI width differs. An ARM Android "
        "userspace (or full-system emulation) is required.";
    plan.requirements = {
        "32-bit ARM Android userspace or an ARM Android system image",
        "An authorized x86 APK if the target is an x86_64 desktop"
    };
    plan.blockers.push_back(
        "Android bionic/EGL/GLES dependencies are not available to a glibc "
        "process.");
    plan.blockers.push_back(
        "Waydroid on aarch64 requires an ARM Android image and is not a generic "
        "Linux compatibility layer.");
    plan.steps = {
        "Use the ARM APK only for analysis on this host.",
        "Run it in an ARM Android VM if required, or obtain an x86 APK for "
        "x86_64 Linux."
    };
    plan.needs_emulation = true;
    plan.needs_android_runtime = true;
    plan.needs_bridge_agent = true;
    plan.needs_binary_hooks = true;
    plan.game_files_present = true;
    add_common_integration(plan);
    return plan;
}

} // namespace

nlohmann::json ExecutionPlan::to_json() const {
    return {
        {"strategy", execution_strategy_name(strategy)},
        {"title", title}, {"viable", viable}, {"confidence", confidence},
        {"summary", summary}, {"requirements", requirements}, {"blockers", blockers},
        {"steps", steps}, {"integrationPoints", integration_points},
        {"needsEmulation", needs_emulation},
        {"needsAndroidRuntime", needs_android_runtime},
        {"needsBridgeAgent", needs_bridge_agent},
        {"needsBinaryHooks", needs_binary_hooks},
        {"gameFilesPresent", game_files_present}
    };
}

std::string ExecutionPlan::to_markdown() const {
    std::ostringstream out;
    out << "# Execution strategy: " << title << "\n\n";
    out << "- Strategy: `" << execution_strategy_name(strategy) << "`\n";
    out << "- Viable now: " << (viable ? "yes" : "no") << "\n";
    out << "- Confidence: " << confidence << "\n";
    out << "- Needs emulation: " << (needs_emulation ? "yes" : "no") << "\n";
    out << "- Needs Android runtime: " << (needs_android_runtime ? "yes" : "no") << "\n";
    out << "- Needs bridge agent: " << (needs_bridge_agent ? "yes" : "no") << "\n";
    out << "- Needs ABI-matched binary hooks: " << (needs_binary_hooks ? "yes" : "no") << "\n\n";
    out << summary << "\n\n";
    if (!requirements.empty()) {
        out << "## Requirements\n\n";
        for (const auto& item : requirements) out << "- " << item << "\n";
        out << "\n";
    }
    if (!blockers.empty()) {
        out << "## Blockers\n\n";
        for (const auto& item : blockers) out << "- " << item << "\n";
        out << "\n";
    }
    if (!steps.empty()) {
        out << "## Steps\n\n";
        for (size_t i = 0; i < steps.size(); ++i) out << (i + 1) << ". " << steps[i] << "\n";
        out << "\n";
    }
    if (!integration_points.empty()) {
        out << "## Integration points\n\n";
        for (const auto& item : integration_points) out << "- " << item << "\n";
    }
    return out.str();
}

ExecutionPlan select_execution_plan(const ApkInfo& apk, const SystemInfo& system,
                                    const RuntimeCapabilities& capabilities,
                                    const std::string& pc_port_executable) {
    const bool apk_x86 = apk.has_x86();
    const bool apk_arm = apk.has_arm();
    const bool host_x86_64 = host_is_x86_64(system);
    const bool host_aarch64 = host_is_aarch64(system);

    if (!apk.valid_apk) {
        ExecutionPlan plan;
        plan.title = "No valid APK installed";
        plan.summary = "Install the user-supplied Minecraft PE 0.14.3 APK first. "
                       "NEXUS never downloads proprietary game files.";
        plan.blockers = apk.issues;
        plan.steps = {
            "Place an authorized APK on this machine.",
            "Run `nexus apk verify <path>` and confirm package com.mojang.minecraftpe, "
            "version 0.14.3 and the expected ABI.",
            "Import it into the launcher with `nexus install import <path>`."
        };
        return plan;
    }

    if (apk_x86 && host_x86_64) return plan_for_x86_x86_64(apk, system, capabilities);

    if (apk_arm && host_x86_64) {
        ExecutionPlan plan = plan_for_arm_on_x86_64(apk, system, capabilities);
        if (!pc_port_executable.empty() && capabilities.wine) {
            plan.integration_points.push_back(
                "A Windows 0.14.3 PC-port executable and Wine were found. It can "
                "launch the game, but it is not a Linux-native build and the port "
                "source is unavailable; NEXUS can manage it as an external "
                "compatibility target.");
        }
        return plan;
    }

    if (apk_arm && host_aarch64) return plan_for_arm_on_aarch64(apk, system, capabilities);

    if (!pc_port_executable.empty()) {
        ExecutionPlan plan;
        plan.strategy = ExecutionStrategy::WindowsPcPortWine;
        plan.title = "Windows 0.14.3 PC port through Wine";
        plan.summary = "A Windows executable was found. It can be launched with Wine, "
                       "but this is not a Linux-native port and no 0.14.3 source is "
                       "available. Shader/mod integration would require Windows-side "
                       "binary hooks, which NEXUS does not ship.";
        plan.viable = capabilities.wine;
        plan.confidence = "documented";
        plan.game_files_present = true;
        plan.needs_bridge_agent = true;
        plan.needs_binary_hooks = true;
        if (!capabilities.wine) {
            plan.blockers.push_back("Wine is not installed.");
        }
        plan.requirements = {"Wine 8+", "The user-supplied PC-port executable"};
        plan.steps = {
            "Verify the executable hash and treat it as proprietary.",
            "Launch it through Wine from the launcher.",
            "Do not claim it is the Linux-native runtime."
        };
        add_common_integration(plan);
        return plan;
    }

    ExecutionPlan plan;
    plan.title = "Unsupported host/APK combination";
    plan.summary = "Host architecture " + system.host_architecture +
                   " and APK ABIs do not form a viable execution path.";
    plan.blockers = {
        "No matching ABI and no supported emulation/runtime configuration was found."
    };
    plan.viable = false;
    plan.game_files_present = true;
    return plan;
}

} // namespace nexus::platform
