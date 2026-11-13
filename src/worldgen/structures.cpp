#include <nexus/worldgen/structures.hpp>

#include <algorithm>

#include <nexus/worldgen/noise.hpp>

namespace nexus::worldgen {

StructureGenerator::StructureGenerator(uint64_t seed, const StructureSettings& settings)
    : settings_(settings), seed_(seed) {}

void StructureGenerator::place_tree(Chunk& chunk, int x, int z, int ground_y,
                                    BiomeType biome, uint64_t hash) const {
    Rng rng(hash);
    const bool spruce = biome == BiomeType::Taiga || biome == BiomeType::Snowy;
    const int trunk_height = spruce ? rng.range(6, 9) : rng.range(4, 6);
    const int top = ground_y + trunk_height;
    if (top + 2 >= kChunkHeight) return;

    for (int y = ground_y + 1; y <= top; ++y) {
        if (Chunk::in_bounds(x, y, z)) chunk.set(x, y, z, BlockId::Log);
    }

    if (spruce) {
        for (int layer = 2; layer <= trunk_height + 1; ++layer) {
            const int radius = std::max(1, (trunk_height + 1 - layer) / 2 + 1);
            const int y = ground_y + layer;
            for (int dz = -radius; dz <= radius; ++dz) {
                for (int dx = -radius; dx <= radius; ++dx) {
                    if (std::abs(dx) + std::abs(dz) > radius + (layer % 2)) continue;
                    if (Chunk::in_bounds(x + dx, y, z + dz) &&
                        chunk.get(x + dx, y, z + dz) == BlockId::Air) {
                        chunk.set(x + dx, y, z + dz, BlockId::Leaves);
                    }
                }
            }
        }
        if (Chunk::in_bounds(x, top + 1, z)) chunk.set(x, top + 1, z, BlockId::Leaves);
    } else {
        const int canopy_base = top - 1;
        for (int y = canopy_base; y <= top + 1; ++y) {
            const int radius = (y >= top) ? 1 : 2;
            for (int dz = -radius; dz <= radius; ++dz) {
                for (int dx = -radius; dx <= radius; ++dx) {
                    if (std::abs(dx) == radius && std::abs(dz) == radius && y == top + 1) continue;
                    if (Chunk::in_bounds(x + dx, y, z + dz) &&
                        chunk.get(x + dx, y, z + dz) == BlockId::Air) {
                        chunk.set(x + dx, y, z + dz, BlockId::Leaves);
                    }
                }
            }
        }
    }
}

void StructureGenerator::place_cactus(Chunk& chunk, int x, int z, int ground_y, uint64_t hash) const {
    Rng rng(hash);
    const int height = rng.range(2, 4);
    for (int y = ground_y + 1; y <= ground_y + height; ++y) {
        if (Chunk::in_bounds(x, y, z)) chunk.set(x, y, z, BlockId::Cactus);
    }
}

void StructureGenerator::generate(Chunk& chunk, const BiomeRegistry& registry) const {
    (void)registry;
    const int origin_x = chunk.coord.x * kChunkSize;
    const int origin_z = chunk.coord.z * kChunkSize;
    for (int z = 2; z < kChunkSize - 2; ++z) {
        for (int x = 2; x < kChunkSize - 2; ++x) {
            const size_t column = static_cast<size_t>(z) * kChunkSize + x;
            const int height = chunk.height[column];
            const BiomeType biome = static_cast<BiomeType>(chunk.biome[column]);
            if (height < 1 || height >= kChunkHeight - 12) continue;
            if (chunk.get(x, height, z) == BlockId::Water ||
                chunk.get(x, height + 1, z) != BlockId::Air) {
                continue;
            }
            const int world_x = origin_x + x;
            const int world_z = origin_z + z;
            const uint64_t hash = hash_position(world_x, world_z, hash_combine(seed_, 0x4001));
            Rng rng(hash);
            double chance = 0.0;
            switch (biome) {
                case BiomeType::Forest: chance = settings_.forest_tree_chance; break;
                case BiomeType::Plains: chance = settings_.plains_tree_chance; break;
                case BiomeType::Taiga: chance = settings_.taiga_tree_chance; break;
                case BiomeType::Savanna: chance = settings_.savanna_tree_chance; break;
                case BiomeType::Swamp: chance = settings_.swamp_tree_chance; break;
                case BiomeType::Snowy: chance = settings_.snowy_tree_chance; break;
                default: chance = 0.0; break;
            }
            if (settings_.trees && chance > 0.0 && rng.chance(chance)) {
                place_tree(chunk, x, z, height, biome, hash);
                continue;
            }
            if (settings_.cacti && biome == BiomeType::Desert &&
                rng.chance(settings_.desert_cactus_chance)) {
                place_cactus(chunk, x, z, height, hash);
            }
            if (settings_.snow_layers && biome == BiomeType::Snowy &&
                chunk.get(x, height, z) == BlockId::Snow) {
                if (chunk.get(x, height + 1, z) == BlockId::Air &&
                    Chunk::in_bounds(x, height + 1, z)) {
                    chunk.set(x, height + 1, z, BlockId::SnowLayer);
                }
            }
        }
    }
}

} // namespace nexus::worldgen
