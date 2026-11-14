#include <nexus/compat/arm32/arm_loader.hpp>
#include <nexus/core/log.hpp>
#include <nexus/core/fs.hpp>
#include <nexus/core/strings.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <elf.h>
#include <fstream>
#include <sstream>
#include <chrono>
#include <thread>
#include <array>

namespace nexus::compat::arm32 {

class ArmLoader::Impl {
public:
    QemuConfig config_;

    Impl(const QemuConfig& config) : config_(config) {}

    Result<ArmBinaryInfo, std::string> analyze_binary(const std::string& path) {
        ArmBinaryInfo info;
        info.path = path;

        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return Result<ArmBinaryInfo, std::string>("Failed to open file: " + path);
        }

        // Read ELF header
        Elf32_Ehdr ehdr;
        file.read(reinterpret_cast<char*>(&ehdr), sizeof(ehdr));
        if (!file || memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0) {
            return Result<ArmBinaryInfo, std::string>("Not a valid ELF file");
        }

        if (ehdr.e_ident[EI_CLASS] != ELFCLASS32) {
            return Result<ArmBinaryInfo, std::string>("Not a 32-bit ELF file");
        }
        if (ehdr.e_ident[EI_DATA] != ELFDATA2LSB) {
            return Result<ArmBinaryInfo, std::string>("Not little-endian ELF");
        }
        if (ehdr.e_machine != EM_ARM) {
            return Result<ArmBinaryInfo, std::string>("Not an ARM binary");
        }

        info.arch = ArmArch::ARMv7;
        info.is_dynamic = (ehdr.e_type == ET_DYN);
        info.entry_point = ehdr.e_entry;

        // Read section headers to find .dynamic
        file.seekg(ehdr.e_shoff);
        std::vector<Elf32_Shdr> shdrs(ehdr.e_shnum);
        file.read(reinterpret_cast<char*>(shdrs.data()), shdrs.size() * sizeof(Elf32_Shdr));

        // Find .dynstr and .dynamic
        std::string shstrtab;
        if (ehdr.e_shstrndx < shdrs.size()) {
            auto& shstr_shdr = shdrs[ehdr.e_shstrndx];
            shstrtab.resize(shstr_shdr.sh_size);
            file.seekg(shstr_shdr.sh_offset);
            file.read(&shstrtab[0], shstr_shdr.sh_size);
        }

        for (size_t i = 0; i < shdrs.size(); ++i) {
            const auto& shdr = shdrs[i];
            std::string name;
            if (shdr.sh_name < shstrtab.size()) {
                name = shstrtab.substr(shdr.sh_name);
            }

            if (name == ".dynamic" && shdr.sh_type == SHT_DYNAMIC) {
                // Parse dynamic section for needed libraries
                file.seekg(shdr.sh_offset);
                std::vector<Elf32_Dyn> dyns(shdr.sh_size / sizeof(Elf32_Dyn));
                file.read(reinterpret_cast<char*>(dyns.data()), dyns.size() * sizeof(Elf32_Dyn));

                // Find .dynstr for string table
                Elf32_Word dynstr_offset = 0;
                for (const auto& dyn : dyns) {
                    if (dyn.d_tag == DT_STRTAB) {
                        dynstr_offset = dyn.d_un.d_val;
                        break;
                    }
                }

                if (dynstr_offset) {
                    // Find .dynstr section
                    for (const auto& shdr2 : shdrs) {
                        if (shdr2.sh_addr == dynstr_offset && shdr2.sh_type == SHT_STRTAB) {
                            std::string dynstr;
                            dynstr.resize(shdr2.sh_size);
                            file.seekg(shdr2.sh_offset);
                            file.read(&dynstr[0], shdr2.sh_size);

                            for (const auto& dyn2 : dyns) {
                                if (dyn2.d_tag == DT_NEEDED) {
                                    if (dyn2.d_un.d_val < dynstr.size()) {
                                        info.needed_libs.push_back(
                                            dynstr.substr(dyn2.d_un.d_val));
                                    }
                                }
                            }
                            break;
                        }
                    }
                }
                break;
            }
        }

        // Read build ID from .note.gnu.build-id
        for (const auto& shdr : shdrs) {
            if (shdr.sh_type == SHT_NOTE) {
                file.seekg(shdr.sh_offset);
                std::vector<char> note_data(shdr.sh_size);
                file.read(note_data.data(), note_data.size());

                // Parse note sections for build ID
                size_t offset = 0;
                while (offset + 12 <= note_data.size()) {
                    Elf32_Nhdr* nhdr = reinterpret_cast<Elf32_Nhdr*>(note_data.data() + offset);
                    if (nhdr->n_type == NT_GNU_BUILD_ID && nhdr->n_namesz == 4 &&
                        memcmp(note_data.data() + offset + sizeof(Elf32_Nhdr), "GNU", 4) == 0) {
                        size_t desc_offset = offset + sizeof(Elf32_Nhdr) + 4;
                        // Align to 4 bytes
                        desc_offset = (desc_offset + 3) & ~3;
                        if (desc_offset + nhdr->n_descsz <= note_data.size()) {
                            info.build_id.assign(note_data.begin() + desc_offset,
                                                 note_data.begin() + desc_offset + nhdr->n_descsz);
                        }
                        break;
                    }
                    offset += sizeof(Elf32_Nhdr);
                    offset += (nhdr->n_namesz + 3) & ~3;
                    offset += (nhdr->n_descsz + 3) & ~3;
                }
            }
        }

        return Result<ArmBinaryInfo, std::string>(std::move(info));
    }

    ExecutionResult execute(const std::string& binary_path,
                           const std::vector<std::string>& args,
                           const std::vector<std::string>& env) {
        return execute_with_config(binary_path, args, env, config_);
    }

    ExecutionResult execute_with_config(const std::string& binary_path,
                                       const std::vector<std::string>& args,
                                       const std::vector<std::string>& env,
                                       const QemuConfig& config) {
        ExecutionResult result;

        // Resolve the emulator at run time: distributions ship it either as
        // qemu-arm-static (fully static, ideal for bundling in an AppImage) or
        // as qemu-arm (dynamic). Never assume one name.
        std::string qemu = config.qemu_path;
        if (qemu.empty() || !qemu_command_works(qemu)) {
            qemu = resolve_qemu_path();
        }
        if (qemu.empty()) {
            result.exit_code = -1;
            result.stderr_output =
                "QEMU ARM user-mode emulator not found (looked for qemu-arm-static and qemu-arm)";
            return result;
        }

        // Build command line
        std::vector<std::string> cmd;
        cmd.push_back(qemu);

        // Add sysroot if specified
        if (!config.sysroot_prefix.empty()) {
            cmd.push_back("-L");
            cmd.push_back(config.sysroot_prefix);
        }

        // Add extra args
        for (const auto& arg : config.extra_args) {
            cmd.push_back(arg);
        }

        // Add binary and its arguments
        cmd.push_back(binary_path);
        for (const auto& arg : args) {
            cmd.push_back(arg);
        }

        // Prepare environment
        std::vector<char*> envp;
        std::vector<std::string> env_strings;

        // Copy current environment
        for (char** env = environ; *env; ++env) {
            env_strings.push_back(*env);
        }
        // Add custom env vars
        for (const auto& e : config.env_vars) {
            env_strings.push_back(e);
        }
        for (const auto& e : env) {
            env_strings.push_back(e);
        }

        for (auto& s : env_strings) {
            envp.push_back(const_cast<char*>(s.c_str()));
        }
        envp.push_back(nullptr);

        // Convert cmd to argv
        std::vector<char*> argv;
        for (auto& s : cmd) {
            argv.push_back(const_cast<char*>(s.c_str()));
        }
        argv.push_back(nullptr);

        // Create pipes for stdout/stderr
        int stdout_pipe[2], stderr_pipe[2];
        if (pipe(stdout_pipe) < 0 || pipe(stderr_pipe) < 0) {
            result.exit_code = -1;
            result.stderr_output = "Failed to create pipes";
            return result;
        }

        pid_t pid = fork();
        if (pid < 0) {
            result.exit_code = -1;
            result.stderr_output = "Fork failed";
            return result;
        }

        if (pid == 0) {
            // Child process
            close(stdout_pipe[0]);
            close(stderr_pipe[0]);
            dup2(stdout_pipe[1], STDOUT_FILENO);
            dup2(stderr_pipe[1], STDERR_FILENO);
            close(stdout_pipe[1]);
            close(stderr_pipe[1]);

            if (config.strace) {
                std::vector<char*> strace_argv;
                strace_argv.push_back(const_cast<char*>("strace"));
                strace_argv.push_back(const_cast<char*>("-f"));
                for (auto& s : cmd) {
                    strace_argv.push_back(const_cast<char*>(s.c_str()));
                }
                strace_argv.push_back(nullptr);
                execvpe("strace", strace_argv.data(), envp.data());
            } else {
                execvpe(cmd[0].c_str(), argv.data(), envp.data());
            }
            _exit(127);
        }

        // Parent process
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);

        // Read stdout/stderr
        std::string stdout_data, stderr_data;
        char buffer[4096];
        ssize_t n;

        while ((n = read(stdout_pipe[0], buffer, sizeof(buffer))) > 0) {
            stdout_data.append(buffer, n);
        }
        while ((n = read(stderr_pipe[0], buffer, sizeof(buffer))) > 0) {
            stderr_data.append(buffer, n);
        }

        close(stdout_pipe[0]);
        close(stderr_pipe[0]);

        // Wait for process
        auto start = std::chrono::steady_clock::now();
        const auto timeout = std::chrono::seconds(60);

        while (true) {
            int wait_status;
            pid_t w = waitpid(pid, &wait_status, WNOHANG);
            if (w == pid) {
                if (WIFEXITED(wait_status)) {
                    result.exit_code = WEXITSTATUS(wait_status);
                } else if (WIFSIGNALED(wait_status)) {
                    result.exit_code = -1;
                    result.crashed = true;
                    result.signal = WTERMSIG(wait_status);
                }
                break;
            }

            if (std::chrono::steady_clock::now() - start > timeout) {
                kill(pid, SIGKILL);
                waitpid(pid, nullptr, 0);
                result.timed_out = true;
                result.exit_code = -1;
                result.stderr_output += "\nProcess timed out";
                break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        result.stdout_output = std::move(stdout_data);
        result.stderr_output = std::move(stderr_data);
        return result;
    }

    static bool qemu_command_works(const std::string& path) {
        std::string cmd = path + " --version >/dev/null 2>&1";
        return std::system(cmd.c_str()) == 0;
    }

    /// Return the first usable ARM user-mode emulator. `qemu-arm-static` is
    /// preferred because a static binary can be bundled in the AppImage.
    static std::string resolve_qemu_path() {
        for (const char* candidate : {"qemu-arm-static", "qemu-arm"}) {
            if (qemu_command_works(candidate)) {
                return candidate;
            }
        }
        return {};
    }

    static bool check_qemu_available() {
        return !resolve_qemu_path().empty();
    }

    static std::string get_qemu_version() {
        std::string qemu = resolve_qemu_path();
        if (qemu.empty()) return "unknown";
        std::array<char, 128> buffer;
        std::string result;
        std::unique_ptr<FILE, decltype(&pclose)> pipe(
            popen((qemu + " --version 2>&1").c_str(), "r"), pclose);
        if (!pipe) return "unknown";
        while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
            result += buffer.data();
        }
        return result;
    }

    bool setup_sysroot(const std::string& sysroot_path,
                       const std::vector<std::string>& required_libs) {
        // Create sysroot directory structure
        std::string lib_dir = sysroot_path + "/lib";
        std::string usr_lib_dir = sysroot_path + "/usr/lib";

        nexus::fs::ensure_directory(lib_dir);
        nexus::fs::ensure_directory(usr_lib_dir);

        // Copy required libraries from Android sysroot or APK
        for (const auto& lib : required_libs) {
            std::string src = "/system/lib/" + lib;  // Would need actual source
            std::string dst = lib_dir + "/" + lib;
            // In real implementation, would copy from Android sysroot
            NEXUS_LOG_DEBUG("Would copy " + src + " to " + dst);
        }

        config_.sysroot_prefix = sysroot_path;
        return true;
    }
};

ArmLoader::ArmLoader(const QemuConfig& config) : pimpl_(std::make_unique<Impl>(config)) {}
ArmLoader::~ArmLoader() = default;
ArmLoader::ArmLoader(ArmLoader&&) noexcept = default;
ArmLoader& ArmLoader::operator=(ArmLoader&&) noexcept = default;

Result<ArmBinaryInfo, std::string> ArmLoader::analyze_binary(const std::string& path) {
    return pimpl_->analyze_binary(path);
}

ExecutionResult ArmLoader::execute(const std::string& binary_path,
                                  const std::vector<std::string>& args,
                                  const std::vector<std::string>& env) {
    return pimpl_->execute(binary_path, args, env);
}

ExecutionResult ArmLoader::execute_with_config(const std::string& binary_path,
                                              const std::vector<std::string>& args,
                                              const std::vector<std::string>& env,
                                              const QemuConfig& config) {
    return pimpl_->execute_with_config(binary_path, args, env, config);
}

bool ArmLoader::check_qemu_available() {
    return Impl::check_qemu_available();
}

std::string ArmLoader::get_qemu_version() {
    return Impl::get_qemu_version();
}

bool ArmLoader::setup_sysroot(const std::string& sysroot_path,
                             const std::vector<std::string>& required_libs) {
    return pimpl_->setup_sysroot(sysroot_path, required_libs);
}

} // namespace nexus::compat::arm32