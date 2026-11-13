#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace nexus::texture {

enum class PackKind { Unknown, Java, Bedrock, PocketEdition };
enum class ImageFormat { Unknown, Png, Tga, Jpeg, Binary };

struct ImageInfo {
    ImageFormat format = ImageFormat::Unknown;
    uint32_t width = 0;
    uint32_t height = 0;
    uint8_t bit_depth = 0;
    uint8_t channels = 0;
    bool valid = false;
    std::string error;
};

struct TextureEntry {
    std::string source_path;
    std::string target_path;
    uint64_t size = 0;
    uint32_t crc32 = 0;
    ImageInfo image;
    std::string sha256;
    bool imported = false;
    std::string error;
};

struct TexturePack {
    std::string name;
    std::string root;
    bool archive = false;
    PackKind kind = PackKind::Unknown;
    std::vector<TextureEntry> entries;
    std::map<std::string, std::string> metadata;
    std::vector<std::string> issues;
};

struct ImportOptions {
    bool generate_previews = true;
    bool convert_paths = true;
    bool copy_images = true;
    uint32_t preview_size = 128;
    uint32_t max_previews = 64;
    uint64_t max_file_bytes = 32ull * 1024 * 1024;
};

TexturePack inspect_texture_pack(const std::string& path, std::string* error = nullptr);
ImageInfo inspect_image(const uint8_t* data, size_t size);
bool is_image_path(const std::string& path);

struct ImportResult {
    bool success = false;
    std::string error;
    TexturePack pack;
    uint32_t imported = 0;
    uint32_t failed = 0;
    uint32_t previews = 0;
    nlohmann::json to_json() const;
    std::string to_markdown() const;
};

ImportResult import_texture_pack(const std::string& path, const std::string& output_dir,
                                 const ImportOptions& options = {});

bool read_pack_entry(const TexturePack& pack, const TextureEntry& entry,
                     std::vector<uint8_t>& data, std::string& error);

std::string map_texture_path(const std::string& source_path, PackKind kind);

} // namespace nexus::texture
