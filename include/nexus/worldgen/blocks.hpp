#pragma once

#include <cstdint>

namespace nexus::worldgen {

// Numeric block ids follow the Minecraft Pocket Edition 0.14 block registry
// for the blocks used by this framework.  They are intentionally stored as
// plain bytes so generated chunks remain cheap to serialize.
enum class BlockId : uint8_t {
    Air = 0,
    Stone = 1,
    Grass = 2,
    Dirt = 3,
    Cobblestone = 4,
    Planks = 5,
    Bedrock = 7,
    FlowingWater = 8,
    Water = 9,
    FlowingLava = 10,
    Lava = 11,
    Sand = 12,
    Gravel = 13,
    GoldOre = 14,
    IronOre = 15,
    CoalOre = 16,
    Log = 17,
    Leaves = 18,
    LapisOre = 21,
    Sandstone = 24,
    SnowLayer = 78,
    Ice = 79,
    Snow = 80,
    Cactus = 81,
    Clay = 82,
    Obsidian = 49,
    DiamondOre = 56,
    RedstoneOre = 73,
    StoneBricks = 98,
    Podzol = 243,
    HardenedClay = 172
};

const char* block_name(BlockId id);
bool block_is_solid(BlockId id);
bool block_is_liquid(BlockId id);
bool block_is_ore(BlockId id);
bool block_is_plant(BlockId id);

} // namespace nexus::worldgen
