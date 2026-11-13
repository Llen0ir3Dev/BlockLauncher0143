#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace nexus::mod {

// A cooperative hook is called at a documented point in the NEXUS pipeline.
// Hooks do not rewrite machine code and never claim to patch the proprietary
// game; they are the supported extension surface of the port itself.
struct HookContext {
    std::string name;
    nlohmann::json payload = nlohmann::json::object();
    bool cancelled = false;
    std::string mod_id;
};

using HookCallback = std::function<void(HookContext&)>;
using HookId = uint64_t;

struct HookRegistration {
    HookId id = 0;
    std::string name;
    std::string mod_id;
    int priority = 0;
    HookCallback callback;
};

class HookManager {
public:
    HookId add(const std::string& name, int priority, const std::string& mod_id,
               HookCallback callback);
    bool remove(HookId id);
    size_t remove_mod(const std::string& mod_id);
    void clear();

    // Runs hooks in ascending priority order.  Returns false when a hook
    // cancelled the operation.
    bool trigger(HookContext& context);
    bool trigger(const std::string& name, nlohmann::json& payload,
                 std::string* cancelled_by = nullptr);

    std::vector<std::string> names() const;
    size_t size() const;
    size_t count(const std::string& name) const;

private:
    mutable std::mutex mutex_;
    std::vector<HookRegistration> hooks_;
    HookId next_id_ = 1;
};

// Resolves an exported symbol from a shared object through dlopen/dlsym.
// Returns nullptr and fills error on failure.  The library handle stays open
// for the lifetime of the process.
void* resolve_symbol(const std::string& library_path, const std::string& symbol,
                     std::string& error);

} // namespace nexus::mod
