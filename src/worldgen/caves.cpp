#include <nexus/worldgen/caves.hpp>

#include <algorithm>
#include <cmath>

namespace nexus::worldgen {

CaveGenerator::CaveGenerator(uint64_t seed, const CaveSettings& settings)
    : settings_(settings),
      caverns_(hash_combine(seed, 0x2001)),
      tunnels_(hash_combine(seed, 0x2002)),
      detail_(hash_combine(seed, 0x2003)) {}

void CaveGenerator::carve(Chunk& chunk) const {
    if (!settings_.enabled) return;
    const int origin_x = chunk.coord.x * kChunkSize;
    const int origin_z = chunk.coord.z * kChunkSize;
    for (int z = 0; z < kChunkSize; ++z) {
        for (int x = 0; x < kChunkSize; ++x) {
            const size_t column = static_cast<size_t>(z) * kChunkSize + x;
            const int surface = chunk.height[column];
            const int max_y = std::min(settings_.max_y, surface - 2);
            const double wx = (origin_x + x) * settings_.horizontal_scale;
            const double wz = (origin_z + z) * settings_.horizontal_scale;
            for (int y = settings_.min_y; y <= max_y; ++y) {
                const BlockId current = chunk.get(x, y, z);
                if (current == BlockId::Air || current == BlockId::Bedrock ||
                    current == BlockId::Water || current == BlockId::Ice) {
                    continue;
                }
                const double wy = y * settings_.vertical_scale;
                const double cavern = fbm3(caverns_, wx, wy, wz, 3, 0.52, 2.0);
                const double tunnel = fbm3(tunnels_, wx * 0.7 + 13.0, wy * 0.9, wz * 0.7, 2, 0.5, 2.0);
                const double detail = detail_.sample(wx * 2.0, wy * 2.0, wz * 2.0);
                bool carved = false;
                if (cavern > settings_.threshold && tunnel > settings_.secondary_threshold &&
                    detail > 0.32) {
                    carved = true;
                }
                if (std::fabs(tunnel - 0.5) < settings_.tunnel_width && cavern > 0.43) {
                    carved = true;
                }
                if (!carved) continue;
                if (settings_.lava_at_bottom && y <= settings_.lava_level) {
                    chunk.set(x, y, z, BlockId::Lava);
                } else {
                    chunk.set(x, y, z, BlockId::Air);
                }
                if (y == chunk.surface_height[column] && y > 0) {
                    chunk.surface_height[column] = static_cast<int16_t>(std::max(0, y - 1));
                }
            }
        }
    }
}

} // namespace nexus::worldgen
