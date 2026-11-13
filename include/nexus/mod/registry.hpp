#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace nexus::mod {

struct BlockDefinition {
    uint16_t id = 0;
    std::string name;
    std::string texture = "missing";
    bool solid = true;
    bool opaque = true;
    bool liquid = false;
    bool custom = true;
    float hardness = 1.0f;
    float light_emission = 0.0f;
    std::string mod_id;
};

struct ItemDefinition {
    uint16_t id = 0;
    std::string name;
    std::string texture = "missing";
    int max_stack = 64;
    std::string mod_id;
};

struct EntityDefinition {
    uint32_t id = 0;
    std::string name;
    std::string model = "missing";
    std::string texture = "missing";
    float width = 0.6f;
    float height = 1.8f;
    std::string mod_id;
};

// Registry for mod-provided blocks, items and entities.  Registrations are
// validated before they enter the registry and every accepted entry is kept so
// that an eventual runtime can materialise it.  Ids are the actual ids a mod
// would ask the engine to use; collisions are rejected rather than silently
// overwritten.
class ContentRegistry {
public:
    // 0..255 is the 0.14 block id space; higher ids are metadata variants in
    // the original engine, so custom blocks are restricted to the free range.
    static constexpr uint16_t kFirstCustomBlockId = 200;
    static constexpr uint16_t kMaxBlockId = 255;
    static constexpr uint16_t kFirstCustomItemId = 400;
    static constexpr uint16_t kMaxItemId = 1023;

    int register_block(const nlohmann::json& definition, const std::string& mod_id,
                       std::string& error);
    int register_item(const nlohmann::json& definition, const std::string& mod_id,
                      std::string& error);
    int register_entity(const nlohmann::json& definition, const std::string& mod_id,
                        std::string& error);

    const BlockDefinition* find_block(const std::string& name) const;
    const BlockDefinition* find_block(uint16_t id) const;
    const ItemDefinition* find_item(const std::string& name) const;
    const EntityDefinition* find_entity(const std::string& name) const;

    const std::vector<BlockDefinition>& blocks() const { return blocks_; }
    const std::vector<ItemDefinition>& items() const { return items_; }
    const std::vector<EntityDefinition>& entities() const { return entities_; }

    size_t remove_mod(const std::string& mod_id);
    void clear();

    nlohmann::json to_json() const;

private:
    std::vector<BlockDefinition> blocks_;
    std::vector<ItemDefinition> items_;
    std::vector<EntityDefinition> entities_;
};

} // namespace nexus::mod
