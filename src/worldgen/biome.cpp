#include <nexus/worldgen/biome.hpp>

#include <algorithm>
#include <cctype>

#include <nexus/core/strings.hpp>

namespace nexus::worldgen {

const char* biome_name(BiomeType type) {
    switch (type) {
        case BiomeType::Ocean: return "ocean";
        case BiomeType::Beach: return "beach";
        case BiomeType::Plains: return "plains";
        case BiomeType::Forest: return "forest";
        case BiomeType::Taiga: return "taiga";
        case BiomeType::Desert: return "desert";
        case BiomeType::Savanna: return "savanna";
        case BiomeType::Mountains: return "mountains";
        case BiomeType::Snowy: return "snowy";
        case BiomeType::Swamp: return "swamp";
        case BiomeType::River: return "river";
        case BiomeType::Custom: return "custom";
    }
    return "unknown";
}

bool parse_biome(const std::string& name, BiomeType& out) {
    const std::string lower = to_lower(trim(name));
    static const BiomeType types[] = {
        BiomeType::Ocean, BiomeType::Beach, BiomeType::Plains, BiomeType::Forest,
        BiomeType::Taiga, BiomeType::Desert, BiomeType::Savanna, BiomeType::Mountains,
        BiomeType::Snowy, BiomeType::Swamp, BiomeType::River, BiomeType::Custom
    };
    for (BiomeType type : types) {
        if (lower == biome_name(type)) {
            out = type;
            return true;
        }
    }
    return false;
}

BiomeRegistry::BiomeRegistry() { reset_to_defaults(); }

void BiomeRegistry::reset_to_defaults() {
    biomes_.clear();
    auto add = [&](BiomeType type, const char* name, BlockId surface, BlockId subsurface,
                   BlockId underwater, float temperature, float humidity, bool snow) {
        BiomeDefinition def;
        def.type = type;
        def.name = name;
        def.surface = surface;
        def.subsurface = subsurface;
        def.underwater_surface = underwater;
        def.temperature = temperature;
        def.humidity = humidity;
        def.snow = snow;
        biomes_.push_back(def);
    };
    add(BiomeType::Ocean, "ocean", BlockId::Gravel, BlockId::Sand, BlockId::Sand, 0.4f, 0.6f, false);
    add(BiomeType::Beach, "beach", BlockId::Sand, BlockId::Sand, BlockId::Sand, 0.7f, 0.4f, false);
    add(BiomeType::Plains, "plains", BlockId::Grass, BlockId::Dirt, BlockId::Sand, 0.6f, 0.4f, false);
    add(BiomeType::Forest, "forest", BlockId::Grass, BlockId::Dirt, BlockId::Sand, 0.6f, 0.7f, false);
    add(BiomeType::Taiga, "taiga", BlockId::Grass, BlockId::Dirt, BlockId::Gravel, 0.25f, 0.6f, false);
    add(BiomeType::Desert, "desert", BlockId::Sand, BlockId::Sandstone, BlockId::Sand, 0.9f, 0.1f, false);
    add(BiomeType::Savanna, "savanna", BlockId::Grass, BlockId::Dirt, BlockId::Sand, 0.8f, 0.3f, false);
    add(BiomeType::Mountains, "mountains", BlockId::Stone, BlockId::Stone, BlockId::Gravel, 0.4f, 0.4f, false);
    add(BiomeType::Snowy, "snowy", BlockId::Grass, BlockId::Dirt, BlockId::Gravel, 0.05f, 0.5f, true);
    add(BiomeType::Swamp, "swamp", BlockId::Grass, BlockId::Dirt, BlockId::Clay, 0.7f, 0.95f, false);
    add(BiomeType::River, "river", BlockId::Sand, BlockId::Sand, BlockId::Sand, 0.5f, 0.6f, false);
}

bool BiomeRegistry::register_biome(const BiomeDefinition& definition) {
    if (definition.name.empty()) return false;
    for (auto& existing : biomes_) {
        if (existing.name == definition.name || existing.type == definition.type) {
            existing = definition;
            existing.custom = true;
            return true;
        }
    }
    BiomeDefinition added = definition;
    added.custom = true;
    biomes_.push_back(added);
    return true;
}

bool BiomeRegistry::unregister_biome(const std::string& name) {
    for (auto it = biomes_.begin(); it != biomes_.end(); ++it) {
        if (it->name == name && it->custom) {
            biomes_.erase(it);
            return true;
        }
    }
    return false;
}

const BiomeDefinition* BiomeRegistry::find(BiomeType type) const {
    for (const auto& biome : biomes_) {
        if (biome.type == type) return &biome;
    }
    return nullptr;
}

const BiomeDefinition* BiomeRegistry::find(const std::string& name) const {
    for (const auto& biome : biomes_) {
        if (biome.name == name) return &biome;
    }
    return nullptr;
}

const BiomeDefinition& BiomeRegistry::require(BiomeType type) const {
    const BiomeDefinition* found = find(type);
    if (found != nullptr) return *found;
    static const BiomeDefinition fallback;
    return fallback;
}

BiomeType BiomeRegistry::select(float temperature, float humidity, int height, int sea_level) const {
    if (height < sea_level - 6) return BiomeType::Ocean;
    if (height <= sea_level + 1) {
        if (temperature < 0.2f) return BiomeType::Snowy;
        return BiomeType::Beach;
    }
    if (height > sea_level + 38) return BiomeType::Mountains;
    if (temperature < 0.2f) return BiomeType::Snowy;
    if (temperature < 0.38f) return humidity > 0.5f ? BiomeType::Taiga : BiomeType::Plains;
    if (temperature > 0.78f) {
        if (humidity < 0.25f) return BiomeType::Desert;
        return BiomeType::Savanna;
    }
    if (humidity > 0.82f && height <= sea_level + 8) return BiomeType::Swamp;
    if (humidity > 0.58f) return BiomeType::Forest;
    return BiomeType::Plains;
}

} // namespace nexus::worldgen
