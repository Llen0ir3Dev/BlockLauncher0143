#pragma once

#include <cstdint>

#include <nexus/worldgen/biome.hpp>
#include <nexus/worldgen/chunk.hpp>

namespace nexus::worldgen {

struct StructureSettings {
    bool trees = true;
    double forest_tree_chance = 0.16;
    double plains_tree_chance = 0.012;
    double taiga_tree_chance = 0.10;
    double savanna_tree_chance = 0.012;
    double swamp_tree_chance = 0.035;
    double snowy_tree_chance = 0.06;
    bool cacti = true;
    double desert_cactus_chance = 0.012;
    bool snow_layers = true;
};

// Generates small deterministic surface structures.  Trees that would cross a
// chunk boundary are clipped; this is documented behaviour, not an attempt to
// silently claim cross-chunk structure consistency.
class StructureGenerator {
public:
    StructureGenerator(uint64_t seed, const StructureSettings& settings = {});

    const StructureSettings& settings() const { return settings_; }
    void generate(Chunk& chunk, const BiomeRegistry& registry) const;

private:
    void place_tree(Chunk& chunk, int x, int z, int ground_y, BiomeType biome, uint64_t hash) const;
    void place_cactus(Chunk& chunk, int x, int z, int ground_y, uint64_t hash) const;

    StructureSettings settings_;
    uint64_t seed_;
};

} // namespace nexus::worldgen
