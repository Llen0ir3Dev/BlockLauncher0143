#pragma once

#include <cstdint>
#include <vector>

#include <nexus/worldgen/blocks.hpp>

namespace nexus::worldgen {

constexpr int kChunkSize = 16;
constexpr int kChunkHeight = 128;
constexpr int kChunkVolume = kChunkSize * kChunkHeight * kChunkSize;

struct ChunkCoord {
    int x = 0;
    int z = 0;

    bool operator==(const ChunkCoord& other) const { return x == other.x && z == other.z; }
    bool operator!=(const ChunkCoord& other) const { return !(*this == other); }
};

struct Chunk {
    ChunkCoord coord;
    std::vector<BlockId> blocks;
    // Per-column terrain data.  height is the highest solid terrain block
    // before caves are carved; surface_height is the final top block.
    std::vector<int16_t> height;
    std::vector<int16_t> surface_height;
    std::vector<uint8_t> biome;
    bool generated = false;

    Chunk() : blocks(kChunkVolume, BlockId::Air), height(kChunkSize * kChunkSize, 0),
              surface_height(kChunkSize * kChunkSize, 0),
              biome(kChunkSize * kChunkSize, 0) {}

    static constexpr size_t index(int x, int y, int z) {
        return (static_cast<size_t>(y) * kChunkSize + z) * kChunkSize + x;
    }
    static bool in_bounds(int x, int y, int z) {
        return x >= 0 && x < kChunkSize && y >= 0 && y < kChunkHeight && z >= 0 && z < kChunkSize;
    }

    BlockId get(int x, int y, int z) const {
        if (!in_bounds(x, y, z)) return BlockId::Air;
        return blocks[index(x, y, z)];
    }
    bool set(int x, int y, int z, BlockId id) {
        if (!in_bounds(x, y, z)) return false;
        blocks[index(x, y, z)] = id;
        return true;
    }
    int column_height(int x, int z) const {
        if (x < 0 || x >= kChunkSize || z < 0 || z >= kChunkSize) return 0;
        return surface_height[static_cast<size_t>(z) * kChunkSize + x];
    }
    int top_solid(int x, int z) const {
        if (x < 0 || x >= kChunkSize || z < 0 || z >= kChunkSize) return -1;
        for (int y = kChunkHeight - 1; y >= 0; --y) {
            const BlockId id = get(x, y, z);
            if (id != BlockId::Air && !block_is_liquid(id) && !block_is_plant(id)) return y;
        }
        return -1;
    }
    int count(BlockId id) const;
};

} // namespace nexus::worldgen
