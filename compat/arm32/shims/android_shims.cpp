#include <nexus/compat/arm32/android_shims.hpp>
#include <nexus/compat/arm32/arm_loader.hpp>
#include <nexus/core/log.hpp>
// NOTE: <android/log.h> is provided by the Android NDK and is not available
// on the Linux host. The required declarations (android_LogPriority,
// __android_log_vprint, ...) live in android_shims.hpp instead.
#include <cstdarg>
#include <pthread.h>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <string>
#include <vector>

namespace nexus::compat::arm32 {

/// Android shim implementation using host Linux APIs
class AndroidShimImpl : public AndroidShim {
public:
    bool initialize() override {
        NEXUS_LOG_INFO("AndroidShim: Initializing Android API shims");
        jni_env_ = get_jni_env_from_vm();
        return jni_env_ != nullptr;
    }

    void shutdown() override {
        NEXUS_LOG_INFO("AndroidShim: Shutting down");
    }

    void* get_jni_env() override { return jni_env_; }
    void* get_java_vm() override { return java_vm_; }

    void* asset_manager_open(const char* filename, int mode) override {
        if (!asset_mgr_) return nullptr;
        // In real implementation, would use AAssetManager_open
        // For now, return a dummy handle
        return reinterpret_cast<void*>(0x1);
    }

    void asset_close(void* asset) override {
        // No-op for dummy implementation
    }

    off_t asset_get_length(void* asset) override {
        return 0;
    }

    const void* asset_get_buffer(void* asset) override {
        return nullptr;
    }

    int log_write(int priority, const char* tag, const char* fmt, ...) override {
        va_list args;
        va_start(args, fmt);
        int result = __android_log_vprint(priority, tag, fmt, args);
        va_end(args);
        return result;
    }

    const char* get_external_storage_path() override {
        static std::string path = "/sdcard";
        return path.c_str();
    }

    const char* get_internal_storage_path() override {
        static std::string path = "/data/data/com.mojang.minecraftpe";
        return path.c_str();
    }

    void* native_window_from_surface(void* surface) override {
        return surface; // Pass through
    }

    void native_window_release(void* window) override {
        // No-op
    }

    void set_java_vm(JavaVM* vm) { java_vm_ = vm; }
    void set_asset_manager(void* mgr) { asset_mgr_ = mgr; }

private:
    JNIEnv* get_jni_env_from_vm() {
        if (!java_vm_) return nullptr;
        JNIEnv* env;
        if (java_vm_->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
            if (java_vm_->AttachCurrentThread(reinterpret_cast<void**>(&env), nullptr) != JNI_OK) {
                return nullptr;
            }
        }
        return env;
    }

    JavaVM* java_vm_ = nullptr;
    JNIEnv* jni_env_ = nullptr;
    void* asset_mgr_ = nullptr;
};

std::unique_ptr<AndroidShim> create_android_shim() {
    return std::make_unique<AndroidShimImpl>();
}

/// JNI Bridge implementation
class JniBridgeImpl : public JniBridge {
public:
    explicit JniBridgeImpl(ArmLoader& loader) : loader_(loader) {}

    bool register_native_methods(const char* class_name,
                                const void* methods,
                                int num_methods) override {
        NEXUS_LOG_INFO(std::string("JNI: Registering ") + std::to_string(num_methods) +
                       " native methods for class " + class_name);
        // In real implementation, would use JNIEnv::RegisterNatives
        return true;
    }

    bool call_static_void_method(const char* class_name,
                                const char* method_name,
                                const char* signature,
                                ...) override {
        NEXUS_LOG_DEBUG(std::string("JNI: Call static void ") + class_name + "::" + method_name);
        return true;
    }

    bool call_static_int_method(const char* class_name,
                               const char* method_name,
                               const char* signature,
                               int* out_result,
                               ...) override {
        NEXUS_LOG_DEBUG(std::string("JNI: Call static int ") + class_name + "::" + method_name);
        if (out_result) *out_result = 0;
        return true;
    }

    bool call_arm_function(uintptr_t address,
                          const void* args,
                          size_t args_size,
                          void* result) override {
        char addr_buf[32];
        std::snprintf(addr_buf, sizeof(addr_buf), "0x%llx",
                      static_cast<unsigned long long>(address));
        NEXUS_LOG_DEBUG(std::string("JNI: Call ARM function at ") + addr_buf);
        return false; // Not implemented
    }

    void* alloc_arm_memory(size_t size) override {
        // Allocate memory that ARM32 code can access
        return malloc(size);
    }

    void free_arm_memory(void* ptr) override {
        free(ptr);
    }

    bool read_arm_memory(uintptr_t addr, void* buf, size_t size) override {
        if (size == 0) return true;
        memcpy(buf, reinterpret_cast<void*>(addr), size);
        return true;
    }

    bool write_arm_memory(uintptr_t addr, const void* buf, size_t size) override {
        if (size == 0) return true;
        memcpy(reinterpret_cast<void*>(addr), buf, size);
        return true;
    }

private:
    ArmLoader& loader_;
};

std::unique_ptr<JniBridge> create_jni_bridge(ArmLoader& loader) {
    return std::make_unique<JniBridgeImpl>(loader);
}

} // namespace nexus::compat::arm32