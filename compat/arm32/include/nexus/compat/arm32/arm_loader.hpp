#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace nexus::compat::arm32 {

/// ARM32 binary architecture types
enum class ArmArch : uint8_t {
    ARMv7 = 7,      // ARMv7-A (armeabi-v7a)
    ARMv7_NEON = 8  // ARMv7-A with NEON
};

/// ARM32 ELF binary information
struct ArmBinaryInfo {
    std::string path;
    ArmArch arch = ArmArch::ARMv7;
    bool is_dynamic = true;
    std::vector<std::string> needed_libs;
    uint32_t entry_point = 0;
    std::vector<uint8_t> build_id;
};

/// QEMU execution configuration
struct QemuConfig {
    // Empty means "auto-detect": prefers qemu-arm-static, falls back to qemu-arm.
    std::string qemu_path;
    std::string sysroot_prefix;           // e.g., "/opt/android-arm"
    std::vector<std::string> env_vars;    // LD_LIBRARY_PATH, etc.
    bool strace = false;                  // Enable syscall tracing
    bool gdb_server = false;              // Enable GDB server
    uint16_t gdb_port = 1234;
    std::vector<std::string> extra_args;
};

/// ARM32 execution result
struct ExecutionResult {
    int exit_code = -1;
    std::string stdout_output;
    std::string stderr_output;
    bool timed_out = false;
    bool crashed = false;
    uint32_t crash_address = 0;
    int signal = 0;
};

/// Result type for fallible operations (C++17 compatible)
template<typename T, typename E>
class Result {
public:
    Result(T&& value) : has_value_(true), value_(std::move(value)) {}
    Result(E&& error) : has_value_(false), error_(std::move(error)) {}

    bool has_value() const { return has_value_; }
    T& value() { return value_; }
    const T& value() const { return value_; }
    E& error() { return error_; }
    const E& error() const { return error_; }

private:
    bool has_value_;
    T value_;
    E error_;
};

/// ARM32 binary loader and executor
class ArmLoader {
public:
    explicit ArmLoader(const QemuConfig& config = {});
    ~ArmLoader();

    // Non-copyable, movable
    ArmLoader(const ArmLoader&) = delete;
    ArmLoader& operator=(const ArmLoader&) = delete;
    ArmLoader(ArmLoader&&) noexcept;
    ArmLoader& operator=(ArmLoader&&) noexcept;

    /// Analyze an ARM32 ELF binary
    Result<ArmBinaryInfo, std::string> analyze_binary(const std::string& path);

    /// Execute an ARM32 binary with arguments
    ExecutionResult execute(const std::string& binary_path,
                           const std::vector<std::string>& args = {},
                           const std::vector<std::string>& env = {});

    /// Execute with custom QEMU config override
    ExecutionResult execute_with_config(const std::string& binary_path,
                                       const std::vector<std::string>& args,
                                       const std::vector<std::string>& env,
                                       const QemuConfig& config);

    /// Check if QEMU is available and functional
    static bool check_qemu_available();

    /// Get QEMU version string
    static std::string get_qemu_version();

    /// Set up ARM32 sysroot with required libraries
    bool setup_sysroot(const std::string& sysroot_path,
                       const std::vector<std::string>& required_libs);

private:
    class Impl;
    std::unique_ptr<Impl> pimpl_;
};

/// Android API shim interface
class AndroidShim {
public:
    virtual ~AndroidShim() = default;

    // Lifecycle
    virtual bool initialize() = 0;
    virtual void shutdown() = 0;

    // JNI environment
    virtual void* get_jni_env() = 0;
    virtual void* get_java_vm() = 0;

    // Asset management
    virtual void* asset_manager_open(const char* filename, int mode) = 0;
    virtual void asset_close(void* asset) = 0;
    virtual off_t asset_get_length(void* asset) = 0;
    virtual const void* asset_get_buffer(void* asset) = 0;

    // Logging
    virtual int log_write(int priority, const char* tag, const char* fmt, ...) = 0;

    // File system
    virtual const char* get_external_storage_path() = 0;
    virtual const char* get_internal_storage_path() = 0;

    // Native window
    virtual void* native_window_from_surface(void* surface) = 0;
    virtual void native_window_release(void* window) = 0;
};

/// Factory for creating Android shim instances
std::unique_ptr<AndroidShim> create_android_shim();

/// JNI bridge for ARM32 ↔ x86_64 communication
class JniBridge {
public:
    virtual ~JniBridge() = default;

    // Register native methods
    virtual bool register_native_methods(const char* class_name,
                                        const void* methods,
                                        int num_methods) = 0;

    // Call Java methods from ARM32
    virtual bool call_static_void_method(const char* class_name,
                                        const char* method_name,
                                        const char* signature,
                                        ...) = 0;
    virtual bool call_static_int_method(const char* class_name,
                                       const char* method_name,
                                       const char* signature,
                                       int* out_result,
                                       ...) = 0;

    // Call ARM32 functions from x86_64
    virtual bool call_arm_function(uintptr_t address,
                                  const void* args,
                                  size_t args_size,
                                  void* result) = 0;

    // Memory management
    virtual void* alloc_arm_memory(size_t size) = 0;
    virtual void free_arm_memory(void* ptr) = 0;
    virtual bool read_arm_memory(uintptr_t addr, void* buf, size_t size) = 0;
    virtual bool write_arm_memory(uintptr_t addr, const void* buf, size_t size) = 0;
};

/// Factory for creating JNI bridge
std::unique_ptr<JniBridge> create_jni_bridge(ArmLoader& loader);

} // namespace nexus::compat::arm32