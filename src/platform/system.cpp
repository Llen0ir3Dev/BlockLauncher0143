#include <nexus/platform/system.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <sys/utsname.h>
#include <unistd.h>

#include <nexus/core/fs.hpp>
#include <nexus/core/strings.hpp>

namespace nexus::platform {

namespace {

std::string read_first_line(const std::string& path) {
    std::ifstream in(path);
    std::string line;
    if (std::getline(in, line)) return trim(line);
    return "";
}

std::string os_pretty_name() {
    std::ifstream in("/etc/os-release");
    std::string line;
    while (std::getline(in, line)) {
        if (starts_with(line, "PRETTY_NAME=")) {
            std::string value = line.substr(std::strlen("PRETTY_NAME="));
            if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
                value = value.substr(1, value.size() - 2);
            }
            return value;
        }
    }
    return "";
}

uint64_t total_memory_mb() {
    std::ifstream in("/proc/meminfo");
    std::string key;
    uint64_t value = 0;
    std::string unit;
    while (in >> key >> value >> unit) {
        if (key == "MemTotal:") return value / 1024;
    }
    return 0;
}

bool in_container() {
    if (fs::exists("/.dockerenv") || fs::exists("/run/.containerenv")) return true;
    const std::string cgroup = read_first_line("/proc/1/cgroup");
    return cgroup.find("docker") != std::string::npos ||
           cgroup.find("lxc") != std::string::npos ||
           cgroup.find("kubepods") != std::string::npos;
}

std::vector<std::string> cpu_features() {
    std::vector<std::string> features;
    std::ifstream in("/proc/cpuinfo");
    std::string line;
    while (std::getline(in, line)) {
        if (starts_with(line, "flags") || starts_with(line, "Features")) {
            const auto colon = line.find(':');
            if (colon == std::string::npos) continue;
            for (const auto& token : split_ws(line.substr(colon + 1))) {
                if (std::find(features.begin(), features.end(), token) == features.end()) {
                    features.push_back(token);
                }
            }
            break;
        }
    }
    return features;
}

std::string quote(const std::string& value) {
    std::string out = "'";
    for (char c : value) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
}

} // namespace

std::string host_architecture() {
    struct utsname info;
    if (uname(&info) != 0) return "unknown";
    return info.machine;
}

SystemInfo system_info() {
    SystemInfo info;
    struct utsname uts;
    if (uname(&uts) == 0) {
        info.kernel = std::string(uts.sysname) + " " + uts.release;
        info.host_machine = uts.machine;
    }
    info.host_architecture = info.host_machine.empty() ? "unknown" : info.host_machine;
    info.os_name = os_pretty_name();
    if (info.os_name.empty()) info.os_name = "unknown Linux";
    const std::string version = read_first_line("/etc/os-release");
    info.os_version = version;
    info.cpu_count = static_cast<int>(sysconf(_SC_NPROCESSORS_ONLN));
    if (info.cpu_count < 1) info.cpu_count = 1;
    info.total_memory_mb = total_memory_mb();
    const char* wayland = std::getenv("WAYLAND_DISPLAY");
    const char* display = std::getenv("DISPLAY");
    info.has_wayland = wayland != nullptr && *wayland != '\0';
    info.has_x11 = display != nullptr && *display != '\0';
    info.display_server = info.has_wayland || info.has_x11;
    if (info.has_wayland) info.display_server_type = info.has_x11 ? "wayland+x11" : "wayland";
    else if (info.has_x11) info.display_server_type = "x11";
    info.desktop = std::getenv("XDG_CURRENT_DESKTOP") != nullptr
                       ? std::getenv("XDG_CURRENT_DESKTOP")
                       : "";
    info.is_wsl = fs::exists("/proc/sys/fs/binfmt_misc/WSLInterop");
    info.is_container = in_container();
    info.cpu_features = cpu_features();
    return info;
}

bool find_executable(const std::string& name, std::string& path) {
    path.clear();
    if (name.empty()) return false;
    if (name.find('/') != std::string::npos) {
        if (access(name.c_str(), X_OK) == 0) {
            path = name;
            return true;
        }
        return false;
    }
    const char* env = std::getenv("PATH");
    if (env == nullptr) return false;
    for (const auto& directory : split(env, ':')) {
        if (directory.empty()) continue;
        const std::string candidate = fs::join(directory, name);
        if (access(candidate.c_str(), X_OK) == 0) {
            path = candidate;
            return true;
        }
    }
    return false;
}

bool command_exists(const std::string& name) {
    std::string path;
    return find_executable(name, path);
}

std::string executable_version(const std::string& path, const std::string& argument) {
    if (path.empty()) return "";
    const std::string command = quote(path) + " " + argument + " 2>/dev/null";
    FILE* pipe = popen(command.c_str(), "r");
    if (pipe == nullptr) return "";
    char buffer[512];
    std::string output;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
        if (output.size() > 4096) break;
    }
    pclose(pipe);
    const auto lines = split(output, '\n');
    return lines.empty() ? "" : trim(lines.front());
}

bool is_android_runtime_available(std::string* runtime) {
    const char* candidates[] = {"waydroid", "anbox", "anbox-container-manager"};
    for (const char* candidate : candidates) {
        std::string path;
        if (find_executable(candidate, path)) {
            if (runtime != nullptr) *runtime = path;
            return true;
        }
    }
    std::string adb;
    if (find_executable("adb", adb)) {
        if (runtime != nullptr) *runtime = adb;
        return true;
    }
    return false;
}

bool is_emulator_available(std::string* emulator) {
    const char* candidates[] = {"qemu-system-arm", "qemu-system-aarch64", "qemu-system-x86_64",
                                "qemu-system-i386"};
    for (const char* candidate : candidates) {
        std::string path;
        if (find_executable(candidate, path)) {
            if (emulator != nullptr) *emulator = path;
            return true;
        }
    }
    return false;
}

bool is_wine_available(std::string* wine) {
    std::string path;
    if (find_executable("wine", path) || find_executable("wine64", path)) {
        if (wine != nullptr) *wine = path;
        return true;
    }
    return false;
}

RuntimeCapabilities detect_runtime_capabilities() {
    RuntimeCapabilities caps;
    std::string path;
    caps.waydroid = find_executable("waydroid", path);
    caps.waydroid_path = caps.waydroid ? path : "";
    caps.anbox = find_executable("anbox", path);
    caps.adb = find_executable("adb", path);
    caps.adb_path = caps.adb ? path : "";
    caps.qemu_arm = find_executable("qemu-arm", path);
    caps.qemu_system_arm = find_executable("qemu-system-arm", path) ||
                           find_executable("qemu-system-aarch64", path);
    if (caps.qemu_system_arm) {
        if (find_executable("qemu-system-arm", path)) caps.qemu_path = path;
        else find_executable("qemu-system-aarch64", path);
        caps.qemu_path = path;
    }
    caps.wine = find_executable("wine", path) || find_executable("wine64", path);
    caps.wine_path = caps.wine ? path : "";
    caps.weston = find_executable("weston", path);
    caps.xvfb = find_executable("xvfb-run", path);
    caps.sdl = true; // The launcher itself links SDL2; this records host-side availability.
    return caps;
}

nlohmann::json SystemInfo::to_json() const {
    return {
        {"os", os_name}, {"osVersion", os_version}, {"kernel", kernel},
        {"architecture", host_architecture}, {"machine", host_machine},
        {"cpuCount", cpu_count}, {"memoryMb", total_memory_mb},
        {"display", display_server}, {"displayType", display_server_type},
        {"wayland", has_wayland}, {"x11", has_x11}, {"wsl", is_wsl},
        {"container", is_container}, {"desktop", desktop},
        {"cpuFeatures", cpu_features}
    };
}

nlohmann::json RuntimeCapabilities::to_json() const {
    return {
        {"waydroid", waydroid}, {"anbox", anbox}, {"adb", adb},
        {"qemuArmUser", qemu_arm}, {"qemuSystemArm", qemu_system_arm},
        {"wine", wine}, {"weston", weston}, {"xvfb", xvfb}, {"sdl", sdl},
        {"paths", {
            {"waydroid", waydroid_path}, {"adb", adb_path},
            {"qemu", qemu_path}, {"wine", wine_path}
        }}
    };
}

} // namespace nexus::platform
