#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <nexus/worldgen/blocks.hpp>
#include <nexus/worldgen/chunk.hpp>

namespace nexus::worldgen {

enum class BiomeType : uint8_t {
    Ocean = 0,
    Beach,
    Plains,
    Forest,
    Taiga,
    Desert,
    Savanna,
    Mountains,
    Snowy,
    Swamp,
    River,
    Custom
};

const char* biome_name(BiomeType type);
bool parse_biome(const std::string& name, BiomeType& out);

struct BiomeDefinition {
    BiomeType type = BiomeType::Plains;
    std::string name = "plains";
    BlockId surface = BlockId::Grass;
    BlockId subsurface = BlockId::Dirt;
    BlockId filler = BlockId::Stone;
    BlockId underwater_surface = BlockId::Sand;
    float temperature = 0.5f;
    float humidity = 0.5f;
    int min_height = 0;
    int max_height = kChunkHeight - 1;
    bool snow = false;
    bool custom = false;
};

// Deterministic selection from climate and altitude.  Custom definitions are
// considered before the built-in defaults so extensions can override biomes.
class BiomeRegistry {
public:
    BiomeRegistry();

    void reset_to_defaults();
    bool register_biome(const BiomeDefinition& definition);
    bool unregister_biome(const std::string& name);
    const BiomeDefinition* find(BiomeType type) const;
    const BiomeDefinition* find(const std::string& name) const;
    const BiomeDefinition& require(BiomeType type) const;

    BiomeType select(float temperature, float humidity, int height, int sea_level) const;
    const std::vector<BiomeDefinition>& all() const { return biomes_; }

private:
    std::vector<BiomeDefinition> biomes_;
};

} // namespace nexus::worldgen
