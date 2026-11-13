#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include <nexus/bridge/agent.hpp>
#include <nexus/bridge/client.hpp>
#include <nexus/bridge/protocol.hpp>
#include <nexus/core/fs.hpp>
#include <nexus/core/log.hpp>
#include <nexus/core/sha256.hpp>
#include <nexus/core/strings.hpp>
#include <nexus/core/time.hpp>
#include <nexus/core/version.hpp>
#include <nexus/mcpe/mcpe.hpp>
#include <nexus/mod/hooks.hpp>
#include <nexus/mod/mod.hpp>
#include <nexus/mod/registry.hpp>
#include <nexus/platform/apk.hpp>
#include <nexus/platform/provisioning.hpp>
#include <nexus/platform/strategy.hpp>
#include <nexus/platform/system.hpp>
#include <nexus/shader/analyzer.hpp>
#include <nexus/shader/compiler.hpp>
#include <nexus/shader/pack.hpp>
#include <nexus/shader/preprocessor.hpp>
#include <nexus/shader/translator.hpp>
#include <nexus/texture/pack.hpp>
#include <nexus/texture/preview.hpp>
#include <nexus/worldgen/extension.hpp>
#include <nexus/worldgen/world.hpp>

namespace {

using nlohmann::json;

const char* kUsage = R"(NEXUS-MCPE - external Minecraft PE 0.14.3 launcher and modding platform

Usage: nexus <command> [arguments]

Commands:
  version                              Print version information
  doctor [--apk PATH]                  Check host prerequisites and runtime options
  apk verify PATH                      Verify a user-supplied APK (never modifies it)
  apk info PATH [--json]               Inspect package, version, ABI and assets
  install import PATH [--root DIR] [--extract-assets] [--overwrite]
  install list [--root DIR] [--json]
  install verify ID [--root DIR]
  install remove ID [--root DIR]
  strategy APK [--pc-port EXE] [--json]
  launch ID [--execute] [--root DIR] [--socket PATH]
  profile list|show|create|delete|path ... 
  shader analyze FILE [--stage auto|vertex|fragment] [--json]
  shader translate FILE [--target gles100|glsl120|es300] [--out FILE] [--json]
  shader compile FILE [--stage ...] [--json]
  shader pack PATH [--out DIR] [--compile] [--target ...] [--json]
  texture inspect PATH [--json]
  texture import PATH --out DIR [--json]
  texture atlas PATH --out FILE [--tile N] [--columns N] [--json]
  mcpe info --assets DIR [--json]
  worldgen SEED [--chunk X,Z] [--height X,Z] [--biome X,Z] [--flat N] [--json]
  mod list DIR [--json]
  mod validate DIR [--json]
  mod run DIR [--allow-native] [--seed N] [--json]
  bridge ping [--socket PATH] [--json]
  bridge status [--socket PATH] [--json]
  bridge call METHOD [--params JSON] [--socket PATH] [--json]

Global: --json, --verbose, --log-file PATH, --help

The original Minecraft PE client is never modified. NEXUS prepares installs,
applies overlays, and talks to the running game through the bridge protocol.
)";

struct ParsedArgs {
    std::vector<std::string> positional;
    std::map<std::string, std::string> values;
    std::set<std::string> flags;

    bool has(const std::string& name) const {
        return flags.count(name) > 0 || values.count(name) > 0;
    }
    std::string get(const std::string& name, const std::string& fallback = "") const {
        const auto it = values.find(name);
        return it == values.end() ? fallback : it->second;
    }
    int get_int(const std::string& name, int fallback = 0) const {
        const auto it = values.find(name);
        if (it == values.end()) return fallback;
        try {
            return std::stoi(it->second);
        } catch (...) {
            return fallback;
        }
    }
};

ParsedArgs parse_args(int argc, char** argv, int start,
                      const std::set<std::string>& value_flags,
                      const std::set<std::string>& bool_flags) {
    ParsedArgs parsed;
    for (int i = start; i < argc; ++i) {
        std::string token = argv[i];
        if (token == "--") {
            for (int j = i + 1; j < argc; ++j) parsed.positional.push_back(argv[j]);
            break;
        }
        if (nexus::starts_with(token, "--")) {
            const auto equals = token.find('=');
            if (equals != std::string::npos) {
                parsed.values[token.substr(0, equals)] = token.substr(equals + 1);
                continue;
            }
            if (value_flags.count(token) > 0 && i + 1 < argc) {
                parsed.values[token] = argv[++i];
                continue;
            }
            if (bool_flags.count(token) > 0 || value_flags.count(token) > 0) {
                parsed.flags.insert(token);
                continue;
            }
            // Unknown flags are retained so commands can reject them rather
            // than silently ignoring a typo.
            parsed.flags.insert(token);
            continue;
        }
        parsed.positional.push_back(token);
    }
    return parsed;
}

void print_json(const json& value) {
    std::cout << value.dump(2) << "\n";
}

int fail(const std::string& message, int code = 1) {
    std::cerr << "nexus: " << message << "\n";
    return code;
}

std::string require_positional(const ParsedArgs& args, size_t index,
                               const std::string& what) {
    if (args.positional.size() <= index) {
        throw std::runtime_error("missing " + what);
    }
    return args.positional[index];
}

nexus::platform::GameInstallation find_installation(const std::string& id_or_path,
                                                    const std::string& root) {
    if (nexus::fs::is_regular_file(nexus::fs::join(id_or_path, "installation.json"))) {
        std::string text;
        if (nexus::fs::read_file(nexus::fs::join(id_or_path, "installation.json"), text)) {
            try {
                nexus::platform::GameInstallation installation;
                std::string error;
                if (nexus::platform::GameInstallation::from_json(json::parse(text), installation,
                                                                 error)) {
                    if (installation.root.empty()) installation.root = id_or_path;
                    return installation;
                }
            } catch (const std::exception&) {
            }
        }
    }
    std::vector<nexus::platform::GameInstallation> installations;
    nexus::platform::discover_installations(root, installations);
    for (const auto& installation : installations) {
        if (installation.id == id_or_path) return installation;
    }
    for (const auto& installation : installations) {
        if (nexus::starts_with(installation.id, id_or_path)) return installation;
    }
    throw std::runtime_error("installation not found: " + id_or_path);
}

nexus::shader::ShaderStage detect_stage(const std::string& path,
                                        const std::string& explicit_stage) {
    if (!explicit_stage.empty() && explicit_stage != "auto") {
        nexus::shader::ShaderStage stage = nexus::shader::ShaderStage::Unknown;
        if (!nexus::shader::parse_stage(explicit_stage, stage)) {
            throw std::runtime_error("unknown shader stage: " + explicit_stage);
        }
        return stage;
    }
    std::string ext = nexus::to_lower(nexus::extension(path));
    if (!ext.empty() && ext.front() == '.') ext.erase(ext.begin());
    nexus::shader::ShaderStage stage = nexus::shader::ShaderStage::Unknown;
    if (nexus::shader::parse_stage(ext, stage)) return stage;
    if (ext == "vsh") return nexus::shader::ShaderStage::Vertex;
    if (ext == "fsh" || ext == "pix") return nexus::shader::ShaderStage::Fragment;
    const std::string lower = nexus::to_lower(path);
    if (lower.find("fragment") != std::string::npos) return nexus::shader::ShaderStage::Fragment;
    if (lower.find("vertex") != std::string::npos) return nexus::shader::ShaderStage::Vertex;
    throw std::runtime_error(
        "cannot infer shader stage from '" + path + "'; pass --stage vertex|fragment");
}

std::string load_text(const std::string& path) {
    std::string text;
    if (!nexus::fs::read_file(path, text, 32ull * 1024 * 1024)) {
        throw std::runtime_error("cannot read " + path);
    }
    return text;
}

// MCPE shaders reference shared code with `#include "shaders/util.h"`.  The
// path is relative to the assets root, not to the shader file, so try the
// file's directory, its parent and the working directory.  Unknown includes
// are left for the preprocessor to report.
std::string expand_shader_includes(const std::string& source,
                                   const std::vector<std::string>& roots,
                                   std::set<std::string>& active, int depth) {
    if (depth > 16) return source;
    std::istringstream in(source);
    std::ostringstream out;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string trimmed = nexus::trim(line);
        if (nexus::starts_with(trimmed, "#include")) {
            const size_t open = trimmed.find_first_of("\"<");
            const size_t close =
                open == std::string::npos ? std::string::npos
                                          : trimmed.find_first_of("\">", open + 1);
            if (open != std::string::npos && close != std::string::npos) {
                const std::string relative = trimmed.substr(open + 1, close - open - 1);
                if (nexus::fs::is_safe_relative_path(relative)) {
                    std::string found;
                    for (const auto& root : roots) {
                        const std::string candidate = nexus::fs::join(root, relative);
                        if (nexus::fs::is_regular_file(candidate)) {
                            found = candidate;
                            break;
                        }
                    }
                    if (!found.empty()) {
                        const std::string canonical = nexus::fs::canonical(found);
                        if (active.insert(canonical).second) {
                            std::string included;
                            if (nexus::fs::read_file(found, included, 8ull * 1024 * 1024)) {
                                std::vector<std::string> child_roots;
                                child_roots.push_back(nexus::dirname(found));
                                child_roots.insert(child_roots.end(), roots.begin(), roots.end());
                                out << expand_shader_includes(included, child_roots, active,
                                                              depth + 1);
                            }
                            active.erase(canonical);
                        }
                    }
                }
            }
            out << "\n";
            continue;
        }
        out << line << "\n";
    }
    return out.str();
}

std::string load_shader_text(const std::string& path) {
    const std::string directory = nexus::dirname(path);
    std::vector<std::string> roots;
    roots.push_back(directory);
    roots.push_back(nexus::dirname(directory));
    roots.push_back(nexus::fs::current_dir());
    std::set<std::string> active;
    active.insert(nexus::fs::canonical(path));
    return expand_shader_includes(load_text(path), roots, active, 0);
}

std::string prepare_for_target(const std::string& source, int glsl_version) {
    if (nexus::mcpe::has_multiversion_marker(source)) {
        return nexus::mcpe::prepare_multiversion_source(source, glsl_version);
    }
    return source;
}

int command_version(const ParsedArgs& args) {
    const bool as_json = args.has("--json");
    if (as_json) {
        print_json({{"name", "NEXUS-MCPE"},
                    {"version", nexus::kVersion},
                    {"gameVersion", nexus::kGameVersion},
                    {"protocol", nexus::bridge::kProtocolName},
                    {"modAbi", nexus::kModAbiVersion}});
    } else {
        std::cout << "NEXUS-MCPE " << nexus::kVersion << "\n"
                  << "target game: Minecraft PE " << nexus::kGameVersion << "\n"
                  << "bridge protocol: " << nexus::bridge::kProtocolName << "\n"
                  << "native mod ABI: " << nexus::kModAbiVersion << "\n";
    }
    return 0;
}

int command_doctor(const ParsedArgs& args) {
    const auto system = nexus::platform::system_info();
    const auto caps = nexus::platform::detect_runtime_capabilities();
    json report;
    report["system"] = system.to_json();
    report["capabilities"] = caps.to_json();
    report["tools"] = {
        {"cmake", nexus::platform::command_exists("cmake")},
        {"ninja", nexus::platform::command_exists("ninja")},
        {"glslangValidator", nexus::shader::find_glslang_validator()},
        {"python3", nexus::platform::command_exists("python3")}
    };
    std::vector<nexus::platform::GameInstallation> installations;
    nexus::platform::discover_installations(nexus::platform::default_installs_root(),
                                            installations);
    report["installsRoot"] = nexus::platform::default_installs_root();
    report["installCount"] = installations.size();
    report["installs"] = json::array();
    for (const auto& installation : installations) {
        report["installs"].push_back(installation.to_json());
    }
    if (args.has("--apk")) {
        const auto apk = nexus::platform::probe_apk(args.get("--apk"));
        report["apk"] = apk.to_json();
    }
    report["warnings"] = json::array();
    if (!caps.waydroid && !caps.anbox) {
        report["warnings"].push_back(
            "No Android runtime found. x86 APK execution requires Waydroid or Anbox.");
    }
    if (!system.display_server) {
        report["warnings"].push_back(
            "No display server detected. Headless commands work; the game and SDL "
            "launcher require a display.");
    }
    if (system.host_architecture != "x86_64") {
        report["warnings"].push_back(
            "Host is " + system.host_architecture +
            "; x86 Android game libraries require an x86_64 host or full-system emulation.");
    }
    if (args.has("--json")) {
        print_json(report);
        return 0;
    }
    std::cout << "NEXUS-MCPE doctor\n"
              << "  host:        " << system.os_name << " (" << system.host_architecture << ")\n"
              << "  kernel:      " << system.kernel << "\n"
              << "  cpu:         " << system.cpu_count << " cores, " << system.total_memory_mb
              << " MB RAM\n"
              << "  display:     "
              << (system.display_server ? system.display_server_type : "none") << "\n"
              << "  waydroid:    " << (caps.waydroid ? caps.waydroid_path : "not installed")
              << "\n"
              << "  anbox:       " << (caps.anbox ? "available" : "not installed") << "\n"
              << "  adb:         " << (caps.adb ? caps.adb_path : "not installed") << "\n"
              << "  qemu-user:   " << (caps.qemu_arm ? "available" : "not installed") << "\n"
              << "  qemu-system: " << (caps.qemu_system_arm ? caps.qemu_path : "not installed")
              << "\n"
              << "  wine:        " << (caps.wine ? caps.wine_path : "not installed") << "\n"
              << "  glslang:     "
              << (nexus::shader::find_glslang_validator().empty()
                      ? "not installed (structural validation only)"
                      : nexus::shader::find_glslang_validator())
              << "\n"
              << "  installs:    " << installations.size() << " in "
              << nexus::platform::default_installs_root() << "\n";
    if (args.has("--apk")) {
        const auto apk = nexus::platform::probe_apk(args.get("--apk"));
        std::cout << "\n" << apk.to_markdown();
    }
    for (const auto& warning : report["warnings"]) {
        std::cout << "  warning:     " << warning.get<std::string>() << "\n";
    }
    return 0;
}

int command_apk(int argc, char** argv) {
    if (argc < 3) return fail("usage: nexus apk verify|info PATH");
    const std::string action = argv[2];
    const ParsedArgs args = parse_args(argc, argv, 3, {}, {"--json"});
    try {
        const std::string path = require_positional(args, 0, "APK path");
        const auto apk = nexus::platform::probe_apk(path);
        if (action == "info") {
            if (args.has("--json")) print_json(apk.to_json());
            else std::cout << apk.to_markdown();
            return apk.valid_apk ? 0 : 2;
        }
        if (action == "verify") {
            json report = apk.to_json();
            const bool x86 = apk.has_x86();
            const bool supported = apk.valid_apk &&
                                   nexus::is_supported_x86_version(apk.manifest.version_code);
            report["x86Ready"] = x86;
            report["supportedX86Version"] = supported;
            report["verdict"] = supported ? "supported-x86"
                                          : (apk.valid_apk ? "valid-but-not-runnable-x86"
                                                           : "invalid");
            report["guidance"] = supported
                ? "Install this APK into Waydroid/Anbox and attach the bridge agent."
                : "Keep this APK for analysis. For x86_64 Linux execution, obtain the "
                  "0.14.3 x86 build (versionCode 780140300 or 780140301).";
            if (args.has("--json")) {
                print_json(report);
            } else {
                std::cout << apk.to_markdown();
                std::cout << "\nVerdict: " << report["verdict"].get<std::string>() << "\n"
                          << report["guidance"].get<std::string>() << "\n";
            }
            return supported ? 0 : (apk.valid_apk ? 3 : 2);
        }
        return fail("unknown apk action: " + action);
    } catch (const std::exception& ex) {
        return fail(ex.what());
    }
}

int command_install(int argc, char** argv) {
    if (argc < 3) return fail("usage: nexus install import|list|verify|remove ...");
    const std::string action = argv[2];
    const ParsedArgs args = parse_args(
        argc, argv, 3,
        {"--root", "--name"},
        {"--extract-assets", "--overwrite", "--json", "--no-copy"});
    const std::string root = args.get("--root", nexus::platform::default_installs_root());
    try {
        if (action == "import") {
            const std::string path = require_positional(args, 0, "APK path");
            nexus::platform::InstallationOptions options;
            options.extract_assets = args.has("--extract-assets");
            options.overwrite = args.has("--overwrite");
            options.copy_apk = !args.has("--no-copy");
            nexus::platform::GameInstallation installation;
            std::string error;
            if (!nexus::platform::create_installation(path, root, options, installation, error)) {
                return fail(error);
            }
            if (args.has("--json")) print_json(installation.to_json());
            else {
                std::cout << "Installed " << installation.name << "\n"
                          << "  id:      " << installation.id << "\n"
                          << "  root:    " << installation.root << "\n"
                          << "  sha256:  " << installation.sha256 << "\n"
                          << "  abis:    " << nexus::join(installation.abis, ", ") << "\n"
                          << "  x86:     " << (installation.x86_ready ? "yes" : "no")
                          << "\n";
                for (const auto& warning : installation.warnings) {
                    std::cout << "  warning: " << warning << "\n";
                }
            }
            return 0;
        }
        if (action == "list") {
            std::vector<nexus::platform::GameInstallation> installations;
            if (!nexus::platform::discover_installations(root, installations)) {
                return fail("cannot read installs root: " + root);
            }
            json out = {{"root", root}, {"installs", json::array()}};
            for (const auto& installation : installations) {
                out["installs"].push_back(installation.to_json());
            }
            if (args.has("--json")) print_json(out);
            else {
                std::cout << "Installs in " << root << ":\n";
                if (installations.empty()) std::cout << "  (none)\n";
                for (const auto& installation : installations) {
                    std::cout << "  " << installation.id << "  " << installation.name << "  ["
                              << nexus::join(installation.abis, ",") << "]"
                              << (installation.x86_ready ? " x86-ready" : "") << "\n";
                }
            }
            return 0;
        }
        const std::string id = require_positional(args, 0, "installation id");
        auto installation = find_installation(id, root);
        if (action == "verify") {
            std::string error;
            const bool ok = nexus::platform::verify_installation(installation, error);
            if (args.has("--json")) {
                print_json({{"ok", ok}, {"id", installation.id}, {"error", error}});
            } else if (ok) {
                std::cout << "OK: " << installation.id << " (" << installation.sha256 << ")\n";
            } else {
                std::cerr << "FAILED: " << error << "\n";
            }
            return ok ? 0 : 1;
        }
        if (action == "remove") {
            std::string error;
            if (!nexus::platform::remove_installation(installation, error, root)) {
                return fail(error);
            }
            std::cout << "Removed " << installation.id << "\n";
            return 0;
        }
        return fail("unknown install action: " + action);
    } catch (const std::exception& ex) {
        return fail(ex.what());
    }
}

int command_strategy(int argc, char** argv) {
    const ParsedArgs args =
        parse_args(argc, argv, 2, {"--pc-port"}, {"--json"});
    try {
        const std::string path = require_positional(args, 0, "APK path");
        const auto apk = nexus::platform::probe_apk(path);
        const auto plan = nexus::platform::select_execution_plan(
            apk, nexus::platform::system_info(),
            nexus::platform::detect_runtime_capabilities(), args.get("--pc-port"));
        if (args.has("--json")) print_json(plan.to_json());
        else std::cout << plan.to_markdown();
        return plan.viable ? 0 : 3;
    } catch (const std::exception& ex) {
        return fail(ex.what());
    }
}

bool run_process(const std::vector<std::string>& command, std::string& error) {
    if (command.empty()) {
        error = "empty command";
        return false;
    }
    const pid_t pid = ::fork();
    if (pid < 0) {
        error = std::string("fork failed: ") + std::strerror(errno);
        return false;
    }
    if (pid == 0) {
        std::vector<char*> argv;
        argv.reserve(command.size() + 1);
        for (const auto& item : command) argv.push_back(const_cast<char*>(item.c_str()));
        argv.push_back(nullptr);
        ::execvp(argv[0], argv.data());
        std::fprintf(stderr, "nexus: cannot execute %s: %s\n", command[0].c_str(),
                     std::strerror(errno));
        _exit(127);
    }
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            error = std::string("waitpid failed: ") + std::strerror(errno);
            return false;
        }
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        error = "command exited with status " + std::to_string(WEXITSTATUS(status));
        return false;
    }
    return true;
}

int command_launch(int argc, char** argv) {
    const ParsedArgs real = parse_args(argc, argv, 2, {"--root", "--socket", "--pc-port"},
                                       {"--execute", "--json", "--dry-run"});
    try {
        const std::string id = require_positional(real, 0, "installation id");
        const std::string root = real.get("--root", nexus::platform::default_installs_root());
        auto installation = find_installation(id, root);
        std::string verify_error;
        if (!nexus::platform::verify_installation(installation, verify_error)) {
            return fail("installation failed verification: " + verify_error);
        }
        const auto apk = nexus::platform::probe_apk(installation.apk_path);
        const auto caps = nexus::platform::detect_runtime_capabilities();
        const auto plan = nexus::platform::select_execution_plan(
            apk, nexus::platform::system_info(), caps, real.get("--pc-port"));
        json report = {{"installation", installation.to_json()},
                       {"plan", plan.to_json()},
                       {"executed", false},
                       {"commands", json::array()}};
        std::vector<std::string> commands;
        if (plan.strategy == nexus::platform::ExecutionStrategy::AndroidContainerX86 &&
            caps.waydroid) {
            commands.push_back("waydroid app install " + installation.apk_path);
            commands.push_back("waydroid app launch com.mojang.minecraftpe");
            commands.push_back("nexus-runtime --socket " +
                               (real.get("--socket").empty()
                                    ? nexus::bridge::default_socket_path()
                                    : real.get("--socket")));
        }
        for (const auto& command : commands) report["commands"].push_back(command);
        if (!plan.viable) {
            report["reason"] = plan.blockers.empty() ? "no viable execution plan"
                                                     : plan.blockers.front();
            if (real.has("--json")) print_json(report);
            else {
                std::cerr << plan.to_markdown();
                std::cerr << "\nLaunch refused: no viable execution path on this host.\n";
            }
            return 3;
        }
        const bool execute = real.has("--execute") && !real.has("--dry-run");
        if (!execute) {
            if (real.has("--json")) print_json(report);
            else {
                std::cout << "Launch plan for " << installation.id << ":\n"
                          << plan.to_markdown() << "\nPlanned commands:\n";
                for (const auto& command : commands) std::cout << "  " << command << "\n";
                std::cout << "\nNothing was executed. Re-run with --execute to start the "
                             "external runtime, or use the script in the integration "
                             "documentation.\n";
            }
            return 0;
        }
        std::string error;
        for (const auto& command : commands) {
            std::vector<std::string> parts;
            std::istringstream in(command);
            std::string part;
            while (in >> part) parts.push_back(part);
            if (parts.empty()) continue;
            if (parts[0] == "nexus-runtime") {
                // Background runtime hosting the bridge agent.
                const pid_t pid = ::fork();
                if (pid == 0) {
                    ::execlp("nexus-runtime", "nexus-runtime", "--socket",
                             real.get("--socket", nexus::bridge::default_socket_path()).c_str(),
                             static_cast<char*>(nullptr));
                    _exit(127);
                }
                continue;
            }
            if (!run_process(parts, error)) return fail(error);
        }
        report["executed"] = true;
        if (real.has("--json")) print_json(report);
        else std::cout << "Started " << installation.name << " through the Android runtime.\n";
        return 0;
    } catch (const std::exception& ex) {
        return fail(ex.what());
    }
}

std::string profiles_dir() {
    return nexus::fs::join(nexus::fs::nexus_home(), "profiles");
}

bool valid_profile_name(const std::string& name) {
    if (name.empty() || name.size() > 64) return false;
    for (char c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_' && c != '.') {
            return false;
        }
    }
    return name != "." && name != "..";
}

int command_profile(int argc, char** argv) {
    if (argc < 3) return fail("usage: nexus profile list|show|create|delete|path ...");
    const std::string action = argv[2];
    const ParsedArgs args = parse_args(
        argc, argv, 3,
        {"--install", "--width", "--height", "--shader-pack", "--texture-pack", "--mod"},
        {"--json", "--fullscreen"});
    const std::string directory = profiles_dir();
    try {
        if (action == "path") {
            std::cout << directory << "\n";
            return 0;
        }
        if (action == "list") {
            json profiles = json::array();
            if (nexus::fs::is_directory(directory)) {
                for (const auto& path : nexus::fs::list_files(directory, ".json")) {
                    std::string text;
                    if (!nexus::fs::read_file(path, text)) continue;
                    try {
                        profiles.push_back(json::parse(text));
                    } catch (const std::exception&) {
                    }
                }
            }
            if (args.has("--json")) {
                print_json({{"directory", directory}, {"profiles", profiles}});
            } else {
                std::cout << "Profiles in " << directory << ":\n";
                if (profiles.empty()) std::cout << "  (none)\n";
                for (const auto& profile : profiles) {
                    std::cout << "  " << profile.value("name", "") << "  install="
                              << profile.value("installId", "") << "\n";
                }
            }
            return 0;
        }
        const std::string name = require_positional(args, 0, "profile name");
        if (!valid_profile_name(name)) return fail("invalid profile name: " + name);
        const std::string path = nexus::fs::join(directory, name + ".json");
        if (action == "show") {
            if (!nexus::fs::is_regular_file(path)) return fail("profile not found: " + name);
            const std::string text = load_text(path);
            if (args.has("--json")) std::cout << text;
            else std::cout << json::parse(text).dump(2) << "\n";
            return 0;
        }
        if (action == "create") {
            if (nexus::fs::is_regular_file(path)) {
                return fail("profile already exists: " + name);
            }
            json profile = {
                {"name", name},
                {"installId", args.get("--install")},
                {"width", args.get_int("--width", 1280)},
                {"height", args.get_int("--height", 720)},
                {"fullscreen", args.has("--fullscreen")},
                {"shaderPack", args.get("--shader-pack")},
                {"texturePacks", json::array()},
                {"mods", json::array()},
                {"createdAt", nexus::iso8601_now()}
            };
            if (args.has("--texture-pack")) profile["texturePacks"].push_back(
                args.get("--texture-pack"));
            if (args.has("--mod")) profile["mods"].push_back(args.get("--mod"));
            std::string error;
            if (!nexus::fs::write_file(path, profile.dump(2) + "\n")) {
                return fail("cannot write " + path);
            }
            std::cout << "Created profile " << name << " at " << path << "\n";
            return 0;
        }
        if (action == "delete") {
            if (!nexus::fs::is_regular_file(path)) return fail("profile not found: " + name);
            if (!nexus::fs::remove_file(path)) return fail("cannot remove " + path);
            std::cout << "Removed profile " << name << "\n";
            return 0;
        }
        return fail("unknown profile action: " + action);
    } catch (const std::exception& ex) {
        return fail(ex.what());
    }
}

int command_shader(int argc, char** argv) {
    if (argc < 3) return fail("usage: nexus shader analyze|translate|compile|pack ...");
    const std::string action = argv[2];
    const ParsedArgs args = parse_args(
        argc, argv, 3, {"--stage", "--target", "--out", "--multiversion"},
        {"--json", "--compile", "--quality"});
    try {
        if (action == "pack") {
            const std::string path = require_positional(args, 0, "shader pack path");
            nexus::shader::TranslationOptions options;
            nexus::shader::TargetProfile target = nexus::shader::TargetProfile::GLES100;
            if (!args.get("--target").empty() &&
                !nexus::shader::parse_target(args.get("--target"), target)) {
                return fail("unknown target: " + args.get("--target"));
            }
            options.target = target;
            auto result = nexus::shader::analyze_and_translate_pack(path, options,
                                                                    args.has("--compile"));
            if (!args.get("--out").empty()) {
                std::string error;
                if (!nexus::shader::write_translation_output(result, args.get("--out"), error)) {
                    return fail(error);
                }
            }
            if (args.has("--json")) print_json(result.to_json());
            else std::cout << result.to_markdown();
            return result.unsupported == 0 ? 0 : 4;
        }
        const std::string file = require_positional(args, 0, "shader file");
        const auto stage = detect_stage(file, args.get("--stage"));
        std::string source = load_shader_text(file);
        const int glsl_version = args.get_int("--multiversion",
                                              args.get("--target") == "es300" ? 300 : 100);
        source = prepare_for_target(source, glsl_version);
        if (action == "analyze") {
            nexus::shader::PreprocessOptions preprocess_options;
            preprocess_options.stage = stage;
            preprocess_options.defines["__VERSION__"] = std::to_string(glsl_version);
            const auto pre = nexus::shader::preprocess_glsl(source, preprocess_options);
            if (!pre.success) return fail("preprocess failed: " + pre.error);
            nexus::shader::ParseOptions parse_options;
            parse_options.stage = stage;
            auto parsed = nexus::shader::parse_glsl(pre.source, parse_options);
            if (!parsed.success) return fail("parse failed: " + parsed.error);
            parsed.unit.version = pre.version;
            parsed.unit.es = pre.es;
            const auto analysis = nexus::shader::analyze_shader(parsed.unit, stage);
            if (args.has("--json")) print_json(analysis.to_json());
            else std::cout << analysis.to_markdown();
            return analysis.level == nexus::shader::CompatibilityLevel::Unsupported ? 4 : 0;
        }
        if (action == "translate") {
            nexus::shader::TranslationOptions options;
            nexus::shader::TargetProfile target = nexus::shader::TargetProfile::GLES100;
            if (!args.get("--target").empty() &&
                !nexus::shader::parse_target(args.get("--target"), target)) {
                return fail("unknown target: " + args.get("--target"));
            }
            options.target = target;
            const auto result = nexus::shader::translate_glsl(source, stage, options);
            if (!result.success) {
                if (args.has("--json")) print_json(result.report);
                else std::cerr << "Translation failed: " << result.error << "\n";
                return 1;
            }
            if (!args.get("--out").empty()) {
                if (!nexus::fs::write_file(args.get("--out"), result.source)) {
                    return fail("cannot write " + args.get("--out"));
                }
            }
            if (args.has("--json")) print_json(result.report);
            else if (!args.get("--out").empty()) {
                std::cout << "Wrote " << args.get("--out") << " ("
                          << nexus::shader::compatibility_name(result.level) << ")\n";
            } else {
                std::cout << result.source;
            }
            return 0;
        }
        if (action == "compile") {
            nexus::shader::CompileOptions options;
            options.stage = stage;
            options.glsl_version = glsl_version;
            const auto result = nexus::shader::compile_shader(source, options);
            if (args.has("--json")) {
                print_json({{"success", result.success},
                            {"validatorAvailable", result.validator_available},
                            {"validator", result.validator},
                            {"log", result.log},
                            {"error", result.error},
                            {"exitCode", result.exit_code}});
            } else {
                std::cout << (result.success ? "compile OK" : "compile FAILED") << "\n";
                if (!result.validator_available) {
                    std::cout << "glslangValidator not available; structural validation used.\n";
                } else {
                    std::cout << "validator: " << result.validator << "\n";
                }
                if (!result.log.empty()) std::cout << result.log << "\n";
                if (!result.error.empty()) std::cerr << result.error << "\n";
            }
            return result.success ? 0 : 1;
        }
        return fail("unknown shader action: " + action);
    } catch (const std::exception& ex) {
        return fail(ex.what());
    }
}

int command_texture(int argc, char** argv) {
    if (argc < 3) return fail("usage: nexus texture inspect|import|atlas ...");
    const std::string action = argv[2];
    const ParsedArgs args =
        parse_args(argc, argv, 3, {"--out", "--tile", "--columns"}, {"--json"});
    try {
        const std::string path = require_positional(args, 0, "texture pack path");
        if (action == "inspect") {
            std::string error;
            const auto pack = nexus::texture::inspect_texture_pack(path, &error);
            if (!error.empty() && pack.entries.empty()) return fail(error);
            json report = {{"name", pack.name}, {"root", pack.root}, {"archive", pack.archive},
                           {"kind", static_cast<int>(pack.kind)},
                           {"metadata", pack.metadata}, {"issues", pack.issues},
                           {"entries", json::array()}};
            for (const auto& entry : pack.entries) {
                report["entries"].push_back(
                    {{"source", entry.source_path}, {"target", entry.target_path},
                     {"size", entry.size}, {"valid", entry.image.valid},
                     {"width", entry.image.width}, {"height", entry.image.height},
                     {"error", entry.error}});
            }
            if (args.has("--json")) print_json(report);
            else {
                std::cout << "Texture pack " << pack.name << " (" << pack.entries.size()
                          << " entries)\n";
                for (const auto& entry : pack.entries) {
                    std::cout << "  " << entry.source_path;
                    if (entry.image.valid) {
                        std::cout << "  " << entry.image.width << "x" << entry.image.height;
                    }
                    if (!entry.error.empty()) std::cout << "  ERROR: " << entry.error;
                    std::cout << "\n";
                }
                for (const auto& issue : pack.issues) std::cout << "  issue: " << issue << "\n";
            }
            return 0;
        }
        if (action == "import") {
            const std::string out = args.get("--out");
            if (out.empty()) return fail("texture import requires --out DIR");
            nexus::texture::ImportOptions options;
            const auto result = nexus::texture::import_texture_pack(path, out, options);
            if (args.has("--json")) print_json(result.to_json());
            else std::cout << result.to_markdown();
            return result.success ? 0 : 1;
        }
        if (action == "atlas") {
            const std::string out = args.get("--out");
            if (out.empty()) return fail("texture atlas requires --out FILE");
            const std::string json_path = out + ".json";
            std::string error;
            if (!nexus::texture::build_atlas(path, out, json_path,
                                             static_cast<uint32_t>(args.get_int("--tile", 32)),
                                             static_cast<uint32_t>(args.get_int("--columns", 16)),
                                             error)) {
                return fail(error);
            }
            if (args.has("--json")) {
                print_json({{"ok", true}, {"png", out}, {"atlas", json_path}});
            } else {
                std::cout << "Wrote " << out << " and " << json_path << "\n";
            }
            return 0;
        }
        return fail("unknown texture action: " + action);
    } catch (const std::exception& ex) {
        return fail(ex.what());
    }
}

int command_mcpe(int argc, char** argv) {
    if (argc < 3 || std::string(argv[2]) != "info") {
        return fail("usage: nexus mcpe info --assets DIR [--json]");
    }
    const ParsedArgs args = parse_args(argc, argv, 3, {"--assets"}, {"--json"});
    try {
        const std::string assets = args.get("--assets");
        if (assets.empty()) return fail("mcpe info requires --assets DIR");
        const auto summary = nexus::mcpe::inspect_assets(assets);
        if (args.has("--json")) print_json(summary.to_json());
        else std::cout << summary.to_markdown();
        return summary.materials.pass_count > 0 ? 0 : 1;
    } catch (const std::exception& ex) {
        return fail(ex.what());
    }
}

int command_worldgen(int argc, char** argv) {
    const ParsedArgs args = parse_args(argc, argv, 2, {"--chunk", "--height", "--biome", "--flat"},
                                       {"--json"});
    try {
        const std::string seed_text = args.positional.empty() ? "0" : args.positional[0];
        const uint64_t seed = std::stoull(seed_text, nullptr, 0);
        nexus::worldgen::WorldSettings settings;
        settings.seed = seed;
        nexus::worldgen::WorldGenerator world(seed, settings);
        if (args.has("--flat")) {
            world.extensions().add(std::make_shared<nexus::worldgen::FlatTerrainExtension>(
                std::max(1, args.get_int("--flat", 8))));
        }
        auto parse_pair = [](const std::string& text, int& x, int& z) {
            const auto comma = text.find(',');
            if (comma == std::string::npos) return false;
            try {
                x = std::stoi(text.substr(0, comma));
                z = std::stoi(text.substr(comma + 1));
                return true;
            } catch (...) {
                return false;
            }
        };
        if (args.has("--chunk")) {
            int cx = 0;
            int cz = 0;
            if (!parse_pair(args.get("--chunk"), cx, cz)) return fail("--chunk expects X,Z");
            const auto chunk = world.generate_chunk(cx, cz);
            const auto summary = world.summarize(chunk);
            json out = {{"seed", seed}, {"chunk", {{"x", cx}, {"z", cz}}},
                        {"summary", {{"minHeight", summary.minimum_height},
                                     {"maxHeight", summary.maximum_height},
                                     {"solid", summary.solid_blocks},
                                     {"water", summary.water_blocks},
                                     {"lava", summary.lava_blocks},
                                     {"ores", summary.ore_blocks},
                                     {"caves", summary.cave_blocks},
                                     {"biomes", summary.biome_count},
                                     {"dominantBiome", nexus::worldgen::biome_name(
                                          static_cast<nexus::worldgen::BiomeType>(
                                              summary.dominant_biome))}}}};
            if (args.has("--json")) print_json(out);
            else {
                std::cout << "Seed " << seed << " chunk " << cx << "," << cz << "\n"
                          << "  height: " << summary.minimum_height << ".."
                          << summary.maximum_height << "\n"
                          << "  solid: " << summary.solid_blocks
                          << " water: " << summary.water_blocks
                          << " lava: " << summary.lava_blocks << "\n"
                          << "  ores: " << summary.ore_blocks
                          << " caves: " << summary.cave_blocks << "\n"
                          << "  dominant biome: "
                          << nexus::worldgen::biome_name(
                                 static_cast<nexus::worldgen::BiomeType>(summary.dominant_biome))
                          << "\n";
            }
            return 0;
        }
        if (args.has("--height")) {
            int x = 0;
            int z = 0;
            if (!parse_pair(args.get("--height"), x, z)) return fail("--height expects X,Z");
            const int height = world.height_at(x, z);
            if (args.has("--json")) print_json({{"x", x}, {"z", z}, {"height", height}});
            else std::cout << "height " << height << "\n";
            return 0;
        }
        if (args.has("--biome")) {
            int x = 0;
            int z = 0;
            if (!parse_pair(args.get("--biome"), x, z)) return fail("--biome expects X,Z");
            const auto biome = world.biome_at(x, z);
            if (args.has("--json")) {
                print_json({{"x", x}, {"z", z}, {"biome", nexus::worldgen::biome_name(biome)}});
            } else {
                std::cout << nexus::worldgen::biome_name(biome) << "\n";
            }
            return 0;
        }
        json out = {{"seed", seed}, {"chunks", json::array()}};
        for (int cz = -1; cz <= 1; ++cz) {
            for (int cx = -1; cx <= 1; ++cx) {
                const auto summary = world.summarize(world.generate_chunk(cx, cz));
                out["chunks"].push_back({{"x", cx}, {"z", cz},
                                         {"minHeight", summary.minimum_height},
                                         {"maxHeight", summary.maximum_height},
                                         {"dominantBiome", nexus::worldgen::biome_name(
                                              static_cast<nexus::worldgen::BiomeType>(
                                                  summary.dominant_biome))}});
            }
        }
        if (args.has("--json")) print_json(out);
        else {
            std::cout << "World seed " << seed << " (3x3 chunks)\n";
            for (const auto& chunk : out["chunks"]) {
                std::cout << "  " << chunk["x"].get<int>() << "," << chunk["z"].get<int>()
                          << "  " << chunk["minimumHeight"].get<int>() << ".."
                          << chunk["maximumHeight"].get<int>() << "  "
                          << chunk["dominantBiome"].get<std::string>() << "\n";
            }
        }
        return 0;
    } catch (const std::exception& ex) {
        return fail(ex.what());
    }
}

std::vector<std::string> collect_mod_directories(const std::string& path) {
    std::vector<std::string> out;
    if (nexus::fs::is_regular_file(nexus::fs::join(path, "mod.json"))) {
        out.push_back(path);
        return out;
    }
    for (const auto& directory : nexus::fs::list_directories(path)) {
        if (nexus::fs::is_regular_file(nexus::fs::join(directory, "mod.json"))) {
            out.push_back(directory);
        }
    }
    return out;
}

int command_mod(int argc, char** argv) {
    if (argc < 3) return fail("usage: nexus mod list|validate|run DIR");
    const std::string action = argv[2];
    const ParsedArgs args =
        parse_args(argc, argv, 3, {"--seed"}, {"--json", "--allow-native"});
    try {
        const std::string path = require_positional(args, 0, "mod directory");
        if (action == "list" || action == "validate") {
            json mods = json::array();
            bool all_ok = true;
            for (const auto& directory : collect_mod_directories(path)) {
                nexus::mod::ModManifest manifest;
                std::string error;
                const bool ok = nexus::mod::load_manifest(directory, manifest, error);
                if (!ok) all_ok = false;
                mods.push_back({{"directory", directory}, {"ok", ok},
                                {"id", manifest.id}, {"name", manifest.name},
                                {"version", manifest.version},
                                {"type", nexus::mod::mod_type_name(manifest.type)},
                                {"entry", manifest.entry}, {"error", error}});
            }
            if (args.has("--json")) print_json({{"path", path}, {"mods", mods}});
            else {
                std::cout << "Mods in " << path << ":\n";
                if (mods.empty()) std::cout << "  (none)\n";
                for (const auto& mod : mods) {
                    std::cout << "  " << (mod["ok"].get<bool>() ? "OK  " : "FAIL")
                              << "  " << mod["id"].get<std::string>() << " "
                              << mod["version"].get<std::string>() << " ["
                              << mod["type"].get<std::string>() << "]";
                    if (!mod["error"].get<std::string>().empty()) {
                        std::cout << "  " << mod["error"].get<std::string>();
                    }
                    std::cout << "\n";
                }
            }
            return all_ok ? 0 : 1;
        }
        if (action == "run") {
            nexus::EventBus events;
            nexus::mod::ContentRegistry registry;
            nexus::mod::HookManager hooks;
            nexus::mod::ModHostOptions options;
            options.allow_native = args.has("--allow-native");
            nexus::mod::ModHost host(events, registry, hooks, options);
            nexus::worldgen::WorldGenerator world(
                static_cast<uint64_t>(std::max(0, args.get_int("--seed", 0))));
            host.set_world(&world);
            size_t discovered = 0;
            if (nexus::fs::is_regular_file(nexus::fs::join(path, "mod.json"))) {
                std::string error;
                if (!host.add_manifest_path(path, error)) return fail(error);
                discovered = 1;
            } else {
                discovered = host.discover(path);
            }
            json failures = json::array();
            for (const auto& record : host.mods()) {
                std::string error;
                if (!host.enable(record.manifest.id, error)) {
                    failures.push_back({{"id", record.manifest.id}, {"error", error}});
                }
            }
            host.emit("runtime.start", {{"gameVersion", nexus::kGameVersion},
                                        {"seed", world.seed()}});
            auto report = host.to_json();
            report["discovered"] = discovered;
            report["failures"] = failures;
            if (args.has("--json")) print_json(report);
            else {
                std::cout << "Loaded " << host.active_count() << "/" << discovered
                          << " mods from " << path << "\n";
                for (const auto& mod : report["mods"]) {
                    std::cout << "  " << mod["id"].get<std::string>() << "  "
                              << mod["state"].get<std::string>() << "  hooks="
                              << mod["hookCount"].get<int>() << "\n";
                }
                for (const auto& failure : failures) {
                    std::cerr << "  failed " << failure["id"].get<std::string>() << ": "
                              << failure["error"].get<std::string>() << "\n";
                }
                std::cout << "Registry: " << registry.blocks().size() << " blocks, "
                          << registry.items().size() << " items, "
                          << registry.entities().size() << " entities\n";
            }
            host.unload_all();
            return failures.empty() ? 0 : 1;
        }
        return fail("unknown mod action: " + action);
    } catch (const std::exception& ex) {
        return fail(ex.what());
    }
}

int command_bridge(int argc, char** argv) {
    if (argc < 3) return fail("usage: nexus bridge ping|status|call [--socket PATH]");
    const std::string action = argv[2];
    const ParsedArgs args = parse_args(argc, argv, 3, {"--socket", "--params"}, {"--json"});
    try {
        nexus::bridge::BridgeClientOptions options;
        options.socket_path = args.get("--socket", nexus::bridge::default_socket_path());
        options.timeout_ms = 3000;
        nexus::bridge::BridgeClient client(options);
        std::string error;
        if (!client.connect(error)) return fail(error);
        if (action == "ping") {
            std::string version;
            if (!client.ping(&version, error)) return fail(error);
            if (args.has("--json")) {
                print_json({{"ok", true}, {"socket", options.socket_path},
                            {"version", version}});
            } else {
                std::cout << "pong from " << options.socket_path << " (NEXUS " << version
                          << ")\n";
            }
            return 0;
        }
        if (action == "status") {
            json result;
            nexus::bridge::BridgeError bridge_error;
            if (!client.status(result, bridge_error)) return fail(bridge_error.message);
            if (args.has("--json")) print_json(result);
            else std::cout << result.dump(2) << "\n";
            return 0;
        }
        if (action == "call") {
            const std::string method = require_positional(args, 0, "bridge method");
            json params = json::object();
            const std::string raw = args.get("--params");
            if (!raw.empty()) {
                try {
                    params = json::parse(raw);
                } catch (const std::exception& ex) {
                    return fail(std::string("invalid --params JSON: ") + ex.what());
                }
            }
            json result;
            nexus::bridge::BridgeError bridge_error;
            if (!client.call(method, params, result, bridge_error)) {
                return fail(bridge_error.message);
            }
            std::cout << result.dump(2) << "\n";
            return 0;
        }
        return fail("unknown bridge action: " + action);
    } catch (const std::exception& ex) {
        return fail(ex.what());
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cout << kUsage;
        return 1;
    }
    // Global logging switches are parsed before the command.
    for (int i = 1; i < argc; ++i) {
        const std::string token = argv[i];
        if (token == "--help" || token == "-h") {
            std::cout << kUsage;
            return 0;
        }
    }
    std::string command = argv[1];
    if (command == "--help" || command == "-h" || command == "help") {
        std::cout << kUsage;
        return 0;
    }
    if (command == "--version") command = "version";
    try {
        if (command == "version") return command_version(parse_args(argc, argv, 2, {}, {"--json"}));
        if (command == "doctor") return command_doctor(parse_args(argc, argv, 2, {"--apk"}, {"--json"}));
        if (command == "apk") return command_apk(argc, argv);
        if (command == "install") return command_install(argc, argv);
        if (command == "strategy") return command_strategy(argc, argv);
        if (command == "launch") return command_launch(argc, argv);
        if (command == "profile") return command_profile(argc, argv);
        if (command == "shader") return command_shader(argc, argv);
        if (command == "texture") return command_texture(argc, argv);
        if (command == "mcpe") return command_mcpe(argc, argv);
        if (command == "worldgen") return command_worldgen(argc, argv);
        if (command == "mod") return command_mod(argc, argv);
        if (command == "bridge") return command_bridge(argc, argv);
    } catch (const std::exception& ex) {
        return fail(ex.what());
    }
    std::cerr << "nexus: unknown command '" << command << "'\n\n" << kUsage;
    return 1;
}
