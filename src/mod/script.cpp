#include <nexus/mod/script.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <new>
#include <sstream>

#include <nexus/core/fs.hpp>
#include <nexus/core/log.hpp>
#include <nexus/core/strings.hpp>
#include <nexus/core/version.hpp>
#include <nexus/worldgen/world.hpp>

#if NEXUS_ENABLE_SCRIPTING
#include <duktape.h>
#endif

namespace nexus::mod {

namespace {

#if NEXUS_ENABLE_SCRIPTING

union AllocHeader {
    size_t size;
    std::max_align_t align;
};

struct LimitedAllocator {
    size_t used = 0;
    size_t limit = 32ull * 1024 * 1024;
    bool exhausted = false;
};

void* limited_alloc(void* udata, duk_size_t size) {
    auto* allocator = static_cast<LimitedAllocator*>(udata);
    if (size == 0) size = 1;
    if (allocator->used + size > allocator->limit) {
        allocator->exhausted = true;
        return nullptr;
    }
    auto* header = static_cast<AllocHeader*>(std::malloc(sizeof(AllocHeader) + size));
    if (header == nullptr) return nullptr;
    header->size = size;
    allocator->used += size;
    return reinterpret_cast<void*>(header + 1);
}

void* limited_realloc(void* udata, void* ptr, duk_size_t size) {
    auto* allocator = static_cast<LimitedAllocator*>(udata);
    if (ptr == nullptr) return limited_alloc(udata, size);
    auto* header = static_cast<AllocHeader*>(ptr) - 1;
    if (size == 0) {
        allocator->used -= header->size;
        std::free(header);
        return nullptr;
    }
    const size_t old_size = header->size;
    if (allocator->used - old_size + size > allocator->limit) {
        allocator->exhausted = true;
        return nullptr;
    }
    auto* resized = static_cast<AllocHeader*>(std::realloc(header, sizeof(AllocHeader) + size));
    if (resized == nullptr) return nullptr;
    resized->size = size;
    allocator->used = allocator->used - old_size + size;
    return reinterpret_cast<void*>(resized + 1);
}

void limited_free(void* udata, void* ptr) {
    if (ptr == nullptr) return;
    auto* allocator = static_cast<LimitedAllocator*>(udata);
    auto* header = static_cast<AllocHeader*>(ptr) - 1;
    allocator->used -= header->size;
    std::free(header);
}

void fatal_handler(void* udata, const char* message) {
    (void)udata;
    std::fprintf(stderr, "NEXUS script fatal: %s\n", message != nullptr ? message : "unknown");
    std::abort();
}

void push_handlers_array(duk_context* ctx) {
    duk_push_global_stash(ctx);
    if (duk_get_prop_string(ctx, -1, "nexus_handlers") && duk_is_array(ctx, -1)) {
        duk_remove(ctx, -2);
        return;
    }
    duk_pop(ctx);
    duk_push_array(ctx);
    duk_dup(ctx, -1);
    duk_put_prop_string(ctx, -3, "nexus_handlers");
    duk_remove(ctx, -2);
}

void store_handler(duk_context* ctx, duk_uint_t index, duk_idx_t function_index) {
    push_handlers_array(ctx);
    const duk_idx_t array_index = duk_get_top_index(ctx);
    duk_dup(ctx, function_index);
    duk_put_prop_index(ctx, array_index, index);
    duk_pop(ctx);
}

#endif

} // namespace

struct ScriptEngine::Impl {
    ScriptEngine* owner = nullptr;
    EventBus* events = nullptr;
    ContentRegistry* registry = nullptr;
    HookManager* hooks = nullptr;
    worldgen::WorldGenerator** world = nullptr;
    std::vector<std::string>* errors = nullptr;
    int max_depth = 16;

#if NEXUS_ENABLE_SCRIPTING
    duk_context* ctx = nullptr;
    LimitedAllocator allocator;
    std::map<std::string, std::vector<duk_uint_t>> handlers;
    std::vector<std::pair<std::string, SubscriptionId>> subscriptions;
    std::vector<HookId> hook_ids;
    std::map<duk_uint_t, HookId> handler_hooks;
    duk_uint_t next_handler = 0;
    int depth = 0;
    bool healthy = false;
    std::string last_error;

    bool push_handler(duk_uint_t index) {
        push_handlers_array(ctx);
        duk_get_prop_index(ctx, -1, index);
        duk_remove(ctx, -2);
        if (!duk_is_function(ctx, -1)) {
            duk_pop(ctx);
            return false;
        }
        return true;
    }

    bool call_handler(duk_uint_t index, const nlohmann::json& payload, nlohmann::json& result,
                      std::string& error) {
        if (!push_handler(index)) {
            error = "script handler disappeared";
            return false;
        }
        const std::string text = payload.dump();
        duk_push_lstring(ctx, text.data(), text.size());
        duk_json_decode(ctx, -1);
        if (duk_pcall(ctx, 1) != 0) {
            error = duk_safe_to_string(ctx, -1);
            duk_pop(ctx);
            return false;
        }
        if (duk_is_object(ctx, -1) && !duk_is_function(ctx, -1)) {
            const char* encoded = duk_json_encode(ctx, -1);
            try {
                result = nlohmann::json::parse(encoded != nullptr ? encoded : "{}");
            } catch (const std::exception& ex) {
                error = std::string("invalid handler result: ") + ex.what();
                duk_pop(ctx);
                return false;
            }
        }
        duk_pop(ctx);
        return true;
    }

    bool dispatch_event(const std::string& name, nlohmann::json& payload, std::string& error) {
        const auto it = handlers.find(name);
        if (it == handlers.end() || it->second.empty()) return true;
        if (depth >= max_depth) {
            error = "event recursion limit reached while dispatching '" + name + "'";
            return false;
        }
        ++depth;
        bool all_ok = true;
        for (duk_uint_t index : it->second) {
            nlohmann::json result = nlohmann::json::object();
            std::string handler_error;
            if (call_handler(index, payload, result, handler_error)) {
                if (result.is_object()) {
                    for (auto item = result.begin(); item != result.end(); ++item) {
                        payload[item.key()] = item.value();
                    }
                }
            } else {
                all_ok = false;
                error = handler_error;
                if (errors != nullptr) errors->push_back(handler_error);
            }
        }
        --depth;
        return all_ok;
    }

    bool dispatch_hook(duk_uint_t index, HookContext& context, std::string& error) {
        nlohmann::json result = nlohmann::json::object();
        if (!call_handler(index, context.payload, result, error)) return false;
        if (result.is_object()) {
            for (auto item = result.begin(); item != result.end(); ++item) {
                context.payload[item.key()] = item.value();
            }
            if (result.contains("cancelled") && result["cancelled"].is_boolean() &&
                result["cancelled"].get<bool>()) {
                context.cancelled = true;
            }
            if (result.contains("cancel") && result["cancel"].is_boolean() &&
                result["cancel"].get<bool>()) {
                context.cancelled = true;
            }
        }
        return true;
    }

    void subscribe_if_needed(const std::string& name) {
        for (const auto& entry : subscriptions) {
            if (entry.first == name) return;
        }
        ScriptEngine* engine = owner;
        Impl* self = this;
        const std::string event_name = name;
        const SubscriptionId id = events->subscribe(
            event_name,
            [engine, self, event_name](Event& event) {
                (void)engine;
                std::string error;
                nlohmann::json payload = event.data;
                if (!self->dispatch_event(event_name, payload, error)) {
                    event.cancelled = true;
                    if (self->errors != nullptr) self->errors->push_back(error);
                }
                event.data = std::move(payload);
            },
            0);
        subscriptions.emplace_back(name, id);
    }

    void register_api();
#endif
};

#if NEXUS_ENABLE_SCRIPTING

namespace {

ScriptEngine::Impl* impl_of(duk_context* ctx) {
    duk_push_global_stash(ctx);
    duk_get_prop_string(ctx, -1, "\xffnexus_impl");
    auto* impl = static_cast<ScriptEngine::Impl*>(duk_get_pointer(ctx, -1));
    duk_pop_2(ctx);
    return impl;
}

int log_level_from_value(duk_context* ctx, duk_idx_t idx) {
    if (duk_is_number(ctx, idx)) return duk_get_int(ctx, idx);
    if (duk_is_string(ctx, idx)) {
        const std::string level = to_lower(duk_get_string(ctx, idx));
        if (level == "trace") return 0;
        if (level == "debug") return 1;
        if (level == "info") return 2;
        if (level == "warn" || level == "warning") return 3;
        if (level == "error") return 4;
        if (level == "fatal") return 5;
    }
    return 2;
}

duk_ret_t js_log(duk_context* ctx) {
    auto* impl = impl_of(ctx);
    if (impl == nullptr) return 0;
    const int level = log_level_from_value(ctx, 0);
    std::string message;
    const duk_idx_t top = duk_get_top(ctx);
    for (duk_idx_t i = 1; i < top; ++i) {
        if (i > 1) message += " ";
        message += duk_safe_to_string(ctx, i);
    }
    if (top == 1) message = duk_safe_to_string(ctx, 0);
    log_message(static_cast<LogLevel>(std::clamp(level, 0, 5)), __FILE__, __LINE__,
                "[script " + (impl->owner != nullptr ? impl->owner->mod_id() : std::string("mod")) +
                    "] " + message);
    return 0;
}

duk_ret_t js_on(duk_context* ctx) {
    auto* impl = impl_of(ctx);
    if (impl == nullptr) return 0;
    if (!duk_is_string(ctx, 0) || !duk_is_function(ctx, 1)) {
        return duk_error(ctx, DUK_ERR_TYPE_ERROR, "nexus.on(event, callback[, priority])");
    }
    const std::string event_name = duk_get_string(ctx, 0);
    const duk_uint_t index = impl->next_handler++;
    store_handler(ctx, index, 1);
    impl->handlers[event_name].push_back(index);
    impl->subscribe_if_needed(event_name);
    return 0;
}

duk_ret_t js_emit(duk_context* ctx) {
    auto* impl = impl_of(ctx);
    if (impl == nullptr) return 0;
    if (!duk_is_string(ctx, 0)) {
        return duk_error(ctx, DUK_ERR_TYPE_ERROR, "nexus.emit(event[, payload])");
    }
    const std::string event_name = duk_get_string(ctx, 0);
    nlohmann::json payload = nlohmann::json::object();
    if (duk_get_top(ctx) > 1 && duk_is_object(ctx, 1)) {
        const char* encoded = duk_json_encode(ctx, 1);
        try {
            payload = nlohmann::json::parse(encoded != nullptr ? encoded : "{}");
        } catch (const std::exception&) {
            payload = nlohmann::json::object();
        }
    }
    duk_push_boolean(ctx, impl->events->emit(event_name, payload, impl->owner->mod_id()) ? 1 : 0);
    return 1;
}

duk_ret_t js_hook(duk_context* ctx) {
    auto* impl = impl_of(ctx);
    if (impl == nullptr) return 0;
    if (!duk_is_string(ctx, 0) || !duk_is_function(ctx, 1)) {
        return duk_error(ctx, DUK_ERR_TYPE_ERROR, "nexus.hook(name, callback[, priority])");
    }
    const std::string name = duk_get_string(ctx, 0);
    const int priority = duk_is_number(ctx, 2) ? duk_get_int(ctx, 2) : 0;
    const duk_uint_t index = impl->next_handler++;
    store_handler(ctx, index, 1);
    ScriptEngine::Impl* self = impl;
    const HookId id = impl->hooks->add(
        name, priority, impl->owner->mod_id(),
        [self, index](HookContext& context) {
            std::string error;
            if (!self->dispatch_hook(index, context, error)) {
                if (self->errors != nullptr) self->errors->push_back(error);
            }
        });
    impl->hook_ids.push_back(id);
    impl->handler_hooks[index] = id;
    duk_push_number(ctx, static_cast<duk_double_t>(id));
    return 1;
}

duk_ret_t register_kind(duk_context* ctx, int kind) {
    auto* impl = impl_of(ctx);
    if (impl == nullptr) return 0;
    if (!duk_is_object(ctx, 0)) {
        return duk_error(ctx, DUK_ERR_TYPE_ERROR, "registration requires an object");
    }
    nlohmann::json definition;
    try {
        const char* encoded = duk_json_encode(ctx, 0);
        definition = nlohmann::json::parse(encoded != nullptr ? encoded : "{}");
    } catch (const std::exception& ex) {
        return duk_error(ctx, DUK_ERR_ERROR, "invalid registration object: %s", ex.what());
    }
    std::string error;
    const std::string mod_id = impl->owner->mod_id();
    int id = -1;
    if (kind == 0) id = impl->registry->register_block(definition, mod_id, error);
    else if (kind == 1) id = impl->registry->register_item(definition, mod_id, error);
    else id = impl->registry->register_entity(definition, mod_id, error);
    if (id < 0) {
        return duk_error(ctx, DUK_ERR_ERROR, "%s", error.c_str());
    }
    duk_push_int(ctx, id);
    return 1;
}

duk_ret_t js_register_block(duk_context* ctx) { return register_kind(ctx, 0); }
duk_ret_t js_register_item(duk_context* ctx) { return register_kind(ctx, 1); }
duk_ret_t js_register_entity(duk_context* ctx) { return register_kind(ctx, 2); }

worldgen::WorldGenerator* require_world(duk_context* ctx, ScriptEngine::Impl* impl) {
    if (impl->world == nullptr || *impl->world == nullptr) {
        duk_error(ctx, DUK_ERR_ERROR, "no world is attached to this script engine");
        return nullptr;
    }
    return *impl->world;
}

duk_ret_t js_world_height(duk_context* ctx) {
    auto* impl = impl_of(ctx);
    if (impl == nullptr) return 0;
    auto* world = require_world(ctx, impl);
    if (world == nullptr) return 0;
    const int x = duk_require_int(ctx, 0);
    const int z = duk_require_int(ctx, 1);
    duk_push_int(ctx, world->height_at(x, z));
    return 1;
}

duk_ret_t js_world_biome(duk_context* ctx) {
    auto* impl = impl_of(ctx);
    if (impl == nullptr) return 0;
    auto* world = require_world(ctx, impl);
    if (world == nullptr) return 0;
    const int x = duk_require_int(ctx, 0);
    const int z = duk_require_int(ctx, 1);
    duk_push_int(ctx, static_cast<int>(world->biome_at(x, z)));
    return 1;
}

duk_ret_t js_world_get_block(duk_context* ctx) {
    auto* impl = impl_of(ctx);
    if (impl == nullptr) return 0;
    auto* world = require_world(ctx, impl);
    if (world == nullptr) return 0;
    const int x = duk_require_int(ctx, 0);
    const int y = duk_require_int(ctx, 1);
    const int z = duk_require_int(ctx, 2);
    duk_push_int(ctx, static_cast<int>(world->block_at(x, y, z)));
    return 1;
}

duk_ret_t js_world_set_block(duk_context* ctx) {
    auto* impl = impl_of(ctx);
    if (impl == nullptr) return 0;
    auto* world = require_world(ctx, impl);
    if (world == nullptr) return 0;
    const int x = duk_require_int(ctx, 0);
    const int y = duk_require_int(ctx, 1);
    const int z = duk_require_int(ctx, 2);
    const int id = duk_require_int(ctx, 3);
    if (id < 0 || id > 255) {
        return duk_error(ctx, DUK_ERR_RANGE_ERROR, "block id must be 0..255");
    }
    duk_push_boolean(ctx, world->set_block(x, y, z, static_cast<worldgen::BlockId>(id)) ? 1 : 0);
    return 1;
}

duk_ret_t js_world_generate_chunk(duk_context* ctx) {
    auto* impl = impl_of(ctx);
    if (impl == nullptr) return 0;
    auto* world = require_world(ctx, impl);
    if (world == nullptr) return 0;
    const int cx = duk_require_int(ctx, 0);
    const int cz = duk_require_int(ctx, 1);
    const worldgen::Chunk& chunk = world->chunk_at(cx, cz);
    const worldgen::ChunkSummary summary = world->summarize(chunk);
    duk_push_object(ctx);
    duk_push_int(ctx, summary.coord.x);
    duk_put_prop_string(ctx, -2, "x");
    duk_push_int(ctx, summary.coord.z);
    duk_put_prop_string(ctx, -2, "z");
    duk_push_int(ctx, summary.minimum_height);
    duk_put_prop_string(ctx, -2, "minimumHeight");
    duk_push_int(ctx, summary.maximum_height);
    duk_put_prop_string(ctx, -2, "maximumHeight");
    duk_push_int(ctx, summary.solid_blocks);
    duk_put_prop_string(ctx, -2, "solidBlocks");
    duk_push_int(ctx, summary.water_blocks);
    duk_put_prop_string(ctx, -2, "waterBlocks");
    duk_push_int(ctx, summary.ore_blocks);
    duk_put_prop_string(ctx, -2, "oreBlocks");
    duk_push_int(ctx, summary.dominant_biome);
    duk_put_prop_string(ctx, -2, "dominantBiome");
    return 1;
}

void add_function(duk_context* ctx, duk_c_function function, duk_idx_t nargs, const char* name) {
    duk_push_c_function(ctx, function, nargs);
    duk_put_prop_string(ctx, -2, name);
}

} // namespace

void ScriptEngine::Impl::register_api() {
    duk_push_global_object(ctx);

    duk_push_object(ctx);
    duk_push_string(ctx, kVersion);
    duk_put_prop_string(ctx, -2, "version");
    duk_push_string(ctx, owner->options().game_version.c_str());
    duk_put_prop_string(ctx, -2, "gameVersion");
    duk_push_string(ctx, owner->mod_id().c_str());
    duk_put_prop_string(ctx, -2, "modId");
    add_function(ctx, js_log, DUK_VARARGS, "log");
    add_function(ctx, js_on, DUK_VARARGS, "on");
    add_function(ctx, js_emit, DUK_VARARGS, "emit");
    add_function(ctx, js_hook, DUK_VARARGS, "hook");
    add_function(ctx, js_register_block, 1, "registerBlock");
    add_function(ctx, js_register_item, 1, "registerItem");
    add_function(ctx, js_register_entity, 1, "registerEntity");

    duk_push_object(ctx);
    add_function(ctx, js_world_height, 2, "heightAt");
    add_function(ctx, js_world_biome, 2, "biomeAt");
    add_function(ctx, js_world_get_block, 3, "getBlock");
    add_function(ctx, js_world_set_block, 4, "setBlock");
    add_function(ctx, js_world_generate_chunk, 2, "generateChunk");
    duk_put_prop_string(ctx, -2, "world");

    duk_dup(ctx, -1);
    duk_put_prop_string(ctx, -3, "nexus");
    duk_dup(ctx, -1);
    duk_put_prop_string(ctx, -3, "NEXUS");
    duk_pop(ctx);

    // console.log alias.
    duk_push_object(ctx);
    add_function(ctx, js_log, DUK_VARARGS, "log");
    duk_put_prop_string(ctx, -2, "console");

    // Capability sandbox: no module loader and no Duktape internals.
    duk_del_prop_string(ctx, -1, "require");
    duk_del_prop_string(ctx, -1, "Duktape");
    duk_del_prop_string(ctx, -1, "ModSearch");
    duk_del_prop_string(ctx, -1, "eval");
    duk_del_prop_string(ctx, -1, "Function");
    duk_pop(ctx);
}

#endif // NEXUS_ENABLE_SCRIPTING

ScriptEngine::ScriptEngine(EventBus& events, ContentRegistry& registry, HookManager& hooks,
                           ScriptOptions options)
    : impl_(std::make_unique<Impl>()),
      events_(events),
      registry_(registry),
      hooks_(hooks),
      options_(std::move(options)) {
    impl_->owner = this;
    impl_->events = &events_;
    impl_->registry = &registry_;
    impl_->hooks = &hooks_;
    impl_->world = &world_;
    impl_->errors = &errors_;
    impl_->max_depth = options_.max_event_depth;
}

ScriptEngine::~ScriptEngine() {
    shutdown();
}

bool ScriptEngine::initialize(std::string& error) {
#if !NEXUS_ENABLE_SCRIPTING
    error = "Duktape support was not compiled in (NEXUS_ENABLE_SCRIPTING=OFF)";
    return false;
#else
    if (impl_->ctx != nullptr) return true;
    impl_->allocator.limit = options_.memory_limit;
    impl_->allocator.used = 0;
    impl_->allocator.exhausted = false;
    impl_->ctx = duk_create_heap(limited_alloc, limited_realloc, limited_free,
                                 &impl_->allocator, fatal_handler);
    if (impl_->ctx == nullptr) {
        error = impl_->allocator.exhausted
                    ? "script heap limit exceeded while creating the Duktape heap"
                    : "failed to create the Duktape heap";
        return false;
    }
    duk_push_global_stash(impl_->ctx);
    duk_push_pointer(impl_->ctx, impl_.get());
    duk_put_prop_string(impl_->ctx, -2, "\xffnexus_impl");
    duk_pop(impl_->ctx);
    push_handlers_array(impl_->ctx);
    duk_pop(impl_->ctx);
    impl_->register_api();
    impl_->healthy = true;
    healthy_ = true;
    return true;
#endif
}

void ScriptEngine::shutdown() {
#if NEXUS_ENABLE_SCRIPTING
    if (impl_ == nullptr) return;
    for (const auto& subscription : impl_->subscriptions) {
        events_.unsubscribe(subscription.second);
    }
    impl_->subscriptions.clear();
    for (HookId id : impl_->hook_ids) {
        hooks_.remove(id);
    }
    impl_->hook_ids.clear();
    impl_->handler_hooks.clear();
    impl_->handlers.clear();
    if (impl_->ctx != nullptr) {
        duk_destroy_heap(impl_->ctx);
        impl_->ctx = nullptr;
    }
    impl_->healthy = false;
#endif
    healthy_ = false;
}

bool ScriptEngine::initialized() const {
#if NEXUS_ENABLE_SCRIPTING
    return impl_ != nullptr && impl_->ctx != nullptr;
#else
    return false;
#endif
}

bool ScriptEngine::eval(const std::string& source, const std::string& name, std::string& error) {
#if !NEXUS_ENABLE_SCRIPTING
    (void)source; (void)name;
    error = "Duktape support was not compiled in";
    return false;
#else
    if (impl_->ctx == nullptr) {
        error = "script engine is not initialized";
        return false;
    }
    const std::string wrapped = "// " + name + "\n" + source;
    if (duk_peval_lstring(impl_->ctx, wrapped.data(), wrapped.size()) != 0) {
        error = duk_safe_to_string(impl_->ctx, -1);
        duk_pop(impl_->ctx);
        errors_.push_back(error);
        return false;
    }
    duk_pop(impl_->ctx);
    return true;
#endif
}

bool ScriptEngine::eval_file(const std::string& path, std::string& error) {
    std::string source;
    if (!fs::read_file(path, source, 32ull * 1024 * 1024)) {
        error = "cannot read script: " + path;
        return false;
    }
    return eval(source, path, error);
}

bool ScriptEngine::has_function(const std::string& name) const {
#if NEXUS_ENABLE_SCRIPTING
    if (impl_->ctx == nullptr) return false;
    if (!duk_get_global_string(impl_->ctx, name.c_str())) {
        duk_pop(impl_->ctx);
        return false;
    }
    const bool result = duk_is_function(impl_->ctx, -1) != 0;
    duk_pop(impl_->ctx);
    return result;
#else
    (void)name;
    return false;
#endif
}

bool ScriptEngine::call_function(const std::string& name, const nlohmann::json& argument,
                                 nlohmann::json& result, std::string& error) {
#if !NEXUS_ENABLE_SCRIPTING
    (void)name; (void)argument; (void)result;
    error = "Duktape support was not compiled in";
    return false;
#else
    if (impl_->ctx == nullptr) {
        error = "script engine is not initialized";
        return false;
    }
    if (!duk_get_global_string(impl_->ctx, name.c_str()) ||
        !duk_is_function(impl_->ctx, -1)) {
        duk_pop(impl_->ctx);
        error = "script function not found: " + name;
        return false;
    }
    const std::string text = argument.dump();
    duk_push_lstring(impl_->ctx, text.data(), text.size());
    if (argument.is_object() || argument.is_array()) {
        duk_json_decode(impl_->ctx, -1);
    }
    if (duk_pcall(impl_->ctx, 1) != 0) {
        error = duk_safe_to_string(impl_->ctx, -1);
        duk_pop(impl_->ctx);
        errors_.push_back(error);
        return false;
    }
    if (duk_is_object(impl_->ctx, -1)) {
        const char* encoded = duk_json_encode(impl_->ctx, -1);
        try {
            result = nlohmann::json::parse(encoded != nullptr ? encoded : "{}");
        } catch (const std::exception& ex) {
            error = std::string("invalid script result: ") + ex.what();
            duk_pop(impl_->ctx);
            return false;
        }
    } else if (duk_is_number(impl_->ctx, -1)) {
        result = duk_get_number(impl_->ctx, -1);
    } else if (duk_is_boolean(impl_->ctx, -1)) {
        result = duk_get_boolean(impl_->ctx, -1) != 0;
    } else if (duk_is_string(impl_->ctx, -1)) {
        result = duk_get_string(impl_->ctx, -1);
    }
    duk_pop(impl_->ctx);
    return true;
#endif
}

bool ScriptEngine::dispatch_event(const std::string& name, nlohmann::json& payload,
                                  std::string& error) {
#if NEXUS_ENABLE_SCRIPTING
    if (impl_->ctx == nullptr) return true;
    return impl_->dispatch_event(name, payload, error);
#else
    (void)name; (void)payload; (void)error;
    return true;
#endif
}

bool ScriptEngine::dispatch_hook(HookContext& context, std::string& error) {
#if NEXUS_ENABLE_SCRIPTING
    (void)context;
    (void)error;
    return true;
#else
    (void)context; (void)error;
    return true;
#endif
}

size_t ScriptEngine::event_handler_count() const {
#if NEXUS_ENABLE_SCRIPTING
    size_t count = 0;
    for (const auto& entry : impl_->handlers) count += entry.second.size();
    return count;
#else
    return 0;
#endif
}

size_t ScriptEngine::hook_count() const {
#if NEXUS_ENABLE_SCRIPTING
    return impl_->hook_ids.size();
#else
    return 0;
#endif
}

} // namespace nexus::mod
