#include <nexus/texture/pack.hpp>

#include <algorithm>
#include <cstring>
#include <sstream>

#include <zlib.h>

#include <nexus/core/fs.hpp>
#include <nexus/core/sha256.hpp>
#include <nexus/core/strings.hpp>
#include <nexus/core/zip.hpp>
#include <nexus/texture/preview.hpp>

namespace nexus::texture {

namespace {

uint16_t rd16le(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t rd32le(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
uint32_t rd32be(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

ImageInfo inspect_png(const uint8_t* data, size_t size) {
    ImageInfo info;
    static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    if (size < 33 || std::memcmp(data, signature, 8) != 0) return info;
    if (rd32be(data + 8) != 13 || std::memcmp(data + 12, "IHDR", 4) != 0) {
        info.error = "PNG missing IHDR";
        return info;
    }
    info.format = ImageFormat::Png;
    info.width = rd32be(data + 16);
    info.height = rd32be(data + 20);
    info.bit_depth = data[24];
    const uint8_t color_type = data[25];
    switch (color_type) {
        case 0: info.channels = 1; break;
        case 2: info.channels = 3; break;
        case 3: info.channels = 1; break;
        case 4: info.channels = 2; break;
        case 6: info.channels = 4; break;
        default: info.channels = 0; break;
    }
    info.valid = info.width > 0 && info.height > 0 && info.bit_depth > 0 && info.channels > 0;
    if (!info.valid) info.error = "invalid PNG IHDR";
    return info;
}

ImageInfo inspect_jpeg(const uint8_t* data, size_t size) {
    ImageInfo info;
    if (size < 4 || data[0] != 0xff || data[1] != 0xd8) return info;
    info.format = ImageFormat::Jpeg;
    size_t pos = 2;
    while (pos + 4 < size) {
        if (data[pos] != 0xff) { ++pos; continue; }
        const uint8_t marker = data[pos + 1];
        if (marker == 0xd8 || marker == 0xd9) { pos += 2; continue; }
        if (pos + 4 > size) break;
        const uint16_t length = static_cast<uint16_t>((data[pos + 2] << 8) | data[pos + 3]);
        if (length < 2) break;
        if ((marker >= 0xc0 && marker <= 0xc3) || (marker >= 0xc5 && marker <= 0xc7) ||
            (marker >= 0xc9 && marker <= 0xcb) || (marker >= 0xcd && marker <= 0xcf)) {
            if (pos + 9 <= size) {
                info.height = static_cast<uint32_t>((data[pos + 5] << 8) | data[pos + 6]);
                info.width = static_cast<uint32_t>((data[pos + 7] << 8) | data[pos + 8]);
                info.channels = data[pos + 9];
                info.valid = info.width > 0 && info.height > 0;
                return info;
            }
        }
        pos += 2 + length;
    }
    info.error = "could not find JPEG frame header";
    return info;
}

ImageInfo inspect_tga(const uint8_t* data, size_t size) {
    ImageInfo info;
    if (size < 18) return info;
    const uint8_t id_length = data[0];
    const uint8_t color_map_type = data[1];
    const uint8_t image_type = data[2];
    const uint16_t width = rd16le(data + 12);
    const uint16_t height = rd16le(data + 14);
    const uint8_t bpp = data[16];
    const bool type_ok = image_type == 1 || image_type == 2 || image_type == 3 ||
                         image_type == 9 || image_type == 10 || image_type == 11;
    const bool bpp_ok = bpp == 8 || bpp == 15 || bpp == 16 || bpp == 24 || bpp == 32;
    if (color_map_type > 1 || !type_ok || !bpp_ok || width == 0 || height == 0) return info;
    if (size < 18ull + id_length) return info;
    info.format = ImageFormat::Tga;
    info.width = width;
    info.height = height;
    info.bit_depth = bpp;
    info.channels = static_cast<uint8_t>(bpp / 8);
    info.valid = true;
    return info;
}

void parse_metadata(TexturePack& pack, const std::string& name, const std::string& text) {
    try {
        const auto j = nlohmann::json::parse(text);
        if (name == "pack.mcmeta" && j.contains("pack")) {
            const auto& p = j["pack"];
            if (p.contains("description")) {
                if (p["description"].is_string()) pack.metadata["description"] = p["description"].get<std::string>();
                else pack.metadata["description"] = p["description"].dump();
            }
            if (p.contains("pack_format")) pack.metadata["pack_format"] = std::to_string(p["pack_format"].get<int>());
        } else if (name == "manifest.json") {
            if (j.contains("header")) {
                const auto& h = j["header"];
                if (h.contains("name")) pack.metadata["name"] = h["name"].get<std::string>();
                if (h.contains("description")) pack.metadata["description"] = h["description"].get<std::string>();
                if (h.contains("version")) pack.metadata["version"] = h["version"].dump();
            }
        }
    } catch (const std::exception&) {
        pack.issues.push_back("could not parse metadata: " + name);
    }
}

bool metadata_name(const std::string& path) {
    return ends_with(path, "pack.mcmeta") || ends_with(path, "manifest.json") ||
           ends_with(path, "pack.json");
}

} // namespace

bool is_image_path(const std::string& path) {
    const std::string ext = extension(path);
    return ext == ".png" || ext == ".tga" || ext == ".jpg" || ext == ".jpeg";
}

ImageInfo inspect_image(const uint8_t* data, size_t size) {
    const ImageInfo png = inspect_png(data, size);
    if (png.format != ImageFormat::Unknown) return png;
    const ImageInfo jpeg = inspect_jpeg(data, size);
    if (jpeg.format != ImageFormat::Unknown) return jpeg;
    const ImageInfo tga = inspect_tga(data, size);
    if (tga.format != ImageFormat::Unknown) return tga;
    ImageInfo unknown;
    unknown.error = "unrecognised image format";
    return unknown;
}

std::string map_texture_path(const std::string& source_path, PackKind kind) {
    std::string path = normalize_slashes(source_path);
    auto replace_prefix = [&](const std::string& prefix, const std::string& target) -> bool {
        if (path.rfind(prefix, 0) == 0) {
            path = target + path.substr(prefix.size());
            return true;
        }
        return false;
    };
    if (kind == PackKind::Java) {
        if (replace_prefix("assets/minecraft/textures/block/", "textures/blocks/")) return path;
        if (replace_prefix("assets/minecraft/textures/item/", "textures/items/")) return path;
        if (replace_prefix("assets/minecraft/textures/entity/", "textures/entity/")) return path;
        if (replace_prefix("assets/minecraft/textures/gui/", "textures/gui/")) return path;
        if (replace_prefix("assets/minecraft/textures/environment/", "textures/environment/")) return path;
        if (replace_prefix("assets/minecraft/textures/painting/", "textures/painting/")) return path;
        if (replace_prefix("assets/minecraft/textures/particle/", "textures/particle/")) return path;
        if (replace_prefix("assets/minecraft/textures/font/", "textures/font/")) return path;
        if (replace_prefix("assets/minecraft/textures/misc/", "textures/misc/")) return path;
        if (replace_prefix("assets/minecraft/textures/", "textures/")) return path;
    }
    if (path.rfind("assets/images/", 0) == 0) return path;
    if (path.rfind("assets/", 0) == 0) return path;
    return path;
}

TexturePack inspect_texture_pack(const std::string& path, std::string* error) {
    TexturePack pack;
    pack.root = path;
    pack.name = basename(path);
    pack.archive = extension(path) == ".zip";
    std::map<std::string, std::string> metadata_files;

    auto add_entry = [&](const std::string& name, const std::vector<uint8_t>& data) {
        TextureEntry entry;
        entry.source_path = name;
        entry.size = data.size();
        entry.crc32 = crc32(0L, data.data(), static_cast<uInt>(data.size()));
        entry.sha256 = Sha256::hash(data.data(), data.size());
        entry.image = inspect_image(data.data(), data.size());
        if (entry.image.format == ImageFormat::Unknown && extension(name) == ".tga") {
            entry.image = inspect_tga(data.data(), data.size());
        }
        if (!entry.image.valid) {
            entry.error = entry.image.error.empty() ? "invalid image" : entry.image.error;
            pack.issues.push_back(name + ": " + entry.error);
        }
        pack.entries.push_back(std::move(entry));
    };

    if (pack.archive) {
        ZipReader zip;
        std::string zip_error;
        if (!zip.open(path, zip_error)) {
            if (error) *error = zip_error;
            return pack;
        }
        for (const auto& ze : zip.entries()) {
            if (ze.is_directory) continue;
            if (is_image_path(ze.name)) {
                if (ze.uncompressed_size > 64ull * 1024 * 1024) {
                    pack.issues.push_back("image too large, skipped: " + ze.name);
                    continue;
                }
                std::vector<uint8_t> data;
                std::string read_error;
                if (zip.read(ze.name, data, read_error)) add_entry(normalize_slashes(ze.name), data);
                else pack.issues.push_back(read_error);
            } else if (metadata_name(ze.name)) {
                std::string text;
                std::string read_error;
                if (zip.read_text(ze.name, text, read_error)) metadata_files[normalize_slashes(ze.name)] = text;
            }
        }
    } else if (fs::is_directory(path)) {
        for (const auto& file : fs::list_directory(path, true)) {
            const std::string name = normalize_slashes(file);
            std::string relative = name;
            const std::string root = normalize_slashes(path);
            if (relative.compare(0, root.size(), root) == 0) {
                relative = relative.substr(root.size());
                while (!relative.empty() && relative.front() == '/') relative.erase(relative.begin());
            }
            if (is_image_path(relative)) {
                std::vector<uint8_t> data;
                if (fs::read_file_binary(file, data, 64ull * 1024 * 1024)) add_entry(relative, data);
                else pack.issues.push_back("cannot read image: " + relative);
            } else if (metadata_name(relative)) {
                std::string text;
                if (fs::read_file(file, text)) metadata_files[relative] = text;
            }
        }
    } else {
        if (error) *error = "texture pack is neither a directory nor a .zip archive: " + path;
        return pack;
    }

    bool has_java = false;
    bool has_bedrock = false;
    bool has_pe = false;
    for (const auto& entry : pack.entries) {
        if (entry.source_path.rfind("assets/minecraft/textures/", 0) == 0) has_java = true;
        if (entry.source_path.rfind("textures/", 0) == 0) has_bedrock = true;
        if (entry.source_path.rfind("assets/images/", 0) == 0 ||
            entry.source_path.rfind("images/", 0) == 0) has_pe = true;
    }
    if (metadata_files.count("pack.mcmeta") || has_java) pack.kind = PackKind::Java;
    else if (has_pe) pack.kind = PackKind::PocketEdition;
    else if (metadata_files.count("manifest.json") || has_bedrock) pack.kind = PackKind::Bedrock;
    else pack.kind = PackKind::Unknown;

    for (const auto& item : metadata_files) parse_metadata(pack, basename(item.first), item.second);
    for (auto& entry : pack.entries) entry.target_path = map_texture_path(entry.source_path, pack.kind);

    if (pack.entries.empty()) {
        if (error) *error = "texture pack contains no images";
    }
    return pack;
}

bool read_pack_entry(const TexturePack& pack, const TextureEntry& entry,
                     std::vector<uint8_t>& data, std::string& error) {
    if (pack.archive) {
        ZipReader zip;
        if (!zip.open(pack.root, error)) return false;
        return zip.read(entry.source_path, data, error);
    }
    return fs::read_file_binary(fs::join(pack.root, entry.source_path), data, 64ull * 1024 * 1024);
}

nlohmann::json ImportResult::to_json() const {
    nlohmann::json j;
    j["success"] = success;
    j["error"] = error;
    j["pack"] = pack.name;
    j["kind"] = pack.kind == PackKind::Java ? "java" :
                pack.kind == PackKind::Bedrock ? "bedrock" :
                pack.kind == PackKind::PocketEdition ? "pocket-edition" : "unknown";
    j["imported"] = imported;
    j["failed"] = failed;
    j["previews"] = previews;
    j["issues"] = pack.issues;
    j["entries"] = nlohmann::json::array();
    for (const auto& entry : pack.entries) {
        j["entries"].push_back({
            {"source", entry.source_path},
            {"target", entry.target_path},
            {"size", entry.size},
            {"sha256", entry.sha256},
            {"format", entry.image.format == ImageFormat::Png ? "png" :
                       entry.image.format == ImageFormat::Tga ? "tga" :
                       entry.image.format == ImageFormat::Jpeg ? "jpeg" : "unknown"},
            {"width", entry.image.width},
            {"height", entry.image.height},
            {"valid", entry.image.valid},
            {"imported", entry.imported},
            {"error", entry.error}
        });
    }
    return j;
}

std::string ImportResult::to_markdown() const {
    std::ostringstream out;
    out << "# Texture pack import report\n\n";
    out << "- Pack: `" << pack.name << "`\n";
    out << "- Imported: " << imported << "\n";
    out << "- Failed: " << failed << "\n";
    out << "- Previews: " << previews << "\n\n";
    if (!pack.issues.empty()) {
        out << "## Issues\n\n";
        for (const auto& issue : pack.issues) out << "- " << issue << "\n";
    }
    return out.str();
}

ImportResult import_texture_pack(const std::string& path, const std::string& output_dir,
                                 const ImportOptions& options) {
    ImportResult result;
    std::string inspect_error;
    result.pack = inspect_texture_pack(path, &inspect_error);
    if (result.pack.entries.empty()) {
        result.error = inspect_error.empty() ? "texture pack contains no images" : inspect_error;
        return result;
    }
    const std::string converted_dir = fs::join(output_dir, "converted");
    const std::string preview_dir = fs::join(output_dir, "previews");
    if (!fs::ensure_directory(converted_dir) || !fs::ensure_directory(preview_dir)) {
        result.error = "cannot create import output directories";
        return result;
    }
    uint32_t preview_count = 0;
    for (auto& entry : result.pack.entries) {
        std::vector<uint8_t> data;
        std::string read_error;
        if (!read_pack_entry(result.pack, entry, data, read_error)) {
            entry.error = read_error;
            ++result.failed;
            continue;
        }
        if (options.copy_images) {
            const std::string target = options.convert_paths ? entry.target_path : entry.source_path;
            const std::string safe_target = fs::sanitize_relative_path(target);
            if (safe_target.empty() || !fs::is_safe_relative_path(safe_target)) {
                entry.error = "unsafe target path";
                ++result.failed;
                continue;
            }
            if (!fs::write_file_binary(fs::join(converted_dir, safe_target), data)) {
                entry.error = "cannot write converted image";
                ++result.failed;
                continue;
            }
            entry.target_path = safe_target;
        }
        entry.imported = true;
        ++result.imported;
        if (options.generate_previews && preview_count < options.max_previews &&
            entry.image.format == ImageFormat::Png) {
            const std::string preview_name = fs::sanitize_relative_path(entry.target_path);
            std::string preview_error;
            PreviewOptions poptions;
            poptions.width = options.preview_size;
            poptions.height = options.preview_size;
            if (write_png_preview(data.data(), data.size(), fs::join(preview_dir, preview_name + ".png"),
                                  poptions, preview_error)) {
                ++preview_count;
            } else {
                result.pack.issues.push_back("preview failed for " + entry.source_path + ": " + preview_error);
            }
        }
    }
    result.previews = preview_count;
    result.success = result.imported > 0;
    if (!result.success) result.error = "no image could be imported";

    if (!fs::write_file(fs::join(output_dir, "report.json"), result.to_json().dump(2) + "\n")) {
        result.error = "cannot write import report";
    }
    fs::write_file(fs::join(output_dir, "REPORT.md"), result.to_markdown());
    return result;
}

} // namespace nexus::texture
