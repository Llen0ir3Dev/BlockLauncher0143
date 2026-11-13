#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace nexus::platform {

struct SystemInfo {
    std::string os_name;
    std::string os_version;
    std::string kernel;
    std::string host_architecture;
    std::string host_machine;
    int cpu_count = 0;
    uint64_t total_memory_mb = 0;
    bool display_server = false;
    std::string display_server_type;
    bool has_wayland = false;
    bool has_x11 = false;
    bool is_wsl = false;
    bool is_container = false;
    std::string desktop;
    std::vector<std::string> cpu_features;
    nlohmann::json to_json() const;
};

SystemInfo system_info();
std::string host_architecture();

// PATH lookup without invoking a shell.
bool find_executable(const std::string& name, std::string& path);
bool command_exists(const std::string& name);
std::string executable_version(const std::string& path, const std::string& argument = "--version");

bool is_android_runtime_available(std::string* runtime = nullptr);
bool is_emulator_available(std::string* emulator = nullptr);
bool is_wine_available(std::string* wine = nullptr);

struct RuntimeCapabilities {
    bool waydroid = false;
    bool anbox = false;
    bool adb = false;
    bool qemu_arm = false;
    bool qemu_system_arm = false;
    bool wine = false;
    bool weston = false;
    bool xvfb = false;
    bool sdl = false;
    std::string waydroid_path;
    std::string adb_path;
    std::string qemu_path;
    std::string wine_path;
    nlohmann::json to_json() const;
};

RuntimeCapabilities detect_runtime_capabilities();

} // namespace nexus::platform
