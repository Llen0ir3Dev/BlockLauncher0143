#include <nexus/mod/mod.hpp>

#include <algorithm>
#include <cctype>
#include <dlfcn.h>
#include <memory>
#include <set>
#include <sstream>

#include <nexus/core/fs.hpp>
#include <nexus/core/log.hpp>
#include <nexus/core/strings.hpp>
#include <nexus/core/time.hpp>
#include <nexus/core/version.hpp>
#include <nexus/mod/native_api.h>
#include <nexus/mod/script.hpp>
#include <nexus/worldgen/world.hpp>

namespace nexus::mod {

const char* mod_type_name(ModType type) {
    switch (type) {
        case ModType::Script: return "script";
        case ModType::Native: return "native";
        case ModType::Unknown: break;
    }
    return "unknown";
}

const char* mod_state_name(ModState state) {
    switch (state) {
        case ModState::Discovered: return "discovered";
        case ModState::Loaded: return "loaded";
        case ModState::Active: return "active";
        case ModState::Failed: return "failed";
        case ModState::Unloaded: return "unloaded";
    }
    return "unknown";
}

namespace {

bool valid_mod_id(const std::string& id) {
    if (id.empty() || id.size() > 64) return false;
    for (char c : id) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (!std::isalnum(uc) && c != '.' && c != '_' && c != '-') return false;
    }
    return true;
}

bool safe_relative_entry(const std::string& entry) {
    if (entry.empty() || entry.size() > 256) return false;
    if (entry.front() == '/' || entry.front() == '\\') return false;
    if (entry.find("..") != std::string::npos) return false;
    if (entry.find('\\') != std::string::npos) return false;
    return true;
}

std::string json_string(const nlohmann::json& json, const char* key,
                        const std::string& fallback = "") {
    const auto it = json.find(key);
    if (it == json.end() || !it->is_string()) return fallback;
    return it->get<std::string>();
}

std::vector<std::string> json_strings(const nlohmann::json& json, const char* key) {
    std::vector<std::string> out;
    const auto it = json.find(key);
    if (it == json.end() || !it->is_array()) return out;
    for (const auto& item : *it) {
        if (item.is_string()) out.push_back(item.get<std::string>());
    }
    return out;
}

} // namespace

bool parse_manifest_json(const nlohmann::json& json, const std::string& directory,
                         ModManifest& manifest, std::string& error) {
    if (!json.is_object()) {
        error = "mod.json must contain a JSON object";
        return false;
    }
    manifest = ModManifest{};
    manifest.raw = json;
    manifest.id = json_string(json, "id");
    if (!valid_mod_id(manifest.id)) {
        error = "mod id is missing or invalid (expected [A-Za-z0-9._-]{1,64})";
        return false;
    }
    manifest.name = json_string(json, "name", manifest.id);
    manifest.version = json_string(json, "version", "0.0.0");
    manifest.author = json_string(json, "author");
    manifest.description = json_string(json, "description");
    manifest.api_version = json_string(json, "apiVersion", "1.0");
    manifest.entry = json_string(json, "entry", "main.js");
    if (!safe_relative_entry(manifest.entry)) {
        error = "mod entry path must be a safe relative path: " + manifest.entry;
        return false;
    }
    manifest.permissions = json_strings(json, "permissions");
    manifest.events = json_strings(json, "events");

    if (!manifest.api_version.empty() && manifest.api_version.front() != '1') {
        error = "unsupported mod API version: " + manifest.api_version + " (host supports 1.x)";
        return false;
    }
    const std::string type = to_lower(json_string(json, "type", ""));
    if (type == "native") manifest.type = ModType::Native;
    else if (type == "script" || type.empty()) manifest.type = ModType::Script;
    else {
        error = "unknown mod type: " + type + " (expected script or native)";
        return false;
    }
    if (ends_with(to_lower(manifest.entry), ".so") || ends_with(to_lower(manifest.entry), ".dll")) {
        manifest.type = ModType::Native;
    }
    if (manifest.type == ModType::Native &&
        std::find(manifest.permissions.begin(), manifest.permissions.end(), "native") ==
            manifest.permissions.end()) {
        manifest.permissions.push_back("native");
    }
    if (!fs::is_directory(directory)) {
        error = "mod directory does not exist: " + directory;
        return false;
    }
    return true;
}

bool load_manifest(const std::string& mod_directory, ModManifest& manifest, std::string& error) {
    const std::string path = fs::join(mod_directory, "mod.json");
    std::string text;
    if (!fs::read_file(path, text, 1024 * 1024)) {
        error = "cannot read " + path;
        return false;
    }
    nlohmann::json json;
    try {
        json = nlohmann::json::parse(text);
    } catch (const std::exception& ex) {
        error = "invalid JSON in " + path + ": " + ex.what();
        return false;
    }
    return parse_manifest_json(json, mod_directory, manifest, error);
}

nlohmann::json ModRecord::to_json() const {
    return {
        {"id", manifest.id},
        {"name", manifest.name},
        {"version", manifest.version},
        {"author", manifest.author},
        {"description", manifest.description},
        {"type", mod_type_name(manifest.type)},
        {"state", mod_state_name(state)},
        {"directory", directory},
        {"entry", entry_path},
        {"apiVersion", manifest.api_version},
        {"permissions", manifest.permissions},
        {"events", manifest.events},
        {"error", error},
        {"initResult", init_result},
        {"hooks", hook_count}
    };
}

struct ModHost::NativeState {
    void* library = nullptr;
    const NexusModDescriptor* descriptor = nullptr;
    int (*init)(const NexusModHost*) = nullptr;
    void (*shutdown)() = nullptr;
    void (*tick)(double) = nullptr;
    NexusModHost host{};
    ModHost* owner = nullptr;
    std::string id;
    std::vector<SubscriptionId> subscriptions;
    std::vector<HookId> hooks;
};

struct ModHost::ScriptState {
    std::unique_ptr<ScriptEngine> engine;
};

namespace {

NexusModHost make_native_host(ModHost::NativeState* state);

} // namespace

ModHost::ModHost(EventBus& events, ContentRegistry& registry, HookManager& hooks,
                 ModHostOptions options)
    : events_(events), registry_(registry), hooks_(hooks), options_(std::move(options)) {}

ModHost::~ModHost() {
    unload_all();
}

void ModHost::set_world(worldgen::WorldGenerator* world) {
    world_ = world;
    for (auto& entry : script_states_) {
        if (entry.second && entry.second->engine) entry.second->engine->set_world(world);
    }
}

size_t ModHost::discover(const std::string& directory) {
    if (!fs::is_directory(directory)) return 0;
    size_t count = 0;
    for (const auto& path : fs::list_directories(directory)) {
        if (!fs::exists(fs::join(path, "mod.json"))) continue;
        std::string error;
        if (add_manifest_path(path, error)) ++count;
        else {
            NEXUS_LOG_WARN("mod discovery: " + error);
        }
    }
    return count;
}

bool ModHost::add_manifest_path(const std::string& directory, std::string& error) {
    ModManifest manifest;
    if (!load_manifest(directory, manifest, error)) return false;
    for (const auto& existing : mods_) {
        if (existing.manifest.id == manifest.id) {
            error = "duplicate mod id '" + manifest.id + "'";
            return false;
        }
    }
    ModRecord record;
    record.manifest = manifest;
    record.directory = fs::absolute(directory);
    record.entry_path = fs::join(record.directory, manifest.entry);
    record.state = ModState::Discovered;
    if (!fs::is_regular_file(record.entry_path)) {
        record.state = ModState::Failed;
        record.error = "mod entry file does not exist: " + record.entry_path;
    }
    mods_.push_back(std::move(record));
    return true;
}

const ModRecord* ModHost::find(const std::string& id) const {
    for (const auto& record : mods_) {
        if (record.manifest.id == id) return &record;
    }
    return nullptr;
}

ModRecord* ModHost::find(const std::string& id) {
    for (auto& record : mods_) {
        if (record.manifest.id == id) return &record;
    }
    return nullptr;
}

size_t ModHost::active_count() const {
    return static_cast<size_t>(std::count_if(mods_.begin(), mods_.end(),
                                             [](const ModRecord& record) {
                                                 return record.state == ModState::Active;
                                             }));
}

bool ModHost::load_script(ModRecord& record, std::string& error) {
    ScriptOptions options;
    options.mod_id = record.manifest.id;
    options.sandbox = options_.sandbox_scripts;
    options.memory_limit = options_.script_memory_limit;
    options.game_version = options_.game_version;
    auto state = std::make_shared<ScriptState>();
    state->engine = std::make_unique<ScriptEngine>(events_, registry_, hooks_, options);
    state->engine->set_world(world_);
    if (!state->engine->initialize(error)) {
        record.state = ModState::Failed;
        record.error = error;
        return false;
    }
    if (!state->engine->eval_file(record.entry_path, error)) {
        state->engine->shutdown();
        record.state = ModState::Failed;
        record.error = error;
        return false;
    }
    const uint64_t handle = next_handle_++;
    record.handle = handle;
    record.state = ModState::Loaded;
    record.error.clear();
    if (state->engine->has_function("onLoad")) {
        nlohmann::json result;
        std::string call_error;
        if (!state->engine->call_function("onLoad", nlohmann::json::object(), result,
                                          call_error)) {
            record.error = call_error;
        }
    }
    script_states_[handle] = std::move(state);
    return true;
}

bool ModHost::load_native(ModRecord& record, std::string& error) {
    if (!options_.allow_native) {
        error = "native mods are disabled; start the host with --allow-native";
        record.state = ModState::Failed;
        record.error = error;
        return false;
    }
    void* library = dlopen(record.entry_path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (library == nullptr) {
        const char* message = dlerror();
        error = std::string("dlopen failed: ") + (message != nullptr ? message : "unknown");
        record.state = ModState::Failed;
        record.error = error;
        return false;
    }
    auto state = std::make_shared<NativeState>();
    state->library = library;
    state->owner = this;
    state->id = record.manifest.id;
    auto descriptor_fn = reinterpret_cast<const NexusModDescriptor* (*)()>(
        dlsym(library, "nexus_mod_descriptor"));
    state->init = reinterpret_cast<int (*)(const NexusModHost*)>(
        dlsym(library, "nexus_mod_init"));
    state->shutdown = reinterpret_cast<void (*)()>(dlsym(library, "nexus_mod_shutdown"));
    state->tick = reinterpret_cast<void (*)(double)>(dlsym(library, "nexus_mod_tick"));
    if (descriptor_fn == nullptr || state->init == nullptr || state->shutdown == nullptr) {
        error = "native mod must export nexus_mod_descriptor, nexus_mod_init and "
                "nexus_mod_shutdown";
        dlclose(library);
        record.state = ModState::Failed;
        record.error = error;
        return false;
    }
    state->descriptor = descriptor_fn();
    if (state->descriptor == nullptr) {
        error = "nexus_mod_descriptor() returned null";
        dlclose(library);
        record.state = ModState::Failed;
        record.error = error;
        return false;
    }
    if (state->descriptor->abi_version != NEXUS_MOD_ABI_VERSION) {
        error = "native mod ABI " + std::to_string(state->descriptor->abi_version) +
                " does not match host ABI " + std::to_string(NEXUS_MOD_ABI_VERSION);
        dlclose(library);
        record.state = ModState::Failed;
        record.error = error;
        return false;
    }
    if (state->descriptor->id == nullptr ||
        record.manifest.id != state->descriptor->id) {
        error = "native descriptor id does not match mod.json id";
        dlclose(library);
        record.state = ModState::Failed;
        record.error = error;
        return false;
    }
    state->host = make_native_host(state.get());
    const int result = state->init(&state->host);
    if (result != 0) {
        error = "nexus_mod_init returned " + std::to_string(result);
        if (state->shutdown != nullptr) state->shutdown();
        dlclose(library);
        record.state = ModState::Failed;
        record.error = error;
        return false;
    }
    record.handle = next_handle_++;
    record.state = ModState::Loaded;
    record.init_result = result;
    record.hook_count = state->hooks.size();
    native_states_[record.handle] = std::move(state);
    return true;
}

bool ModHost::load(const std::string& id, std::string& error) {
    ModRecord* record = find(id);
    if (record == nullptr) {
        error = "mod not found: " + id;
        return false;
    }
    if (record->state == ModState::Loaded || record->state == ModState::Active) {
        return true;
    }
    if (record->state == ModState::Failed && !record->error.empty()) {
        // A failed record stays failed until explicitly reloaded.
        error = record->error;
        return false;
    }
    record->state = ModState::Discovered;
    record->error.clear();
    if (record->manifest.type == ModType::Native) return load_native(*record, error);
    return load_script(*record, error);
}

bool ModHost::activate(const std::string& id, std::string& error) {
    ModRecord* record = find(id);
    if (record == nullptr) {
        error = "mod not found: " + id;
        return false;
    }
    if (record->state == ModState::Active) return true;
    if (record->state != ModState::Loaded) {
        if (!load(id, error)) return false;
        record = find(id);
    }
    if (record->manifest.type == ModType::Script) {
        auto it = script_states_.find(record->handle);
        if (it != script_states_.end() && it->second->engine &&
            it->second->engine->has_function("onEnable")) {
            nlohmann::json result;
            std::string call_error;
            if (!it->second->engine->call_function("onEnable", nlohmann::json::object(),
                                                   result, call_error)) {
                error = call_error;
                record->state = ModState::Failed;
                record->error = error;
                return false;
            }
        }
    }
    record->state = ModState::Active;
    nlohmann::json payload = {{"id", record->manifest.id},
                              {"name", record->manifest.name},
                              {"type", mod_type_name(record->manifest.type)}};
    events_.emit("mod.activate", payload, record->manifest.id);
    return true;
}

bool ModHost::enable(const std::string& id, std::string& error) {
    if (!load(id, error)) return false;
    return activate(id, error);
}

void ModHost::unload_script(ModRecord& record) {
    auto it = script_states_.find(record.handle);
    if (it == script_states_.end()) return;
    if (it->second->engine) {
        if (it->second->engine->has_function("onDisable")) {
            nlohmann::json result;
            std::string error;
            it->second->engine->call_function("onDisable", nlohmann::json::object(), result,
                                              error);
        }
        it->second->engine->shutdown();
    }
    script_states_.erase(it);
    registry_.remove_mod(record.manifest.id);
    hooks_.remove_mod(record.manifest.id);
}

void ModHost::unload_native(ModRecord& record) {
    auto it = native_states_.find(record.handle);
    if (it == native_states_.end()) return;
    auto state = it->second;
    for (SubscriptionId id : state->subscriptions) events_.unsubscribe(id);
    for (HookId id : state->hooks) hooks_.remove(id);
    if (state->shutdown != nullptr) state->shutdown();
    if (state->library != nullptr) dlclose(state->library);
    native_states_.erase(it);
    registry_.remove_mod(record.manifest.id);
    hooks_.remove_mod(record.manifest.id);
}

bool ModHost::unload(const std::string& id, std::string& error) {
    ModRecord* record = find(id);
    if (record == nullptr) {
        error = "mod not found: " + id;
        return false;
    }
    if (record->manifest.type == ModType::Native) unload_native(*record);
    else unload_script(*record);
    record->state = ModState::Unloaded;
    record->handle = 0;
    record->hook_count = 0;
    nlohmann::json payload = {{"id", record->manifest.id}};
    events_.emit("mod.deactivate", payload, record->manifest.id);
    return true;
}

size_t ModHost::unload_all() {
    size_t count = 0;
    for (auto& record : mods_) {
        if (record.state == ModState::Loaded || record.state == ModState::Active ||
            record.state == ModState::Failed) {
            std::string error;
            if (unload(record.manifest.id, error) || record.state == ModState::Unloaded) ++count;
        }
    }
    return count;
}

bool ModHost::emit(const std::string& name, nlohmann::json data) {
    return events_.emit(name, std::move(data), "mod-host");
}

nlohmann::json ModHost::to_json() const {
    nlohmann::json out;
    out["mods"] = nlohmann::json::array();
    for (const auto& record : mods_) out["mods"].push_back(record.to_json());
    out["active"] = active_count();
    out["registry"] = registry_.to_json();
    out["hooks"] = hooks_.names();
    out["hookCount"] = hooks_.size();
    return out;
}

namespace {

void native_log_bridge(void* userdata, int level, const char* message) {
    (void)userdata;
    const int clamped = std::max(0, std::min(5, level));
    log_message(static_cast<LogLevel>(clamped), "native-mod", 0,
                message != nullptr ? message : "");
}

int native_emit_bridge(void* userdata, const char* event_name, const char* json_payload) {
    auto* state = static_cast<ModHost::NativeState*>(userdata);
    if (state == nullptr || state->owner == nullptr || event_name == nullptr) return 0;
    nlohmann::json payload = nlohmann::json::object();
    if (json_payload != nullptr && *json_payload != '\0') {
        try {
            payload = nlohmann::json::parse(json_payload);
        } catch (const std::exception&) {
            payload = nlohmann::json::object();
        }
    }
    return state->owner->emit(event_name, payload) ? 1 : 0;
}

uint64_t native_subscribe_bridge(void* userdata, const char* event_name, int priority,
                                 NexusEventCallback callback, void* callback_userdata) {
    auto* state = static_cast<ModHost::NativeState*>(userdata);
    if (state == nullptr || callback == nullptr || event_name == nullptr) return 0;
    const std::string name = event_name;
    const SubscriptionId id = state->owner->events().subscribe(
        name,
        [callback, callback_userdata, name](Event& event) {
            const std::string payload = event.data.dump();
            const char* result = callback(callback_userdata, name.c_str(), payload.c_str());
            if (result == nullptr || *result == '\0') return;
            try {
                const nlohmann::json updated = nlohmann::json::parse(result);
                if (updated.is_object()) {
                    for (auto item = updated.begin(); item != updated.end(); ++item) {
                        event.data[item.key()] = item.value();
                    }
                    if (updated.contains("cancelled") && updated["cancelled"].is_boolean()) {
                        event.cancelled = updated["cancelled"].get<bool>();
                    }
                }
            } catch (const std::exception&) {
                // Invalid JSON from a native mod is ignored rather than
                // corrupting the event stream.
            }
        },
        priority);
    state->subscriptions.push_back(id);
    return id;
}

int native_unsubscribe_bridge(void* userdata, uint64_t subscription_id) {
    auto* state = static_cast<ModHost::NativeState*>(userdata);
    if (state == nullptr) return 0;
    const bool ok = state->owner->events().unsubscribe(subscription_id);
    state->subscriptions.erase(
        std::remove(state->subscriptions.begin(), state->subscriptions.end(), subscription_id),
        state->subscriptions.end());
    return ok ? 1 : 0;
}

void* native_resolve_bridge(void* userdata, const char* library, const char* symbol) {
    (void)userdata;
    std::string error;
    void* address = resolve_symbol(library != nullptr ? library : "",
                                   symbol != nullptr ? symbol : "", error);
    if (address == nullptr) {
        NEXUS_LOG_WARN("native mod symbol lookup failed: " + error);
    }
    return address;
}

int native_register_block_bridge(void* userdata, const char* json_definition) {
    auto* state = static_cast<ModHost::NativeState*>(userdata);
    if (state == nullptr || json_definition == nullptr) return -1;
    try {
        const nlohmann::json json = nlohmann::json::parse(json_definition);
        std::string error;
        const int id = state->owner->registry().register_block(json, state->id, error);
        if (id < 0) NEXUS_LOG_WARN("native block registration failed: " + error);
        return id;
    } catch (const std::exception& ex) {
        NEXUS_LOG_WARN(std::string("native block JSON invalid: ") + ex.what());
        return -1;
    }
}

int native_register_item_bridge(void* userdata, const char* json_definition) {
    auto* state = static_cast<ModHost::NativeState*>(userdata);
    if (state == nullptr || json_definition == nullptr) return -1;
    try {
        const nlohmann::json json = nlohmann::json::parse(json_definition);
        std::string error;
        const int id = state->owner->registry().register_item(json, state->id, error);
        if (id < 0) NEXUS_LOG_WARN("native item registration failed: " + error);
        return id;
    } catch (const std::exception& ex) {
        NEXUS_LOG_WARN(std::string("native item JSON invalid: ") + ex.what());
        return -1;
    }
}

int native_register_entity_bridge(void* userdata, const char* json_definition) {
    auto* state = static_cast<ModHost::NativeState*>(userdata);
    if (state == nullptr || json_definition == nullptr) return -1;
    try {
        const nlohmann::json json = nlohmann::json::parse(json_definition);
        std::string error;
        const int id = state->owner->registry().register_entity(json, state->id, error);
        if (id < 0) NEXUS_LOG_WARN("native entity registration failed: " + error);
        return id;
    } catch (const std::exception& ex) {
        NEXUS_LOG_WARN(std::string("native entity JSON invalid: ") + ex.what());
        return -1;
    }
}

uint64_t native_register_hook_bridge(void* userdata, const char* hook_name, int priority,
                                     NexusEventCallback callback, void* callback_userdata) {
    auto* state = static_cast<ModHost::NativeState*>(userdata);
    if (state == nullptr || callback == nullptr || hook_name == nullptr) return 0;
    const std::string name = hook_name;
    const HookId id = state->owner->hooks().add(
        name, priority, state->id,
        [callback, callback_userdata, name](HookContext& context) {
            const std::string payload = context.payload.dump();
            const char* result = callback(callback_userdata, name.c_str(), payload.c_str());
            if (result == nullptr || *result == '\0') return;
            try {
                const nlohmann::json updated = nlohmann::json::parse(result);
                if (updated.is_object()) {
                    for (auto item = updated.begin(); item != updated.end(); ++item) {
                        context.payload[item.key()] = item.value();
                    }
                    if (updated.contains("cancel") && updated["cancel"].is_boolean()) {
                        context.cancelled = updated["cancel"].get<bool>();
                    }
                }
            } catch (const std::exception&) {
            }
        });
    state->hooks.push_back(id);
    return id;
}

NexusModHost make_native_host(ModHost::NativeState* state) {
    NexusModHost host{};
    host.abi_version = NEXUS_MOD_ABI_VERSION;
    host.nexus_version = kVersion;
    host.userdata = state;
    host.log = native_log_bridge;
    host.emit = native_emit_bridge;
    host.subscribe = native_subscribe_bridge;
    host.unsubscribe = native_unsubscribe_bridge;
    host.resolve_symbol = native_resolve_bridge;
    host.register_block = native_register_block_bridge;
    host.register_item = native_register_item_bridge;
    host.register_entity = native_register_entity_bridge;
    host.register_hook = native_register_hook_bridge;
    return host;
}

} // namespace

} // namespace nexus::mod
