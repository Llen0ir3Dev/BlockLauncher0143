#pragma once

#include <string>
#include <vector>

#include <nexus/texture/pack.hpp>

namespace nexus::texture {

struct PreviewOptions {
    uint32_t width = 128;
    uint32_t height = 128;
    bool preserve_aspect = true;
};

bool write_png_preview(const uint8_t* data, size_t size, const std::string& output_path,
                       const PreviewOptions& options, std::string& error);

bool build_atlas(const std::string& pack_path, const std::string& output_png,
                 const std::string& output_json, uint32_t tile_size, uint32_t columns,
                 std::string& error);

} // namespace nexus::texture
