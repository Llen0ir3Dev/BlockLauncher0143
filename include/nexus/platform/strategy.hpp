#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexus/platform/apk.hpp>
#include <nexus/platform/system.hpp>

namespace nexus::platform {

enum class ExecutionStrategy {
    None,
    AndroidContainerX86,
    AndroidContainerArm,
    ArmSystemEmulation,
    ArmUserModeShim,
    WindowsPcPortWine,
    NativePort
};

const char* execution_strategy_name(ExecutionStrategy strategy);

struct ExecutionPlan {
    ExecutionStrategy strategy = ExecutionStrategy::None;
    std::string title;
    bool viable = false;
    std::string confidence; // "verified", "documented", "experimental", "blocked"
    std::string summary;
    std::vector<std::string> requirements;
    std::vector<std::string> blockers;
    std::vector<std::string> steps;
    std::vector<std::string> integration_points;
    bool needs_emulation = false;
    bool needs_android_runtime = false;
    bool needs_bridge_agent = false;
    bool needs_binary_hooks = false;
    bool game_files_present = false;
    nlohmann::json to_json() const;
    std::string to_markdown() const;
};

// Chooses the most realistic execution path from facts, not assumptions:
//   - APK architecture and version code (manifest + ELF machine)
//   - host architecture
//   - available Android runtimes, emulators and compatibility layers
//   - optional Windows PC-port executable
//
// The result never claims that the game runs.  It documents what is required
// and which integration mechanism (bridge agent, GLES interception, asset
// overlay) would be used if the requirements are met.
ExecutionPlan select_execution_plan(const ApkInfo& apk, const SystemInfo& system,
                                    const RuntimeCapabilities& capabilities,
                                    const std::string& pc_port_executable = "");

} // namespace nexus::platform
