#include "../test_support.hpp"

#include <cstdint>
#include <vector>

#include <nexus/core/fs.hpp>
#include <nexus/texture/pack.hpp>

using namespace nexus_test;

namespace {

// Minimal valid 1x1 RGBA PNG (transparent pixel).  Kept as bytes so the test
// does not depend on any external asset or image tool.
const uint8_t kOnePixelPng[] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
    0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4, 0x89, 0x00, 0x00, 0x00,
    0x0A, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0x00, 0x01, 0x00, 0x00,
    0x05, 0x00, 0x01, 0x0D, 0x0A, 0x2D, 0xB4, 0x00, 0x00, 0x00, 0x00, 0x49,
    0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};

void test_inspect_png_bytes() {
    const auto info = nexus::texture::inspect_image(
        kOnePixelPng, sizeof(kOnePixelPng));
    CHECK(info.valid);
    CHECK(info.format == nexus::texture::ImageFormat::Png);
    CHECK_EQ(info.width, static_cast<uint32_t>(1));
    CHECK_EQ(info.height, static_cast<uint32_t>(1));
    CHECK_EQ(info.bit_depth, static_cast<uint8_t>(8));
}

void test_inspect_image_rejects_garbage() {
    const uint8_t garbage[] = {0x00, 0x01, 0x02, 0x03, 0x04};
    const auto info = nexus::texture::inspect_image(garbage, sizeof(garbage));
    CHECK(!info.valid);
    CHECK(!info.error.empty());
}

void test_inspect_pack_and_import() {
    const std::string directory = make_temp("nexus-texture-test");
    const std::string textures = nexus::fs::join(directory, "textures/blocks");
    CHECK(nexus::fs::ensure_directory(textures));
    const std::string image = nexus::fs::join(textures, "stone.png");
    CHECK(nexus::fs::write_file_binary(
        image, std::vector<uint8_t>(kOnePixelPng, kOnePixelPng + sizeof(kOnePixelPng))));

    std::string error;
    const auto pack = nexus::texture::inspect_texture_pack(directory, &error);
    CHECK(error.empty());
    CHECK_EQ(pack.entries.size(), static_cast<size_t>(1));
    if (!pack.entries.empty()) {
        CHECK(pack.entries.front().image.valid);
        CHECK_EQ(pack.entries.front().image.width, static_cast<uint32_t>(1));
        CHECK_EQ(pack.entries.front().source_path, std::string("textures/blocks/stone.png"));
    }

    nexus::texture::ImportOptions options;
    options.generate_previews = true;
    const std::string output = nexus::fs::join(directory, "out");
    const auto imported = nexus::texture::import_texture_pack(directory, output, options);
    CHECK(imported.success);
    CHECK_EQ(imported.imported, static_cast<uint32_t>(1));
    CHECK_EQ(imported.failed, static_cast<uint32_t>(0));
    CHECK(nexus::fs::is_regular_file(
        nexus::fs::join(output, "converted/textures/blocks/stone.png")));

    const std::string archive = nexus::fs::join(directory, "not-a-zip.zip");
    CHECK(nexus::fs::write_file(archive, "this is not a zip archive"));
    const auto bad = nexus::texture::inspect_texture_pack(archive, &error);
    CHECK(!error.empty() || !bad.issues.empty());

    nexus::fs::remove_all(directory);
}

void test_path_mapping() {
    CHECK_EQ(nexus::texture::map_texture_path(
                 "assets/minecraft/textures/block/stone.png",
                 nexus::texture::PackKind::Java),
             std::string("textures/blocks/stone.png"));
    CHECK(nexus::texture::is_image_path("textures/blocks/stone.png"));
    CHECK(!nexus::texture::is_image_path("textures/blocks/stone.txt"));
}

} // namespace

int main() {
    return run_all("texture", {
        {"inspect PNG bytes", test_inspect_png_bytes},
        {"reject garbage", test_inspect_image_rejects_garbage},
        {"inspect pack and import", test_inspect_pack_and_import},
        {"path mapping", test_path_mapping},
    });
}
