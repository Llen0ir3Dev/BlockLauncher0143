#include <nexus/worldgen/terrain.hpp>

#include <algorithm>
#include <cmath>

namespace nexus::worldgen {

namespace {
double clamp01(double value) { return std::max(0.0, std::min(1.0, value)); }
} // namespace

TerrainGenerator::TerrainGenerator(uint64_t seed, const TerrainSettings& settings)
    : settings_(settings),
      continent_(hash_combine(seed, 0x1001)),
      hills_(hash_combine(seed, 0x1002)),
      ridge_(hash_combine(seed, 0x1003)),
      temperature_(hash_combine(seed, 0x1004)),
      humidity_(hash_combine(seed, 0x1005)),
      river_(hash_combine(seed, 0x1006)) {
    settings_.seed = seed;
}

int TerrainGenerator::height_at(int world_x, int world_z) const {
    const double wx = static_cast<double>(world_x);
    const double wz = static_cast<double>(world_z);
    const double continent = fbm2(continent_, wx * settings_.continent_scale,
                                  wz * settings_.continent_scale, settings_.octaves, 0.52, 2.0);
    const double hills = fbm2(hills_, wx * settings_.detail_scale,
                              wz * settings_.detail_scale, settings_.octaves, 0.48, 2.1);
    const double ridge = ridged2(ridge_, wx * settings_.ridge_scale,
                                 wz * settings_.ridge_scale, 3);
    const double mountain_factor = std::max(0.0, (continent - 0.56) / 0.44);
    double height = settings_.base_height +
                    (continent - 0.5) * settings_.continent_amplitude +
                    (hills - 0.5) * settings_.hill_amplitude +
                    mountain_factor * mountain_factor * ridge * settings_.mountain_amplitude;
    if (settings_.generate_rivers) {
        const double river = std::fabs(fbm2(river_, wx * 0.0022, wz * 0.0022, 3, 0.5, 2.0) - 0.5);
        if (river < 0.018 && height > settings_.sea_level - 4) {
            const double strength = 1.0 - river / 0.018;
            height -= strength * 7.0;
        }
    }
    if (settings_.generate_beaches && height > settings_.sea_level - 1 &&
        height < settings_.sea_level + 3) {
        height = settings_.sea_level + 1;
    }
    int result = static_cast<int>(std::lround(height));
    return std::max(settings_.min_height, std::min(settings_.max_height, result));
}

float TerrainGenerator::temperature_at(int world_x, int world_z) const {
    const double value = fbm2(temperature_, world_x * 0.0018, world_z * 0.0018, 3, 0.5, 2.0);
    return static_cast<float>(clamp01(value));
}

float TerrainGenerator::humidity_at(int world_x, int world_z) const {
    const double value = fbm2(humidity_, world_x * 0.0021, world_z * 0.0021, 3, 0.5, 2.0);
    return static_cast<float>(clamp01(value));
}

BiomeType TerrainGenerator::biome_at(int world_x, int world_z, int height,
                                     const BiomeRegistry& registry) const {
    return registry.select(temperature_at(world_x, world_z),
                           humidity_at(world_x, world_z), height, settings_.sea_level);
}

void TerrainGenerator::fill(Chunk& chunk, const BiomeRegistry& registry,
                            const std::function<int(int, int, int)>& height_modifier) const {
    const int origin_x = chunk.coord.x * kChunkSize;
    const int origin_z = chunk.coord.z * kChunkSize;
    for (int z = 0; z < kChunkSize; ++z) {
        for (int x = 0; x < kChunkSize; ++x) {
            const int world_x = origin_x + x;
            const int world_z = origin_z + z;
            int height = height_at(world_x, world_z);
            if (height_modifier) {
                height = height_modifier(world_x, world_z, height);
                height = std::max(1, std::min(kChunkHeight - 12, height));
            }
            const BiomeType biome_type = biome_at(world_x, world_z, height, registry);
            const BiomeDefinition& biome = registry.require(biome_type);
            const size_t column = static_cast<size_t>(z) * kChunkSize + x;
            chunk.height[column] = static_cast<int16_t>(height);
            chunk.surface_height[column] = static_cast<int16_t>(height);
            chunk.biome[column] = static_cast<uint8_t>(biome_type);

            for (int y = 0; y <= height; ++y) {
                BlockId block = BlockId::Stone;
                if (y == 0) {
                    block = BlockId::Bedrock;
                } else if (y == height) {
                    block = height < settings_.sea_level ? biome.underwater_surface : biome.surface;
                    if (height > settings_.sea_level + 38 && biome.type != BiomeType::Snowy) {
                        block = biome.filler;
                    }
                    if (biome.snow && height >= settings_.sea_level) block = BlockId::Snow;
                } else if (y >= height - 3) {
                    block = height < settings_.sea_level ? biome.underwater_surface : biome.subsurface;
                }
                chunk.set(x, y, z, block);
            }

            if (biome.type == BiomeType::Desert) {
                for (int y = std::max(1, height - 7); y < height; ++y) {
                    chunk.set(x, y, z, BlockId::Sandstone);
                }
            }

            if (height < settings_.sea_level) {
                for (int y = height + 1; y <= settings_.sea_level; ++y) {
                    chunk.set(x, y, z, biome.snow ? BlockId::Ice : BlockId::Water);
                }
            }
        }
    }
}

} // namespace nexus::worldgen
