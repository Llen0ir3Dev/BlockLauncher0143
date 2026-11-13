#include "../test_support.hpp"

#include <set>

#include <nexus/worldgen/blocks.hpp>
#include <nexus/worldgen/extension.hpp>
#include <nexus/worldgen/world.hpp>

using namespace nexus_test;
using namespace nexus::worldgen;

namespace {

void test_determinism() {
    WorldGenerator a(1234567);
    WorldGenerator b(1234567);
    const Chunk first = a.generate_chunk(3, -2);
    const Chunk second = b.generate_chunk(3, -2);
    CHECK(first.generated);
    CHECK(second.generated);
    CHECK(first.blocks.size() == static_cast<size_t>(kChunkVolume));
    CHECK(first.blocks == second.blocks);
    CHECK(first.height == second.height);
    CHECK(first.biome == second.biome);
    CHECK_EQ(a.height_at(-17, 42), b.height_at(-17, 42));
    CHECK(a.biome_at(-17, 42) == b.biome_at(-17, 42));
}

void test_seed_changes_world() {
    WorldGenerator a(1);
    WorldGenerator b(2);
    const Chunk first = a.generate_chunk(0, 0);
    const Chunk second = b.generate_chunk(0, 0);
    CHECK(first.blocks != second.blocks);
}

void test_summary() {
    WorldGenerator world(42);
    const Chunk chunk = world.generate_chunk(0, 0);
    const auto summary = world.summarize(chunk);
    CHECK(summary.solid_blocks > 0);
    CHECK(summary.maximum_height >= summary.minimum_height);
    CHECK(summary.biome_count > 0);
    CHECK(summary.dominant_biome >= 0);
}

void test_block_mutation_and_cache() {
    WorldGenerator world(7);
    const int x = 5;
    const int z = 6;
    CHECK(world.set_block(x, 100, z, BlockId::Stone));
    CHECK(world.block_at(x, 100, z) == BlockId::Stone);
    const Chunk* cached = world.cached_chunk(0, 0);
    CHECK(cached != nullptr);
    if (cached != nullptr) {
        CHECK(cached->get(x, 100, z) == BlockId::Stone);
    }
    CHECK(world.cached_chunks() >= 1);
    world.clear_cache();
    CHECK_EQ(world.cached_chunks(), static_cast<size_t>(0));
}

void test_extension_hook() {
    WorldGenerator world(99);
    world.extensions().add(std::make_shared<FlatTerrainExtension>(12));
    const Chunk chunk = world.generate_chunk(0, 0);
    const auto summary = world.summarize(chunk);
    CHECK(summary.maximum_height <= 12);
    CHECK(summary.solid_blocks > 0);
}

void test_several_chunks_are_distinct() {
    WorldGenerator world(2024);
    std::set<int> heights;
    for (int cz = -1; cz <= 1; ++cz) {
        for (int cx = -1; cx <= 1; ++cx) {
            const Chunk chunk = world.generate_chunk(cx, cz);
            CHECK(chunk.generated);
            heights.insert(world.summarize(chunk).maximum_height);
        }
    }
    CHECK(heights.size() >= 2);
}

} // namespace

int main() {
    return run_all("worldgen", {
        {"determinism", test_determinism},
        {"seed changes world", test_seed_changes_world},
        {"chunk summary", test_summary},
        {"block mutation and cache", test_block_mutation_and_cache},
        {"extension hook", test_extension_hook},
        {"multiple chunks", test_several_chunks_are_distinct},
    });
}
