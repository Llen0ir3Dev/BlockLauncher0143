// NEXUS-MCPE launcher
//
// The launcher is an EXTERNAL process.  It never patches or embeds the
// original Minecraft PE client.  It discovers local installations, prepares
// profiles, selects an execution strategy from verified facts and, when the
// user explicitly asks for it, starts the Android runtime / game plus the
// NEXUS bridge host.
//
// The interactive UI is intentionally dependency-free (ANSI + stdin).  A
// windowed launcher is a future layer; the platform Window/renderer are used
// by nexus-runtime to validate GL contexts.

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include <nexus/bridge/agent.hpp>
#include <nexus/bridge/client.hpp>
#include <nexus/core/fs.hpp>
#include <nexus/core/log.hpp>
#include <nexus/core/strings.hpp>
#include <nexus/core/time.hpp>
#include <nexus/core/version.hpp>
#include <nexus/platform/apk.hpp>
#include <nexus/platform/provisioning.hpp>
#include <nexus/platform/strategy.hpp>
#include <nexus/platform/system.hpp>

namespace {

using nlohmann::json;

const char* kUsage = R"(NEXUS-MCPE launcher

An external launcher for Minecraft PE 0.14.3.  The original game remains the
client; NEXUS manages installations, profiles, assets and the bridge runtime.

Usage:
  nexus-launcher [options]                 Start the interactive launcher
  nexus-launcher --list [--json]           List local installations
  nexus-launcher --report [--json]         Print a host/installation report
  nexus-launcher --strategy [--install ID] Show the execution strategy
  nexus-launcher --launch [--install ID] [--execute] [--yes]
  nexus-launcher --profiles [--json]       List profiles

Options:
  --root DIR        Installs directory (default: $NEXUS_HOME/installs)
  --socket PATH     Bridge socket (default: computed runtime socket)
  --pc-port EXE     Optional Windows PC-port executable for the Wine strategy
  --profile NAME    Profile to use for launch
  --json            Machine-readable output for non-interactive modes
  --execute         Actually spawn the external runtime/game
  --yes             Do not ask for confirmation (requires --execute)
  --help
)";

struct Options {
    bool list = false;
    bool report = false;
    bool strategy = false;
    bool launch = false;
    bool profiles = false;
    bool json = false;
    bool execute = false;
    bool yes = false;
    bool help = false;
    std::string root = nexus::platform::default_installs_root();
    std::string socket = nexus::bridge::default_socket_path();
    std::string pc_port;
    std::string install;
    std::string profile;
};

bool host_has_tty() {
    return ::isatty(STDIN_FILENO) == 1 && ::isatty(STDOUT_FILENO) == 1;
}

std::string profiles_dir() {
    return nexus::fs::join(nexus::fs::nexus_home(), "profiles");
}

json read_json_file(const std::string& path) {
    std::string text;
    if (!nexus::fs::read_file(path, text, 4ull * 1024 * 1024)) return json::object();
    try {
        return json::parse(text);
    } catch (const std::exception&) {
        return json::object();
    }
}

std::vector<json> load_profiles() {
    std::vector<json> profiles;
    const std::string directory = profiles_dir();
    if (!nexus::fs::is_directory(directory)) return profiles;
    for (const auto& path : nexus::fs::list_files(directory, ".json")) {
        json value = read_json_file(path);
        if (value.is_object()) {
            value["_path"] = path;
            profiles.push_back(std::move(value));
        }
    }
    std::sort(profiles.begin(), profiles.end(), [](const json& a, const json& b) {
        return a.value("name", "") < b.value("name", "");
    });
    return profiles;
}

std::vector<nexus::platform::GameInstallation> discovered_installations(
    const std::string& root) {
    std::vector<nexus::platform::GameInstallation> installations;
    nexus::platform::discover_installations(root, installations);
    std::sort(installations.begin(), installations.end(),
              [](const auto& a, const auto& b) { return a.id < b.id; });
    return installations;
}

bool find_installation(const std::string& selector, const std::string& root,
                       nexus::platform::GameInstallation& out, std::string& error) {
    const auto installations = discovered_installations(root);
    if (installations.empty()) {
        error = "no installations found in " + root +
                " (import an APK with: nexus install import PATH)";
        return false;
    }
    const std::string wanted = selector.empty() ? std::string() : selector;
    for (const auto& installation : installations) {
        if (!wanted.empty() && installation.id == wanted) {
            out = installation;
            return true;
        }
    }
    if (!wanted.empty()) {
        for (const auto& installation : installations) {
            if (nexus::starts_with(installation.id, wanted)) {
                out = installation;
                return true;
            }
        }
        error = "installation not found: " + wanted;
        return false;
    }
    // No selector: prefer an x86-ready install, otherwise the first one.
    for (const auto& installation : installations) {
        if (installation.x86_ready) {
            out = installation;
            return true;
        }
    }
    out = installations.front();
    return true;
}

int run_process(const std::vector<std::string>& command, bool verbose) {
    if (command.empty()) return 127;
    if (verbose) {
        std::cout << "  $";
        for (const auto& part : command) std::cout << " " << part;
        std::cout << "\n";
    }
    const pid_t pid = ::fork();
    if (pid < 0) {
        std::cerr << "nexus-launcher: fork failed: " << std::strerror(errno) << "\n";
        return 127;
    }
    if (pid == 0) {
        std::vector<char*> argv;
        argv.reserve(command.size() + 1);
        for (const auto& item : command) argv.push_back(const_cast<char*>(item.c_str()));
        argv.push_back(nullptr);
        ::execvp(argv[0], argv.data());
        std::fprintf(stderr, "nexus-launcher: cannot execute %s: %s\n", command[0].c_str(),
                     std::strerror(errno));
        _exit(127);
    }
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return 127;
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
}

pid_t spawn_background(const std::vector<std::string>& command, bool verbose) {
    if (command.empty()) return -1;
    if (verbose) {
        std::cout << "  &";
        for (const auto& part : command) std::cout << " " << part;
        std::cout << " (background)\n";
    }
    const pid_t pid = ::fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        ::setsid();
        std::vector<char*> argv;
        argv.reserve(command.size() + 1);
        for (const auto& item : command) argv.push_back(const_cast<char*>(item.c_str()));
        argv.push_back(nullptr);
        ::execvp(argv[0], argv.data());
        _exit(127);
    }
    return pid;
}

std::string sibling_executable(const std::string& name) {
    const std::string self = nexus::fs::executable_path();
    if (!self.empty()) {
        const std::string candidate = nexus::fs::join(nexus::dirname(self), name);
        if (nexus::fs::is_regular_file(candidate)) return candidate;
    }
    std::string found;
    if (nexus::platform::find_executable(name, found)) return found;
    return name;
}

struct LaunchPlan {
    nexus::platform::GameInstallation installation;
    nexus::platform::ApkInfo apk;
    nexus::platform::ExecutionPlan plan;
    std::vector<std::vector<std::string>> foreground;
    std::vector<std::vector<std::string>> background;
    std::vector<std::string> notes;
};

bool build_launch_plan(const Options& options, LaunchPlan& out, std::string& error) {
    if (!find_installation(options.install, options.root, out.installation, error)) {
        return false;
    }
    if (!nexus::platform::verify_installation(out.installation, error)) {
        error = "installation failed verification: " + error;
        return false;
    }
    out.apk = nexus::platform::probe_apk(out.installation.apk_path);
    const auto capabilities = nexus::platform::detect_runtime_capabilities();
    out.plan = nexus::platform::select_execution_plan(
        out.apk, nexus::platform::system_info(), capabilities, options.pc_port);

    const std::string runtime = sibling_executable("nexus-runtime");
    const std::string socket = options.socket.empty()
                                   ? nexus::bridge::default_socket_path()
                                   : options.socket;

    switch (out.plan.strategy) {
        case nexus::platform::ExecutionStrategy::AndroidContainerX86:
            // Waydroid path.  No command is executed unless the user asks.
            out.foreground.push_back({sibling_executable("waydroid"), "app", "install",
                                      out.installation.apk_path});
            out.foreground.push_back(
                {sibling_executable("waydroid"), "app", "launch", "com.mojang.minecraftpe"});
            out.background.push_back({runtime, "--socket", socket, "--assets",
                                      nexus::fs::join(out.installation.root, "assets")});
            out.notes.push_back(
                "The bridge runtime starts after the game.  In-game shader and mod "
                "application additionally needs an Android-side helper; see "
                "documentation/INJECTION-LEVELS.md.");
            break;
        case nexus::platform::ExecutionStrategy::WindowsPcPortWine:
            if (!options.pc_port.empty()) {
                out.foreground.push_back(
                    {sibling_executable("wine"), options.pc_port});
            }
            out.background.push_back({runtime, "--socket", socket});
            out.notes.push_back(
                "The Windows PC port is a separate binary.  NEXUS only launches it; "
                "it is not part of this repository.");
            break;
        case nexus::platform::ExecutionStrategy::NativePort:
            if (!options.pc_port.empty()) {
                out.foreground.push_back({options.pc_port});
            }
            out.background.push_back({runtime, "--socket", socket});
            break;
        case nexus::platform::ExecutionStrategy::ArmSystemEmulation:
        case nexus::platform::ExecutionStrategy::ArmUserModeShim:
        case nexus::platform::ExecutionStrategy::AndroidContainerArm:
            out.notes.push_back(
                "This strategy is documented but not automated by the launcher: an "
                "ABI-correct Android userspace or full-system emulation must be "
                "provisioned first.");
            break;
        case nexus::platform::ExecutionStrategy::None:
            break;
    }
    if (!out.plan.viable) {
        error = out.plan.blockers.empty() ? "no viable execution strategy" : out.plan.blockers.front();
        return false;
    }
    return true;
}

void print_plan(const LaunchPlan& plan) {
    std::cout << "\n" << plan.plan.to_markdown();
    if (!plan.foreground.empty() || !plan.background.empty()) {
        std::cout << "\nPlanned commands:\n";
        for (const auto& command : plan.foreground) {
            std::cout << "  ";
            for (const auto& part : command) std::cout << part << " ";
            std::cout << "\n";
        }
        for (const auto& command : plan.background) {
            std::cout << "  ";
            for (const auto& part : command) std::cout << part << " ";
            std::cout << "& (background)\n";
        }
    }
    for (const auto& note : plan.notes) {
        std::cout << "\nNote: " << note << "\n";
    }
}

bool confirm(const std::string& prompt, bool assume_yes) {
    if (assume_yes) return true;
    if (!host_has_tty()) {
        std::cerr << "nexus-launcher: refusing to spawn processes without a TTY; "
                     "pass --yes together with --execute.\n";
        return false;
    }
    std::cout << prompt << " [y/N] ";
    std::cout.flush();
    std::string line;
    if (!std::getline(std::cin, line)) return false;
    const std::string answer = nexus::to_lower(nexus::trim(line));
    return answer == "y" || answer == "yes";
}

int execute_plan(const LaunchPlan& plan, bool verbose) {
    for (const auto& command : plan.foreground) {
        if (command.empty() || command[0].empty()) continue;
        const int status = run_process(command, verbose);
        if (status != 0) {
            std::cerr << "nexus-launcher: command failed with status " << status << "\n";
            return status;
        }
    }
    for (const auto& command : plan.background) {
        if (command.empty() || command[0].empty()) continue;
        if (spawn_background(command, verbose) < 0) {
            std::cerr << "nexus-launcher: failed to start background command\n";
            return 1;
        }
    }
    return 0;
}

json host_report(const Options& options) {
    const auto system = nexus::platform::system_info();
    const auto capabilities = nexus::platform::detect_runtime_capabilities();
    json installs = json::array();
    for (const auto& installation : discovered_installations(options.root)) {
        installs.push_back(installation.to_json());
    }
    return {
        {"name", "NEXUS-MCPE"},
        {"version", nexus::kVersion},
        {"gameVersion", nexus::kGameVersion},
        {"protocol", nexus::bridge::kProtocolName},
        {"host", system.to_json()},
        {"capabilities", capabilities.to_json()},
        {"installsRoot", options.root},
        {"installCount", installs.size()},
        {"installs", installs},
        {"profiles", load_profiles()},
        {"socket", options.socket}
    };
}

int print_list(const Options& options) {
    const auto installations = discovered_installations(options.root);
    if (options.json) {
        json out = {{"root", options.root}, {"installs", json::array()}};
        for (const auto& installation : installations) {
            out["installs"].push_back(installation.to_json());
        }
        std::cout << out.dump(2) << "\n";
        return 0;
    }
    std::cout << "Installations in " << options.root << ":\n";
    if (installations.empty()) {
        std::cout << "  (none) - import an APK with: nexus install import PATH\n";
        return 0;
    }
    for (const auto& installation : installations) {
        std::cout << "  " << installation.id << "  " << installation.name << "  "
                  << installation.version_name << " (" << installation.version_code << ")  ["
                  << nexus::join(installation.abis, ",") << "]"
                  << (installation.x86_ready ? "  x86-ready" : "  not-x86") << "\n";
    }
    return 0;
}

int print_strategy(const Options& options) {
    nexus::platform::GameInstallation installation;
    std::string error;
    if (!find_installation(options.install, options.root, installation, error)) {
        std::cerr << "nexus-launcher: " << error << "\n";
        return 1;
    }
    const auto apk = nexus::platform::probe_apk(installation.apk_path);
    const auto plan = nexus::platform::select_execution_plan(
        apk, nexus::platform::system_info(),
        nexus::platform::detect_runtime_capabilities(), options.pc_port);
    if (options.json) {
        std::cout << json({{"installation", installation.to_json()},
                           {"apk", apk.to_json()},
                           {"plan", plan.to_json()}})
                         .dump(2)
                  << "\n";
    } else {
        std::cout << "Installation: " << installation.id << " (" << installation.name << ")\n";
        std::cout << plan.to_markdown();
    }
    return plan.viable ? 0 : 3;
}

int print_profiles(const Options& options) {
    const auto profiles = load_profiles();
    if (options.json) {
        std::cout << json({{"directory", profiles_dir()}, {"profiles", profiles}}).dump(2)
                  << "\n";
        return 0;
    }
    std::cout << "Profiles in " << profiles_dir() << ":\n";
    if (profiles.empty()) {
        std::cout << "  (none) - create one with: nexus profile create NAME --install ID\n";
        return 0;
    }
    for (const auto& profile : profiles) {
        std::cout << "  " << profile.value("name", "") << "  install="
                  << profile.value("installId", "") << "  "
                  << profile.value("width", 1280) << "x" << profile.value("height", 720)
                  << (profile.value("fullscreen", false) ? " fullscreen" : "") << "\n";
    }
    return 0;
}

std::string bridge_status_line(const std::string& socket) {
    nexus::bridge::BridgeClientOptions client_options;
    client_options.socket_path = socket;
    client_options.timeout_ms = 500;
    nexus::bridge::BridgeClient client(client_options);
    std::string error;
    if (!client.connect(error)) return std::string("offline (") + error + ")";
    std::string version;
    if (client.ping(&version, error)) return "online, NEXUS " + version;
    return std::string("unresponsive (") + error + ")";
}

void clear_screen() {
    if (host_has_tty()) std::cout << "\x1b[2J\x1b[H";
}

void interactive(const Options& options) {
    std::string selected;
    std::string error;
    while (true) {
        const auto installations = discovered_installations(options.root);
        clear_screen();
        std::cout << "NEXUS-MCPE " << nexus::kVersion << "  launcher  (game "
                  << nexus::kGameVersion << ")\n";
        std::cout << "The original Minecraft PE client is never modified.\n\n";
        std::cout << "Installs (" << options.root << ")\n";
        if (installations.empty()) {
            std::cout << "  (none) - use 'nexus install import PATH' first\n";
        } else {
            for (size_t i = 0; i < installations.size(); ++i) {
                const auto& installation = installations[i];
                std::cout << "  " << (i + 1) << ") " << installation.id << "  "
                          << installation.name << "  [" << nexus::join(installation.abis, ",")
                          << "]" << (installation.x86_ready ? "  x86-ready" : "  not-x86")
                          << "\n";
            }
        }
        std::cout << "\nSelected: "
                  << (selected.empty() ? std::string("(auto)") : selected) << "\n";
        std::cout << "Bridge:   " << bridge_status_line(options.socket) << " @ "
                  << options.socket << "\n\n";
        std::cout << "  [1-" << installations.size() << "] select install\n"
                  << "  s  show execution strategy\n"
                  << "  p  profiles\n"
                  << "  l  launch (asks before spawning anything)\n"
                  << "  r  refresh\n"
                  << "  q  quit\n"
                  << "> ";
        std::cout.flush();
        std::string line;
        if (!std::getline(std::cin, line)) break;
        const std::string command = nexus::to_lower(nexus::trim(line));
        if (command == "q" || command == "quit" || command == "exit") break;
        if (command == "r" || command.empty()) continue;
        if (command == "p" || command == "profiles") {
            Options list_options = options;
            list_options.json = false;
            print_profiles(list_options);
            std::cout << "\nPress Enter to continue...";
            std::cout.flush();
            std::getline(std::cin, line);
            continue;
        }
        if (command == "s" || command == "strategy" || command == "l" ||
            command == "launch") {
            Options action_options = options;
            action_options.install = selected;
            nexus::platform::GameInstallation installation;
            std::string find_error;
            if (!find_installation(selected, options.root, installation, find_error)) {
                std::cout << find_error << "\nPress Enter to continue...";
                std::cout.flush();
                std::getline(std::cin, line);
                continue;
            }
            LaunchPlan plan;
            std::string plan_error;
            if (!build_launch_plan(action_options, plan, plan_error)) {
                std::cout << "\n" << plan.plan.to_markdown();
                std::cout << "\nLaunch refused: " << plan_error << "\n";
                std::cout << "Press Enter to continue...";
                std::cout.flush();
                std::getline(std::cin, line);
                continue;
            }
            print_plan(plan);
            if (command == "l" || command == "launch") {
                if (!confirm("Start this launch plan now?", false)) {
                    std::cout << "Cancelled. Nothing was executed.\n";
                } else {
                    const int status = execute_plan(plan, true);
                    std::cout << (status == 0 ? "Launch commands completed.\n"
                                              : "Launch failed.\n");
                }
                std::cout << "Press Enter to continue...";
                std::cout.flush();
                std::getline(std::cin, line);
            }
            continue;
        }
        bool numeric = !command.empty();
        for (char c : command) {
            if (!std::isdigit(static_cast<unsigned char>(c))) numeric = false;
        }
        if (numeric) {
            const size_t index = static_cast<size_t>(std::atoi(command.c_str()));
            if (index >= 1 && index <= installations.size()) {
                selected = installations[index - 1].id;
            }
            continue;
        }
        std::cout << "Unknown command. Press Enter to continue...";
        std::cout.flush();
        std::getline(std::cin, line);
        (void)error;
    }
    std::cout << "Goodbye.\n";
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--version" || arg == "-V") {
            std::cout << "NEXUS-MCPE " << NEXUS_VERSION << "\n";
            return 0;
        } else if (arg == "--help" || arg == "-h") {
            options.help = true;
        } else if (arg == "--list") {
            options.list = true;
        } else if (arg == "--report") {
            options.report = true;
        } else if (arg == "--strategy") {
            options.strategy = true;
        } else if (arg == "--launch") {
            options.launch = true;
        } else if (arg == "--profiles") {
            options.profiles = true;
        } else if (arg == "--json") {
            options.json = true;
        } else if (arg == "--execute") {
            options.execute = true;
        } else if (arg == "--yes" || arg == "-y") {
            options.yes = true;
        } else if (arg == "--root" && i + 1 < argc) {
            options.root = argv[++i];
        } else if (arg == "--socket" && i + 1 < argc) {
            options.socket = argv[++i];
        } else if (arg == "--pc-port" && i + 1 < argc) {
            options.pc_port = argv[++i];
        } else if (arg == "--install" && i + 1 < argc) {
            options.install = argv[++i];
        } else if (arg == "--profile" && i + 1 < argc) {
            options.profile = argv[++i];
        } else {
            std::cerr << "nexus-launcher: unknown argument " << arg << "\n\n"
                      << kUsage;
            return 2;
        }
    }
    if (options.help) {
        std::cout << kUsage;
        return 0;
    }
    nexus::Logger::instance().set_log_file(
        nexus::fs::join(nexus::fs::logs_dir(),
                        "launcher-" + nexus::fs::timestamp_string() + ".log"));
    NEXUS_LOG_INFO("launcher starting");

    if (!options.profile.empty()) {
        bool found = false;
        for (const auto& profile : load_profiles()) {
            if (profile.value("name", "") == options.profile) {
                found = true;
                if (options.install.empty()) options.install = profile.value("installId", "");
            }
        }
        if (!found) {
            std::cerr << "nexus-launcher: profile not found: " << options.profile << "\n";
            return 1;
        }
    }

    if (options.list) return print_list(options);
    if (options.report) {
        const json report = host_report(options);
        if (options.json) std::cout << report.dump(2) << "\n";
        else {
            std::cout << "NEXUS-MCPE " << nexus::kVersion << " host report\n"
                      << "  installs root: " << options.root << "\n"
                      << "  installs:      " << report["installCount"] << "\n"
                      << "  bridge socket: " << options.socket << "\n"
                      << "  bridge:        " << bridge_status_line(options.socket) << "\n";
        }
        return 0;
    }
    if (options.strategy) return print_strategy(options);
    if (options.profiles) return print_profiles(options);
    if (options.launch) {
        LaunchPlan plan;
        std::string error;
        if (!build_launch_plan(options, plan, error)) {
            if (options.json) {
                std::cout << json({{"viable", false},
                                   {"error", error},
                                   {"plan", plan.plan.to_json()}})
                                 .dump(2)
                          << "\n";
            } else {
                std::cerr << plan.plan.to_markdown();
                std::cerr << "\nnexus-launcher: " << error << "\n";
            }
            return 3;
        }
        if (options.json) {
            json out = {{"viable", true},
                        {"installation", plan.installation.to_json()},
                        {"plan", plan.plan.to_json()},
                        {"executed", false},
                        {"foreground", json::array()},
                        {"background", json::array()},
                        {"notes", plan.notes}};
            for (const auto& command : plan.foreground) out["foreground"].push_back(command);
            for (const auto& command : plan.background) out["background"].push_back(command);
            std::cout << out.dump(2) << "\n";
            return 0;
        }
        print_plan(plan);
        if (!options.execute) {
            std::cout << "\nNothing was executed. Re-run with --execute to start the "
                         "external runtime/game.\n";
            return 0;
        }
        if (!confirm("Execute the plan above?", options.yes)) {
            std::cout << "Cancelled. Nothing was executed.\n";
            return 0;
        }
        const int status = execute_plan(plan, true);
        std::cout << (status == 0 ? "Launch commands completed.\n" : "Launch failed.\n");
        return status;
    }

    if (!host_has_tty()) {
        std::cerr << "nexus-launcher: interactive mode needs a TTY. Use --list, --report, "
                     "--strategy or --launch.\n";
        return 2;
    }
    interactive(options);
    return 0;
}
