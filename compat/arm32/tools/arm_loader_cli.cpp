// NEXUS-ARM-COMPAT - command line front-end for the ARM32 loader.
//
// This tool runs on the x86_64 host and launches ARM32 (armeabi-v7a) binaries
// through QEMU user-mode, optionally inside an Android bionic sysroot. It is
// deliberately thin: all execution logic lives in nexus::compat::arm32::ArmLoader.

#include <nexus/compat/arm32/arm_loader.hpp>
#include <nexus/core/log.hpp>

#include <cstddef>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

namespace {

void print_usage(const char* argv0) {
    std::cout
        << "NEXUS ARM32 loader\n\n"
        << "Usage:\n"
        << "  " << argv0 << " [options] <arm32-binary> [program-args...]\n\n"
        << "Options:\n"
        << "  -h, --help            Show this help message and exit\n"
        << "  -v, --version         Print the QEMU version and exit\n"
        << "  --check               Verify that QEMU user-mode is available\n"
        << "  --strace              Run the binary under strace\n"
        << "  --sysroot <path>      Set the sysroot prefix used for library lookup\n"
        << "  --env <K=V>           Add an environment variable (repeatable)\n"
        << "  --analyze             Print ELF information instead of executing\n\n"
        << "Program arguments after the binary path are passed through unchanged.\n";
}

} // namespace

int main(int argc, char** argv) {
    using namespace nexus::compat::arm32;

    std::vector<std::string> raw(argv + 1, argv + argc);

    QemuConfig config;
    std::vector<std::string> program;
    bool analyze_only = false;

    for (std::size_t i = 0; i < raw.size(); ++i) {
        const std::string& arg = raw[i];

        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        }
        if (arg == "-v" || arg == "--version") {
            std::cout << ArmLoader::get_qemu_version();
            return 0;
        }
        if (arg == "--check") {
            if (ArmLoader::check_qemu_available()) {
                std::cout << "QEMU user-mode is available\n";
                return 0;
            }
            std::cerr << "QEMU ARM user-mode emulator (qemu-arm-static or qemu-arm) was not found in PATH\n";
            return 3;
        }
        if (arg == "--strace") {
            config.strace = true;
            continue;
        }
        if (arg == "--analyze") {
            analyze_only = true;
            continue;
        }
        if (arg == "--sysroot" && i + 1 < raw.size()) {
            config.sysroot_prefix = raw[++i];
            continue;
        }
        if (arg == "--env" && i + 1 < raw.size()) {
            config.env_vars.push_back(raw[++i]);
            continue;
        }

        // First non-option token is the binary; everything after it is a
        // program argument, even if it starts with '-'.
        program.assign(raw.begin() + static_cast<std::ptrdiff_t>(i), raw.end());
        break;
    }

    if (program.empty()) {
        print_usage(argv[0]);
        return 2;
    }

    const std::string binary = program.front();
    std::vector<std::string> program_args(program.begin() + 1, program.end());

    ArmLoader loader(config);

    if (analyze_only) {
        auto info = loader.analyze_binary(binary);
        if (!info.has_value()) {
            std::cerr << "Failed to analyze " << binary << ": " << info.error() << "\n";
            return 1;
        }
        const ArmBinaryInfo& bin = info.value();
        std::cout << "Path:        " << bin.path << "\n"
                  << "Arch:        "
                  << (bin.arch == ArmArch::ARMv7_NEON ? "ARMv7-A + NEON" : "ARMv7-A")
                  << "\n"
                  << "Dynamic:     " << (bin.is_dynamic ? "yes" : "no") << "\n"
                  << "Entry point: 0x" << std::hex << bin.entry_point << std::dec << "\n";
        if (!bin.needed_libs.empty()) {
            std::cout << "Needed libs:";
            for (const auto& lib : bin.needed_libs) {
                std::cout << ' ' << lib;
            }
            std::cout << "\n";
        }
        return 0;
    }

    if (!ArmLoader::check_qemu_available()) {
        std::cerr << "QEMU ARM user-mode emulator (qemu-arm-static or qemu-arm) was not found in PATH\n";
        return 3;
    }

    ExecutionResult result = loader.execute(binary, program_args);

    if (!result.stdout_output.empty()) {
        std::cout << result.stdout_output;
        if (result.stdout_output.back() != '\n') std::cout << '\n';
    }
    if (!result.stderr_output.empty()) {
        std::cerr << result.stderr_output;
        if (result.stderr_output.back() != '\n') std::cerr << '\n';
    }

    if (result.timed_out) {
        std::cerr << "Execution timed out\n";
        return 124;
    }
    if (result.crashed) {
        std::cerr << "Execution crashed with signal " << result.signal << "\n";
        return 128 + result.signal;
    }

    return result.exit_code;
}
