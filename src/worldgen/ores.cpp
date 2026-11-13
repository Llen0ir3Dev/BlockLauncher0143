#include <nexus/worldgen/ores.hpp>

#include <algorithm>

namespace nexus::worldgen {

std::vector<OreDefinition> default_ore_definitions() {
    std::vector<OreDefinition> ores;
    ores.push_back({"coal", BlockId::CoalOre, 20, 5, 120, 10, BlockId::Stone, true});
    ores.push_back({"iron", BlockId::IronOre, 14, 3, 96, 7, BlockId::Stone, true});
    ores.push_back({"gold", BlockId::GoldOre, 4, 2, 36, 6, BlockId::Stone, true});
    ores.push_back({"redstone", BlockId::RedstoneOre, 6, 1, 24, 6, BlockId::Stone, true});
    ores.push_back({"lapis", BlockId::LapisOre, 2, 2, 32, 5, BlockId::Stone, true});
    ores.push_back({"diamond", BlockId::DiamondOre, 2, 1, 16, 5, BlockId::Stone, true});
    ores.push_back({"gravel", BlockId::Gravel, 6, 8, 110, 14, BlockId::Stone, true});
    return ores;
}

OreGenerator::OreGenerator(uint64_t seed) : seed_(seed) {}

void OreGenerator::generate(Chunk& chunk, const std::vector<OreDefinition>& ores) const {
    const uint64_t chunk_seed = hash_position(chunk.coord.x, chunk.coord.z, hash_combine(seed_, 0x3001));
    for (const auto& ore : ores) {
        if (!ore.enabled || ore.veins_per_chunk <= 0 || ore.vein_size <= 0) continue;
        Rng rng(hash_combine(chunk_seed, static_cast<uint64_t>(ore.block)));
        const int min_y = std::max(1, std::min(ore.min_y, ore.max_y));
        const int max_y = std::min(kChunkHeight - 2, std::max(ore.min_y, ore.max_y));
        if (max_y < min_y) continue;
        for (int vein = 0; vein < ore.veins_per_chunk; ++vein) {
            int x = rng.range(1, kChunkSize - 2);
            int z = rng.range(1, kChunkSize - 2);
            int y = rng.range(min_y, max_y);
            const int size = std::max(1, ore.vein_size + rng.range(-2, 2));
            for (int step = 0; step < size; ++step) {
                if (Chunk::in_bounds(x, y, z) && chunk.get(x, y, z) == ore.replace) {
                    chunk.set(x, y, z, ore.block);
                }
                // Short random walk produces a vein instead of a perfect blob.
                const int direction = rng.next_int(6);
                if (direction == 0) ++x;
                else if (direction == 1) --x;
                else if (direction == 2) ++y;
                else if (direction == 3) --y;
                else if (direction == 4) ++z;
                else --z;
                x = std::max(1, std::min(kChunkSize - 2, x));
                z = std::max(1, std::min(kChunkSize - 2, z));
                y = std::max(min_y, std::min(max_y, y));
            }
        }
    }
}

} // namespace nexus::worldgen
