#pragma once

#include <cstdint>
#include <functional>

#include <nexus/worldgen/biome.hpp>
#include <nexus/worldgen/chunk.hpp>
#include <nexus/worldgen/noise.hpp>

namespace nexus::worldgen {

struct TerrainSettings {
    uint64_t seed = 0;
    int sea_level = 62;
    int base_height = 64;
    double continent_amplitude = 26.0;
    double hill_amplitude = 9.0;
    double mountain_amplitude = 42.0;
    double continent_scale = 0.0055;
    double detail_scale = 0.032;
    double ridge_scale = 0.0038;
    int octaves = 4;
    int min_height = 3;
    int max_height = kChunkHeight - 3;
    bool generate_beaches = true;
    bool generate_rivers = true;
};

// Produces the base heightmap, climate and solid terrain.  Caves, ores and
// structures are applied by their own generators afterwards.
class TerrainGenerator {
public:
    TerrainGenerator(uint64_t seed, const TerrainSettings& settings);

    const TerrainSettings& settings() const { return settings_; }
    int height_at(int world_x, int world_z) const;
    float temperature_at(int world_x, int world_z) const;
    float humidity_at(int world_x, int world_z) const;
    BiomeType biome_at(int world_x, int world_z, int height, const BiomeRegistry& registry) const;

    void fill(Chunk& chunk, const BiomeRegistry& registry,
              const std::function<int(int, int, int)>& height_modifier = {}) const;

private:
    TerrainSettings settings_;
    ValueNoise2D continent_;
    ValueNoise2D hills_;
    ValueNoise2D ridge_;
    ValueNoise2D temperature_;
    ValueNoise2D humidity_;
    ValueNoise2D river_;
};

} // namespace nexus::worldgen
