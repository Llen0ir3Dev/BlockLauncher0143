#include <nexus/worldgen/world.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <map>

namespace nexus::worldgen {

namespace {
int floor_div(int value, int divisor) {
    int quotient = value / divisor;
    const int remainder = value % divisor;
    if (remainder != 0 && ((remainder < 0) != (divisor < 0))) --quotient;
    return quotient;
}
} // namespace

WorldGenerator::WorldGenerator(uint64_t seed, WorldSettings settings)
    : settings_(std::move(settings)),
      terrain_(0, TerrainSettings{}),
      caves_(0, CaveSettings{}),
      ores_(0),
      structures_(0, StructureSettings{}) {
    settings_.seed = seed;
    settings_.terrain.seed = seed;
    terrain_ = TerrainGenerator(seed, settings_.terrain);
    caves_ = CaveGenerator(seed, settings_.caves);
    ores_ = OreGenerator(seed);
    structures_ = StructureGenerator(seed, settings_.structures);
}

int WorldGenerator::height_at(int world_x, int world_z) const {
    return terrain_.height_at(world_x, world_z);
}

BiomeType WorldGenerator::biome_at(int world_x, int world_z) const {
    const int height = terrain_.height_at(world_x, world_z);
    return terrain_.biome_at(world_x, world_z, height, biomes_);
}

Chunk WorldGenerator::generate_chunk(int chunk_x, int chunk_z) {
    Chunk chunk;
    chunk.coord = {chunk_x, chunk_z};
    GenerationContext context;
    context.world_seed = settings_.seed;
    context.terrain = &settings_.terrain;
    context.chunk = &chunk;
    context.chunk_origin_x = chunk_x * kChunkSize;
    context.chunk_origin_z = chunk_z * kChunkSize;

    for (const auto& extension : extensions_.extensions()) {
        extension->on_chunk_start(context);
    }

    std::function<int(int, int, int)> height_modifier;
    if (!extensions_.empty()) {
        height_modifier = [&](int world_x, int world_z, int height) {
            int modified = height;
            for (const auto& extension : extensions_.extensions()) {
                modified = extension->modify_surface_height(context, world_x, world_z, modified);
            }
            return modified;
        };
    }
    terrain_.fill(chunk, biomes_, height_modifier);

    if (settings_.generate_caves) {
        CaveSettings cave_settings = settings_.caves;
        cave_settings.enabled = true;
        caves_ = CaveGenerator(settings_.seed, cave_settings);
        caves_.carve(chunk);
    }
    if (settings_.generate_ores) {
        ores_.generate(chunk, settings_.ores);
    }
    if (settings_.generate_structures) {
        structures_ = StructureGenerator(settings_.seed, settings_.structures);
        structures_.generate(chunk, biomes_);
    }

    if (!extensions_.empty()) {
        for (int y = 0; y < kChunkHeight; ++y) {
            for (int z = 0; z < kChunkSize; ++z) {
                for (int x = 0; x < kChunkSize; ++x) {
                    BlockId block = chunk.get(x, y, z);
                    for (const auto& extension : extensions_.extensions()) {
                        block = extension->modify_block(context, x, y, z, block);
                    }
                    chunk.set(x, y, z, block);
                }
            }
        }
    }

    for (const auto& extension : extensions_.extensions()) {
        extension->on_chunk_end(context);
    }
    chunk.generated = true;
    return chunk;
}

BlockId WorldGenerator::block_at(int world_x, int y, int world_z) {
    if (y < 0 || y >= kChunkHeight) return BlockId::Air;
    const int chunk_x = floor_div(world_x, kChunkSize);
    const int chunk_z = floor_div(world_z, kChunkSize);
    const int local_x = world_x - chunk_x * kChunkSize;
    const int local_z = world_z - chunk_z * kChunkSize;
    const Chunk& chunk = chunk_at(chunk_x, chunk_z);
    return chunk.get(local_x, y, local_z);
}

uint64_t WorldGenerator::cache_key(int chunk_x, int chunk_z) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(chunk_x)) << 32) |
           static_cast<uint32_t>(chunk_z);
}

void WorldGenerator::remember_chunk(int chunk_x, int chunk_z, const Chunk& chunk) {
    // Bound the cache so scripts that probe a large area cannot grow it without
    // limit.  Eviction is oldest-generated first (std::map ordering is not
    // temporal, so erase the first entry, which is deterministic).
    constexpr size_t kMaxCachedChunks = 1024;
    if (cache_.size() >= kMaxCachedChunks) cache_.erase(cache_.begin());
    cache_[cache_key(chunk_x, chunk_z)] = chunk;
}

Chunk& WorldGenerator::chunk_at(int chunk_x, int chunk_z) {
    const uint64_t key = cache_key(chunk_x, chunk_z);
    const auto it = cache_.find(key);
    if (it != cache_.end()) return it->second;
    Chunk chunk = generate_chunk(chunk_x, chunk_z);
    remember_chunk(chunk_x, chunk_z, chunk);
    return cache_[key];
}

const Chunk* WorldGenerator::cached_chunk(int chunk_x, int chunk_z) const {
    const auto it = cache_.find(cache_key(chunk_x, chunk_z));
    return it == cache_.end() ? nullptr : &it->second;
}

bool WorldGenerator::set_block(int world_x, int y, int world_z, BlockId id) {
    if (y < 0 || y >= kChunkHeight) return false;
    const int chunk_x = floor_div(world_x, kChunkSize);
    const int chunk_z = floor_div(world_z, kChunkSize);
    const int local_x = world_x - chunk_x * kChunkSize;
    const int local_z = world_z - chunk_z * kChunkSize;
    Chunk& chunk = chunk_at(chunk_x, chunk_z);
    if (!chunk.set(local_x, y, local_z, id)) return false;
    const size_t column = static_cast<size_t>(local_z) * kChunkSize + local_x;
    if (id != BlockId::Air && !block_is_liquid(id) && !block_is_plant(id)) {
        chunk.surface_height[column] = static_cast<int16_t>(
            std::max<int>(chunk.surface_height[column], y));
    } else if (y >= chunk.surface_height[column]) {
        int top = -1;
        for (int scan = kChunkHeight - 1; scan >= 0; --scan) {
            const BlockId candidate = chunk.get(local_x, scan, local_z);
            if (candidate != BlockId::Air && !block_is_liquid(candidate) &&
                !block_is_plant(candidate)) {
                top = scan;
                break;
            }
        }
        chunk.surface_height[column] = static_cast<int16_t>(std::max(0, top));
    }
    return true;
}

void WorldGenerator::clear_cache() {
    cache_.clear();
}

ChunkSummary WorldGenerator::summarize(const Chunk& chunk) const {
    ChunkSummary summary;
    summary.coord = chunk.coord;
    summary.minimum_height = kChunkHeight;
    std::map<int, int> biome_counts;
    for (int z = 0; z < kChunkSize; ++z) {
        for (int x = 0; x < kChunkSize; ++x) {
            const int height = chunk.height[static_cast<size_t>(z) * kChunkSize + x];
            summary.minimum_height = std::min(summary.minimum_height, height);
            summary.maximum_height = std::max(summary.maximum_height, height);
            biome_counts[chunk.biome[static_cast<size_t>(z) * kChunkSize + x]]++;
        }
    }
    for (const auto& block : chunk.blocks) {
        if (block == BlockId::Air) continue;
        if (block == BlockId::Water || block == BlockId::FlowingWater) ++summary.water_blocks;
        else if (block == BlockId::Lava || block == BlockId::FlowingLava) ++summary.lava_blocks;
        else if (block_is_ore(block)) {
            ++summary.ore_blocks;
            ++summary.solid_blocks;
        } else if (block_is_solid(block)) {
            ++summary.solid_blocks;
        }
    }
    summary.biome_count = static_cast<int>(biome_counts.size());
    int best = -1;
    for (const auto& entry : biome_counts) {
        if (entry.second > best) {
            best = entry.second;
            summary.dominant_biome = entry.first;
        }
    }
    return summary;
}

} // namespace nexus::worldgen
