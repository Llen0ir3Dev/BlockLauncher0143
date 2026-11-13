#include <nexus/mod/hooks.hpp>

#include <algorithm>
#include <dlfcn.h>
#include <map>

namespace nexus::mod {

HookId HookManager::add(const std::string& name, int priority, const std::string& mod_id,
                        HookCallback callback) {
    if (name.empty() || !callback) return 0;
    std::lock_guard<std::mutex> lock(mutex_);
    HookRegistration registration;
    registration.id = next_id_++;
    registration.name = name;
    registration.mod_id = mod_id;
    registration.priority = priority;
    registration.callback = std::move(callback);
    hooks_.push_back(std::move(registration));
    return hooks_.back().id;
}

bool HookManager::remove(HookId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = std::find_if(hooks_.begin(), hooks_.end(),
                                 [&](const HookRegistration& hook) { return hook.id == id; });
    if (it == hooks_.end()) return false;
    hooks_.erase(it);
    return true;
}

size_t HookManager::remove_mod(const std::string& mod_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const size_t before = hooks_.size();
    hooks_.erase(std::remove_if(hooks_.begin(), hooks_.end(),
                                [&](const HookRegistration& hook) {
                                    return hook.mod_id == mod_id;
                                }),
                 hooks_.end());
    return before - hooks_.size();
}

void HookManager::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    hooks_.clear();
}

bool HookManager::trigger(HookContext& context) {
    std::vector<HookRegistration> selected;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& hook : hooks_) {
            if (hook.name == context.name) selected.push_back(hook);
        }
    }
    std::sort(selected.begin(), selected.end(),
              [](const HookRegistration& a, const HookRegistration& b) {
                  if (a.priority != b.priority) return a.priority < b.priority;
                  return a.id < b.id;
              });
    context.cancelled = false;
    for (auto& hook : selected) {
        context.mod_id = hook.mod_id;
        hook.callback(context);
        if (context.cancelled) return false;
    }
    return true;
}

bool HookManager::trigger(const std::string& name, nlohmann::json& payload,
                          std::string* cancelled_by) {
    HookContext context;
    context.name = name;
    context.payload = payload;
    const bool ok = trigger(context);
    payload = std::move(context.payload);
    if (!ok && cancelled_by) *cancelled_by = context.mod_id;
    return ok;
}

std::vector<std::string> HookManager::names() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> result;
    for (const auto& hook : hooks_) {
        if (std::find(result.begin(), result.end(), hook.name) == result.end()) {
            result.push_back(hook.name);
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

size_t HookManager::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return hooks_.size();
}

size_t HookManager::count(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<size_t>(std::count_if(hooks_.begin(), hooks_.end(),
                                             [&](const HookRegistration& hook) {
                                                 return hook.name == name;
                                             }));
}

void* resolve_symbol(const std::string& library_path, const std::string& symbol,
                     std::string& error) {
    error.clear();
    if (symbol.empty()) {
        error = "empty symbol name";
        return nullptr;
    }
    void* handle = RTLD_DEFAULT;
    if (!library_path.empty()) {
        handle = dlopen(library_path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (handle == nullptr) {
            const char* message = dlerror();
            error = message != nullptr ? message : "dlopen failed";
            return nullptr;
        }
    }
    dlerror();
    void* address = dlsym(handle, symbol.c_str());
    if (address == nullptr) {
        const char* message = dlerror();
        error = message != nullptr ? message : ("symbol not found: " + symbol);
    }
    return address;
}

} // namespace nexus::mod
