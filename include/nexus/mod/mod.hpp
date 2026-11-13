#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexus/core/event_bus.hpp>
#include <nexus/mod/hooks.hpp>
#include <nexus/mod/registry.hpp>

namespace nexus::worldgen {
class WorldGenerator;
}

namespace nexus::mod {

enum class ModType { Script, Native, Unknown };
enum class ModState { Discovered, Loaded, Active, Failed, Unloaded };

const char* mod_type_name(ModType type);
const char* mod_state_name(ModState state);

struct ModManifest {
    std::string id;
    std::string name;
    std::string version = "0.0.0";
    std::string author;
    std::string description;
    std::string entry = "main.js";
    std::string api_version = "1.0";
    ModType type = ModType::Script;
    std::vector<std::string> permissions;
    std::vector<std::string> events;
    nlohmann::json raw = nlohmann::json::object();
};

bool parse_manifest_json(const nlohmann::json& json, const std::string& directory,
                         ModManifest& manifest, std::string& error);
bool load_manifest(const std::string& mod_directory, ModManifest& manifest, std::string& error);

struct ModRecord {
    ModManifest manifest;
    std::string directory;
    std::string entry_path;
    ModState state = ModState::Discovered;
    std::string error;
    uint64_t handle = 0; // native library handle or script engine id
    int init_result = 0;
    size_t hook_count = 0;
    nlohmann::json to_json() const;
};

struct ModHostOptions {
    bool allow_native = false;   // native code is opt-in
    bool sandbox_scripts = true; // remove dangerous globals, cap memory
    size_t script_memory_limit = 32ull * 1024 * 1024;
    std::string game_version = "0.14.3";
};

// The mod host owns discovered mods, the content registry, hook manager and
// their event subscriptions.  A WorldGenerator can be attached to expose the
// world API to scripts.
class ModHost {
public:
    ModHost(EventBus& events, ContentRegistry& registry, HookManager& hooks,
            ModHostOptions options = {});
    ~ModHost();

    void set_world(worldgen::WorldGenerator* world);

    // Scans immediate subdirectories of `directory` for mod.json.  Returns the
    // number of manifests discovered.  Errors are stored per record.
    size_t discover(const std::string& directory);
    bool add_manifest_path(const std::string& directory, std::string& error);

    bool load(const std::string& id, std::string& error);
    bool activate(const std::string& id, std::string& error);
    bool enable(const std::string& id, std::string& error);
    bool unload(const std::string& id, std::string& error);
    size_t unload_all();

    const ModRecord* find(const std::string& id) const;
    ModRecord* find(const std::string& id);
    const std::vector<ModRecord>& mods() const { return mods_; }
    size_t active_count() const;

    // Emits an event through the host event bus.  Scripts registered with
    // nexus.on() and native subscriptions both receive it.
    bool emit(const std::string& name, nlohmann::json data = nlohmann::json::object());

    EventBus& events() { return events_; }
    ContentRegistry& registry() { return registry_; }
    HookManager& hooks() { return hooks_; }
    const ModHostOptions& options() const { return options_; }

    nlohmann::json to_json() const;

    struct NativeState;
    struct ScriptState;

private:
    bool load_script(ModRecord& record, std::string& error);
    bool load_native(ModRecord& record, std::string& error);
    void unload_script(ModRecord& record);
    void unload_native(ModRecord& record);

    EventBus& events_;
    ContentRegistry& registry_;
    HookManager& hooks_;
    ModHostOptions options_;
    worldgen::WorldGenerator* world_ = nullptr;
    std::vector<ModRecord> mods_;
    std::map<uint64_t, std::shared_ptr<NativeState>> native_states_;
    std::map<uint64_t, std::shared_ptr<ScriptState>> script_states_;
    uint64_t next_handle_ = 1;
};

} // namespace nexus::mod
