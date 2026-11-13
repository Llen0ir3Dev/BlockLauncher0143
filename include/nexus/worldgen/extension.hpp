#pragma once

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include <nexus/worldgen/biome.hpp>
#include <nexus/worldgen/chunk.hpp>
#include <nexus/worldgen/terrain.hpp>

namespace nexus::worldgen {

struct GenerationContext {
    uint64_t world_seed = 0;
    const TerrainSettings* terrain = nullptr;
    Chunk* chunk = nullptr;
    int chunk_origin_x = 0;
    int chunk_origin_z = 0;
};

// Extensions are applied in priority order (lower first).  Hooks are only
// called when a generator actually runs; no configuration file is treated as
// if it modified the world by itself.
class GenerationExtension {
public:
    virtual ~GenerationExtension() = default;

    virtual std::string name() const = 0;
    virtual std::string version() const { return "1.0.0"; }
    virtual int priority() const { return 0; }

    virtual void on_chunk_start(GenerationContext& context) { (void)context; }
    // Return a replacement height, or the original value to leave it alone.
    virtual int modify_surface_height(GenerationContext& context, int world_x, int world_z,
                                      int height) {
        (void)context; (void)world_x; (void)world_z;
        return height;
    }
    // Modify a block that was produced by the base generators.
    virtual BlockId modify_block(GenerationContext& context, int x, int y, int z, BlockId block) {
        (void)context; (void)x; (void)y; (void)z;
        return block;
    }
    virtual void on_chunk_end(GenerationContext& context) { (void)context; }
};

class ExtensionRegistry {
public:
    void add(std::shared_ptr<GenerationExtension> extension);
    bool remove(const std::string& name);
    void clear();
    bool empty() const { return extensions_.empty(); }
    size_t size() const { return extensions_.size(); }

    const std::vector<std::shared_ptr<GenerationExtension>>& extensions() const {
        return extensions_;
    }

    void sort_by_priority();

private:
    std::vector<std::shared_ptr<GenerationExtension>> extensions_;
};

// Small built-in extension used by the CLI and the tests.  It demonstrates
// that extension hooks affect real generated blocks rather than only a
// configuration file.
class FlatTerrainExtension : public GenerationExtension {
public:
    explicit FlatTerrainExtension(int height, BlockId surface = BlockId::Grass)
        : height_(height), surface_(surface) {}
    std::string name() const override { return "builtin.flat-terrain"; }
    int modify_surface_height(GenerationContext&, int, int, int) override { return height_; }
    BlockId modify_block(GenerationContext&, int x, int y, int z, BlockId block) override {
        (void)x; (void)z;
        if (y == 0) return BlockId::Bedrock;
        if (y < height_ - 3) return BlockId::Stone;
        if (y < height_) return BlockId::Dirt;
        if (y == height_) return surface_;
        return block;
    }
    int height() const { return height_; }

private:
    int height_;
    BlockId surface_;
};

} // namespace nexus::worldgen
