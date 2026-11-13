#include <nexus/texture/preview.hpp>

#include <algorithm>
#include <cstring>
#include <sstream>
#include <vector>

#include <png.h>

#include <nlohmann/json.hpp>

#include <nexus/core/fs.hpp>
#include <nexus/core/log.hpp>
#include <nexus/core/strings.hpp>
#include <nexus/core/zip.hpp>

namespace nexus::texture {

namespace {

struct Image {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> rgba;
};

bool decode_png(const uint8_t* data, size_t size, Image& image, std::string& error) {
    if (data == nullptr || size == 0) {
        error = "empty image data";
        return false;
    }
    png_image png;
    std::memset(&png, 0, sizeof(png));
    png.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&png, data, size)) {
        error = std::string("PNG decode failed: ") + png.message;
        return false;
    }
    png.format = PNG_FORMAT_RGBA;
    image.width = png.width;
    image.height = png.height;
    image.rgba.resize(PNG_IMAGE_SIZE(png));
    if (!png_image_finish_read(&png, nullptr, image.rgba.data(), 0, nullptr)) {
        error = std::string("PNG decode failed: ") + png.message;
        png_image_free(&png);
        return false;
    }
    png_image_free(&png);
    if (image.width == 0 || image.height == 0) {
        error = "PNG has zero dimensions";
        return false;
    }
    return true;
}

Image resize_nearest(const Image& source, uint32_t width, uint32_t height, bool preserve_aspect) {
    Image output;
    output.width = width;
    output.height = height;
    output.rgba.assign(static_cast<size_t>(width) * height * 4, 0);
    if (source.width == 0 || source.height == 0) return output;

    uint32_t draw_width = width;
    uint32_t draw_height = height;
    uint32_t offset_x = 0;
    uint32_t offset_y = 0;
    if (preserve_aspect) {
        const float scale_x = static_cast<float>(width) / static_cast<float>(source.width);
        const float scale_y = static_cast<float>(height) / static_cast<float>(source.height);
        const float scale = std::min(scale_x, scale_y);
        draw_width = std::max(1u, static_cast<uint32_t>(source.width * scale));
        draw_height = std::max(1u, static_cast<uint32_t>(source.height * scale));
        offset_x = (width - draw_width) / 2;
        offset_y = (height - draw_height) / 2;
    }
    for (uint32_t y = 0; y < draw_height; ++y) {
        const uint32_t sy = std::min(source.height - 1, y * source.height / draw_height);
        for (uint32_t x = 0; x < draw_width; ++x) {
            const uint32_t sx = std::min(source.width - 1, x * source.width / draw_width);
            const uint8_t* src = source.rgba.data() + (static_cast<size_t>(sy) * source.width + sx) * 4;
            uint8_t* dst = output.rgba.data() +
                           (static_cast<size_t>(y + offset_y) * width + (x + offset_x)) * 4;
            std::memcpy(dst, src, 4);
        }
    }
    return output;
}

bool encode_png(const Image& image, const std::string& path, std::string& error) {
    png_image png;
    std::memset(&png, 0, sizeof(png));
    png.version = PNG_IMAGE_VERSION;
    png.width = image.width;
    png.height = image.height;
    png.format = PNG_FORMAT_RGBA;
    if (!png_image_write_to_file(&png, path.c_str(), 0, image.rgba.data(), 0, nullptr)) {
        error = std::string("PNG encode failed: ") + png.message;
        png_image_free(&png);
        return false;
    }
    png_image_free(&png);
    return true;
}

} // namespace

bool write_png_preview(const uint8_t* data, size_t size, const std::string& output_path,
                       const PreviewOptions& options, std::string& error) {
    if (options.width == 0 || options.height == 0) {
        error = "preview dimensions must be non-zero";
        return false;
    }
    Image source;
    if (!decode_png(data, size, source, error)) return false;
    Image preview = resize_nearest(source, options.width, options.height, options.preserve_aspect);
    if (!fs::ensure_parent(output_path)) {
        error = "cannot create preview directory for " + output_path;
        return false;
    }
    return encode_png(preview, output_path, error);
}

bool build_atlas(const std::string& pack_path, const std::string& output_png,
                 const std::string& output_json, uint32_t tile_size, uint32_t columns,
                 std::string& error) {
    if (tile_size == 0 || columns == 0) {
        error = "tile size and column count must be non-zero";
        return false;
    }
    std::string inspect_error;
    TexturePack pack = inspect_texture_pack(pack_path, &inspect_error);
    if (pack.entries.empty()) {
        error = inspect_error.empty() ? "pack contains no images" : inspect_error;
        return false;
    }

    std::vector<const TextureEntry*> usable;
    for (const auto& entry : pack.entries) {
        if (entry.image.valid && entry.image.format == ImageFormat::Png) usable.push_back(&entry);
    }
    if (usable.empty()) {
        error = "pack contains no decodable PNG images";
        return false;
    }
    const uint32_t count = static_cast<uint32_t>(usable.size());
    const uint32_t rows = (count + columns - 1) / columns;

    Image atlas;
    atlas.width = columns * tile_size;
    atlas.height = rows * tile_size;
    atlas.rgba.assign(static_cast<size_t>(atlas.width) * atlas.height * 4, 0);
    for (size_t i = 0; i < atlas.rgba.size(); i += 4) {
        atlas.rgba[i + 0] = 24;
        atlas.rgba[i + 1] = 24;
        atlas.rgba[i + 2] = 24;
        atlas.rgba[i + 3] = 255;
    }

    nlohmann::json metadata;
    metadata["pack"] = pack.name;
    metadata["tile_size"] = tile_size;
    metadata["columns"] = columns;
    metadata["rows"] = rows;
    metadata["tiles"] = nlohmann::json::array();

    for (uint32_t index = 0; index < count; ++index) {
        const TextureEntry& entry = *usable[index];
        std::vector<uint8_t> data;
        std::string read_error;
        if (!read_pack_entry(pack, entry, data, read_error)) {
            metadata["tiles"].push_back({{"index", index}, {"path", entry.target_path}, {"error", read_error}});
            continue;
        }
        Image image;
        std::string decode_error;
        if (!decode_png(data.data(), data.size(), image, decode_error)) {
            metadata["tiles"].push_back({{"index", index}, {"path", entry.target_path}, {"error", decode_error}});
            continue;
        }
        Image tile = resize_nearest(image, tile_size, tile_size, true);
        const uint32_t tile_x = (index % columns) * tile_size;
        const uint32_t tile_y = (index / columns) * tile_size;
        for (uint32_t y = 0; y < tile_size; ++y) {
            for (uint32_t x = 0; x < tile_size; ++x) {
                const uint8_t* src = tile.rgba.data() + (static_cast<size_t>(y) * tile_size + x) * 4;
                uint8_t* dst = atlas.rgba.data() +
                               (static_cast<size_t>(tile_y + y) * atlas.width + (tile_x + x)) * 4;
                std::memcpy(dst, src, 4);
            }
        }
        metadata["tiles"].push_back({
            {"index", index},
            {"path", entry.target_path},
            {"source", entry.source_path},
            {"x", tile_x},
            {"y", tile_y},
            {"width", tile_size},
            {"height", tile_size}
        });
    }

    if (!fs::ensure_parent(output_png) || !fs::ensure_parent(output_json)) {
        error = "cannot create atlas output directory";
        return false;
    }
    if (!encode_png(atlas, output_png, error)) return false;
    if (!fs::write_file(output_json, metadata.dump(2) + "\n")) {
        error = "cannot write atlas metadata";
        return false;
    }
    return true;
}

} // namespace nexus::texture
