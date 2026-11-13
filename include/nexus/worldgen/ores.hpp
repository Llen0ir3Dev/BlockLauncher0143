#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nexus/worldgen/chunk.hpp>
#include <nexus/worldgen/noise.hpp>

namespace nexus::worldgen {

struct OreDefinition {
    std::string name;
    BlockId block = BlockId::CoalOre;
    int veins_per_chunk = 20;
    int min_y = 1;
    int max_y = 120;
    int vein_size = 8;
    BlockId replace = BlockId::Stone;
    bool enabled = true;
};

std::vector<OreDefinition> default_ore_definitions();

class OreGenerator {
public:
    explicit OreGenerator(uint64_t seed);

    uint64_t seed() const { return seed_; }
    void generate(Chunk& chunk, const std::vector<OreDefinition>& ores) const;

private:
    uint64_t seed_;
};

} // namespace nexus::worldgen
