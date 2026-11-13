#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <nexus/worldgen/biome.hpp>
#include <nexus/worldgen/caves.hpp>
#include <nexus/worldgen/chunk.hpp>
#include <nexus/worldgen/extension.hpp>
#include <nexus/worldgen/ores.hpp>
#include <nexus/worldgen/structures.hpp>
#include <nexus/worldgen/terrain.hpp>

namespace nexus::worldgen {

struct WorldSettings {
    uint64_t seed = 0;
    TerrainSettings terrain;
    CaveSettings caves;
    StructureSettings structures;
    bool generate_caves = true;
    bool generate_ores = true;
    bool generate_structures = true;
    std::vector<OreDefinition> ores = default_ore_definitions();
};

struct ChunkSummary {
    ChunkCoord coord;
    int minimum_height = 0;
    int maximum_height = 0;
    int solid_blocks = 0;
    int water_blocks = 0;
    int lava_blocks = 0;
    int ore_blocks = 0;
    int cave_blocks = 0;
    int biome_count = 0;
    int dominant_biome = 0;
};

class WorldGenerator {
public:
    explicit WorldGenerator(uint64_t seed = 0, WorldSettings settings = {});

    uint64_t seed() const { return settings_.seed; }
    const WorldSettings& settings() const { return settings_; }
    WorldSettings& settings() { return settings_; }

    BiomeRegistry& biomes() { return biomes_; }
    const BiomeRegistry& biomes() const { return biomes_; }
    ExtensionRegistry& extensions() { return extensions_; }
    const ExtensionRegistry& extensions() const { return extensions_; }

    int height_at(int world_x, int world_z) const;
    BiomeType biome_at(int world_x, int world_z) const;
    Chunk generate_chunk(int chunk_x, int chunk_z);
    Chunk generate_chunk(const ChunkCoord& coord) { return generate_chunk(coord.x, coord.z); }

    // Convenience access for tools.  Reads go through the bounded chunk cache
    // below so repeated sampling of the same area is cheap.
    BlockId block_at(int world_x, int y, int world_z);
    // Persistent in-memory chunk cache used by tools, scripts and the runtime.
    // set_block mutates the cached chunk, so changes are observable by later
    // block_at / cached_chunk calls.  The cache is bounded and evicts the
    // least-recently generated chunk.
    Chunk& chunk_at(int chunk_x, int chunk_z);
    const Chunk* cached_chunk(int chunk_x, int chunk_z) const;
    bool set_block(int world_x, int y, int world_z, BlockId id);
    void clear_cache();
    size_t cached_chunks() const { return cache_.size(); }
    ChunkSummary summarize(const Chunk& chunk) const;

    const std::vector<std::string>& warnings() const { return warnings_; }

private:
    static uint64_t cache_key(int chunk_x, int chunk_z);
    void remember_chunk(int chunk_x, int chunk_z, const Chunk& chunk);

    WorldSettings settings_;
    BiomeRegistry biomes_;
    TerrainGenerator terrain_;
    CaveGenerator caves_;
    OreGenerator ores_;
    StructureGenerator structures_;
    ExtensionRegistry extensions_;
    std::vector<std::string> warnings_;
    std::map<uint64_t, Chunk> cache_;
};

} // namespace nexus::worldgen
