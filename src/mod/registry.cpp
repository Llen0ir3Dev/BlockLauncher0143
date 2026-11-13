#include <nexus/mod/registry.hpp>

#include <algorithm>
#include <cctype>
#include <set>

#include <nexus/core/strings.hpp>

namespace nexus::mod {

namespace {

bool valid_name(const std::string& name) {
    if (name.empty() || name.size() > 64) return false;
    for (char c : name) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (!std::isalnum(uc) && c != '_' && c != '-' && c != '.' && c != ':') return false;
    }
    return true;
}

bool valid_texture(const std::string& texture) {
    if (texture.empty() || texture.size() > 256) return false;
    if (texture.find("..") != std::string::npos) return false;
    if (texture.front() == '/' || texture.front() == '\\') return false;
    if (texture.find('\\') != std::string::npos) return false;
    return true;
}

std::string string_field(const nlohmann::json& json, const char* key,
                         const std::string& fallback = "") {
    const auto it = json.find(key);
    if (it == json.end() || !it->is_string()) return fallback;
    return it->get<std::string>();
}

bool bool_field(const nlohmann::json& json, const char* key, bool fallback) {
    const auto it = json.find(key);
    if (it == json.end() || !it->is_boolean()) return fallback;
    return it->get<bool>();
}

float number_field(const nlohmann::json& json, const char* key, float fallback) {
    const auto it = json.find(key);
    if (it == json.end() || !it->is_number()) return fallback;
    return it->get<float>();
}

int int_field(const nlohmann::json& json, const char* key, int fallback) {
    const auto it = json.find(key);
    if (it == json.end() || !it->is_number_integer()) return fallback;
    return it->get<int>();
}

} // namespace

int ContentRegistry::register_block(const nlohmann::json& definition,
                                    const std::string& mod_id, std::string& error) {
    if (!definition.is_object()) {
        error = "block definition must be a JSON object";
        return -1;
    }
    BlockDefinition block;
    block.name = string_field(definition, "name");
    if (!valid_name(block.name)) {
        error = "block name is missing or invalid (expected [A-Za-z0-9_.:-]{1,64})";
        return -1;
    }
    if (find_block(block.name) != nullptr) {
        error = "block '" + block.name + "' is already registered";
        return -1;
    }
    block.texture = string_field(definition, "texture", "missing");
    if (!valid_texture(block.texture)) {
        error = "block texture path is invalid: " + block.texture;
        return -1;
    }
    const int requested = int_field(definition, "id", 0);
    if (requested != 0) {
        if (requested < kFirstCustomBlockId || requested > kMaxBlockId) {
            error = "block id " + std::to_string(requested) + " is outside the custom range " +
                    std::to_string(kFirstCustomBlockId) + ".." + std::to_string(kMaxBlockId);
            return -1;
        }
        if (find_block(static_cast<uint16_t>(requested)) != nullptr) {
            error = "block id " + std::to_string(requested) + " is already in use";
            return -1;
        }
        block.id = static_cast<uint16_t>(requested);
    } else {
        uint16_t candidate = kFirstCustomBlockId;
        while (candidate <= kMaxBlockId && find_block(candidate) != nullptr) ++candidate;
        if (candidate > kMaxBlockId) {
            error = "no free custom block ids remain";
            return -1;
        }
        block.id = candidate;
    }
    block.solid = bool_field(definition, "solid", true);
    block.opaque = bool_field(definition, "opaque", true);
    block.liquid = bool_field(definition, "liquid", false);
    block.hardness = number_field(definition, "hardness", 1.0f);
    block.light_emission = number_field(definition, "lightEmission", 0.0f);
    if (block.hardness < 0.0f || block.light_emission < 0.0f || block.light_emission > 1.0f) {
        error = "block hardness/lightEmission is out of range";
        return -1;
    }
    block.mod_id = mod_id;
    blocks_.push_back(block);
    return block.id;
}

int ContentRegistry::register_item(const nlohmann::json& definition,
                                   const std::string& mod_id, std::string& error) {
    if (!definition.is_object()) {
        error = "item definition must be a JSON object";
        return -1;
    }
    ItemDefinition item;
    item.name = string_field(definition, "name");
    if (!valid_name(item.name)) {
        error = "item name is missing or invalid";
        return -1;
    }
    if (find_item(item.name) != nullptr) {
        error = "item '" + item.name + "' is already registered";
        return -1;
    }
    item.texture = string_field(definition, "texture", "missing");
    if (!valid_texture(item.texture)) {
        error = "item texture path is invalid: " + item.texture;
        return -1;
    }
    item.max_stack = int_field(definition, "maxStack", 64);
    if (item.max_stack < 1 || item.max_stack > 64) {
        error = "item maxStack must be between 1 and 64";
        return -1;
    }
    const int requested = int_field(definition, "id", 0);
    if (requested != 0) {
        if (requested < kFirstCustomItemId || requested > kMaxItemId) {
            error = "item id " + std::to_string(requested) + " is outside the custom range";
            return -1;
        }
        for (const auto& existing : items_) {
            if (existing.id == requested) {
                error = "item id " + std::to_string(requested) + " is already in use";
                return -1;
            }
        }
        item.id = static_cast<uint16_t>(requested);
    } else {
        uint16_t candidate = kFirstCustomItemId;
        while (candidate <= kMaxItemId) {
            const bool used = std::any_of(items_.begin(), items_.end(),
                                          [&](const ItemDefinition& existing) {
                                              return existing.id == candidate;
                                          });
            if (!used) break;
            ++candidate;
        }
        if (candidate > kMaxItemId) {
            error = "no free custom item ids remain";
            return -1;
        }
        item.id = candidate;
    }
    item.mod_id = mod_id;
    items_.push_back(item);
    return item.id;
}

int ContentRegistry::register_entity(const nlohmann::json& definition,
                                     const std::string& mod_id, std::string& error) {
    if (!definition.is_object()) {
        error = "entity definition must be a JSON object";
        return -1;
    }
    EntityDefinition entity;
    entity.name = string_field(definition, "name");
    if (!valid_name(entity.name)) {
        error = "entity name is missing or invalid";
        return -1;
    }
    if (find_entity(entity.name) != nullptr) {
        error = "entity '" + entity.name + "' is already registered";
        return -1;
    }
    entity.model = string_field(definition, "model", "missing");
    entity.texture = string_field(definition, "texture", "missing");
    if (!valid_texture(entity.model) || !valid_texture(entity.texture)) {
        error = "entity model/texture path is invalid";
        return -1;
    }
    const int requested = int_field(definition, "id", 0);
    if (requested < 0) {
        error = "entity id must not be negative";
        return -1;
    }
    if (requested != 0) {
        for (const auto& existing : entities_) {
            if (existing.id == static_cast<uint32_t>(requested)) {
                error = "entity id " + std::to_string(requested) + " is already in use";
                return -1;
            }
        }
        entity.id = static_cast<uint32_t>(requested);
    } else {
        uint32_t candidate = 1;
        bool used = true;
        while (used) {
            used = false;
            for (const auto& existing : entities_) {
                if (existing.id == candidate) {
                    used = true;
                    break;
                }
            }
            if (used) ++candidate;
        }
        entity.id = candidate;
    }
    entity.width = number_field(definition, "width", 0.6f);
    entity.height = number_field(definition, "height", 1.8f);
    if (entity.width <= 0.0f || entity.height <= 0.0f) {
        error = "entity dimensions must be positive";
        return -1;
    }
    entity.mod_id = mod_id;
    entities_.push_back(entity);
    return static_cast<int>(entity.id);
}

const BlockDefinition* ContentRegistry::find_block(const std::string& name) const {
    for (const auto& block : blocks_) {
        if (block.name == name) return &block;
    }
    return nullptr;
}

const BlockDefinition* ContentRegistry::find_block(uint16_t id) const {
    for (const auto& block : blocks_) {
        if (block.id == id) return &block;
    }
    return nullptr;
}

const ItemDefinition* ContentRegistry::find_item(const std::string& name) const {
    for (const auto& item : items_) {
        if (item.name == name) return &item;
    }
    return nullptr;
}

const EntityDefinition* ContentRegistry::find_entity(const std::string& name) const {
    for (const auto& entity : entities_) {
        if (entity.name == name) return &entity;
    }
    return nullptr;
}

size_t ContentRegistry::remove_mod(const std::string& mod_id) {
    const size_t before = blocks_.size() + items_.size() + entities_.size();
    blocks_.erase(std::remove_if(blocks_.begin(), blocks_.end(),
                                 [&](const BlockDefinition& entry) {
                                     return entry.mod_id == mod_id;
                                 }),
                  blocks_.end());
    items_.erase(std::remove_if(items_.begin(), items_.end(),
                                [&](const ItemDefinition& entry) {
                                    return entry.mod_id == mod_id;
                                }),
                 items_.end());
    entities_.erase(std::remove_if(entities_.begin(), entities_.end(),
                                   [&](const EntityDefinition& entry) {
                                       return entry.mod_id == mod_id;
                                   }),
                    entities_.end());
    return before - (blocks_.size() + items_.size() + entities_.size());
}

void ContentRegistry::clear() {
    blocks_.clear();
    items_.clear();
    entities_.clear();
}

nlohmann::json ContentRegistry::to_json() const {
    nlohmann::json out;
    out["blocks"] = nlohmann::json::array();
    out["items"] = nlohmann::json::array();
    out["entities"] = nlohmann::json::array();
    for (const auto& block : blocks_) {
        out["blocks"].push_back({
            {"id", block.id}, {"name", block.name}, {"texture", block.texture},
            {"solid", block.solid}, {"opaque", block.opaque}, {"liquid", block.liquid},
            {"hardness", block.hardness}, {"lightEmission", block.light_emission},
            {"mod", block.mod_id}
        });
    }
    for (const auto& item : items_) {
        out["items"].push_back({
            {"id", item.id}, {"name", item.name}, {"texture", item.texture},
            {"maxStack", item.max_stack}, {"mod", item.mod_id}
        });
    }
    for (const auto& entity : entities_) {
        out["entities"].push_back({
            {"id", entity.id}, {"name", entity.name}, {"model", entity.model},
            {"texture", entity.texture}, {"width", entity.width},
            {"height", entity.height}, {"mod", entity.mod_id}
        });
    }
    return out;
}

} // namespace nexus::mod
