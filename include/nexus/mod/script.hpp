#pragma once

#include <cstdint>
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

struct ScriptOptions {
    bool sandbox = true;
    size_t memory_limit = 32ull * 1024 * 1024;
    std::string mod_id = "script";
    std::string game_version = "0.14.3";
    int max_event_depth = 16;
};

// Embedded JavaScript runtime (Duktape) with a narrow capability API.
//
// The engine exposes:
//   nexus.log(level, message)
//   nexus.on(event, callback[, priority])
//   nexus.emit(event, payload)
//   nexus.hook(name, callback[, priority])
//   nexus.registerBlock(object)
//   nexus.registerItem(object)
//   nexus.registerEntity(object)
//   nexus.world.heightAt(x, z)
//   nexus.world.biomeAt(x, z)
//   nexus.world.getBlock(x, y, z)
//   nexus.world.setBlock(x, y, z, id)
//   nexus.world.generateChunk(cx, cz)
//   nexus.version / nexus.gameVersion / nexus.modId
//
// It has no filesystem, process, network or native-memory access.  CPU time is
// not bounded by Duktape; event recursion is bounded and the heap is capped.
class ScriptEngine {
public:
    ScriptEngine(EventBus& events, ContentRegistry& registry, HookManager& hooks,
                 ScriptOptions options = {});
    ~ScriptEngine();

    ScriptEngine(const ScriptEngine&) = delete;
    ScriptEngine& operator=(const ScriptEngine&) = delete;

    bool initialize(std::string& error);
    void shutdown();
    bool initialized() const;
    bool healthy() const { return healthy_; }

    bool eval(const std::string& source, const std::string& name, std::string& error);
    bool eval_file(const std::string& path, std::string& error);
    bool has_function(const std::string& name) const;
    bool call_function(const std::string& name, const nlohmann::json& argument,
                       nlohmann::json& result, std::string& error);

    void set_world(worldgen::WorldGenerator* world) { world_ = world; }
    worldgen::WorldGenerator* world() const { return world_; }

    // Delivers an event to every nexus.on() handler and merges a returned
    // object into the event payload.  Used by the host when the event bus fires.
    bool dispatch_event(const std::string& name, nlohmann::json& payload, std::string& error);

    // Fires a registered script hook.  Returns false when cancelled.
    bool dispatch_hook(HookContext& context, std::string& error);

    const std::vector<std::string>& errors() const { return errors_; }
    size_t event_handler_count() const;
    size_t hook_count() const;
    const std::string& mod_id() const { return options_.mod_id; }
    const ScriptOptions& options() const { return options_; }

    // Opaque implementation.  Declared public so helpers in the translation
    // unit can name the type; its definition remains private to script.cpp.
    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
    EventBus& events_;
    ContentRegistry& registry_;
    HookManager& hooks_;
    ScriptOptions options_;
    worldgen::WorldGenerator* world_ = nullptr;
    bool healthy_ = false;
    std::vector<std::string> errors_;
};

} // namespace nexus::mod
