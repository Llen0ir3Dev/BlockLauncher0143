#include <nexus/worldgen/blocks.hpp>

namespace nexus::worldgen {

const char* block_name(BlockId id) {
    switch (id) {
        case BlockId::Air: return "air";
        case BlockId::Stone: return "stone";
        case BlockId::Grass: return "grass";
        case BlockId::Dirt: return "dirt";
        case BlockId::Cobblestone: return "cobblestone";
        case BlockId::Planks: return "planks";
        case BlockId::Bedrock: return "bedrock";
        case BlockId::FlowingWater: return "flowing_water";
        case BlockId::Water: return "water";
        case BlockId::FlowingLava: return "flowing_lava";
        case BlockId::Lava: return "lava";
        case BlockId::Sand: return "sand";
        case BlockId::Gravel: return "gravel";
        case BlockId::GoldOre: return "gold_ore";
        case BlockId::IronOre: return "iron_ore";
        case BlockId::CoalOre: return "coal_ore";
        case BlockId::Log: return "log";
        case BlockId::Leaves: return "leaves";
        case BlockId::LapisOre: return "lapis_ore";
        case BlockId::Sandstone: return "sandstone";
        case BlockId::SnowLayer: return "snow_layer";
        case BlockId::Ice: return "ice";
        case BlockId::Snow: return "snow";
        case BlockId::Cactus: return "cactus";
        case BlockId::Clay: return "clay";
        case BlockId::Obsidian: return "obsidian";
        case BlockId::DiamondOre: return "diamond_ore";
        case BlockId::RedstoneOre: return "redstone_ore";
        case BlockId::StoneBricks: return "stone_bricks";
        case BlockId::Podzol: return "podzol";
        case BlockId::HardenedClay: return "hardened_clay";
    }
    return "unknown";
}

bool block_is_solid(BlockId id) {
    switch (id) {
        case BlockId::Air:
        case BlockId::FlowingWater:
        case BlockId::Water:
        case BlockId::FlowingLava:
        case BlockId::Lava:
        case BlockId::SnowLayer:
            return false;
        default:
            return true;
    }
}

bool block_is_liquid(BlockId id) {
    return id == BlockId::Water || id == BlockId::FlowingWater ||
           id == BlockId::Lava || id == BlockId::FlowingLava;
}

bool block_is_ore(BlockId id) {
    return id == BlockId::CoalOre || id == BlockId::IronOre || id == BlockId::GoldOre ||
           id == BlockId::DiamondOre || id == BlockId::RedstoneOre || id == BlockId::LapisOre;
}

bool block_is_plant(BlockId id) {
    return id == BlockId::Leaves || id == BlockId::SnowLayer;
}

} // namespace nexus::worldgen
