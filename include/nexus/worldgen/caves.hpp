#pragma once

#include <cstdint>

#include <nexus/worldgen/chunk.hpp>
#include <nexus/worldgen/noise.hpp>

namespace nexus::worldgen {

struct CaveSettings {
    bool enabled = true;
    int min_y = 5;
    int max_y = 110;
    double threshold = 0.58;
    double secondary_threshold = 0.46;
    double tunnel_width = 0.032;
    double horizontal_scale = 0.021;
    double vertical_scale = 0.035;
    bool lava_at_bottom = true;
    int lava_level = 10;
};

class CaveGenerator {
public:
    CaveGenerator(uint64_t seed, const CaveSettings& settings = {});

    const CaveSettings& settings() const { return settings_; }
    void carve(Chunk& chunk) const;

private:
    CaveSettings settings_;
    ValueNoise3D caverns_;
    ValueNoise3D tunnels_;
    ValueNoise3D detail_;
};

} // namespace nexus::worldgen
