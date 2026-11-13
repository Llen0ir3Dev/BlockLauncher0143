#include "../test_support.hpp"

#include <nexus/core/config.hpp>
#include <nexus/core/fs.hpp>
#include <nexus/core/sha256.hpp>
#include <nexus/core/strings.hpp>
#include <nexus/core/version.hpp>

using namespace nexus_test;

namespace {

void test_sha256() {
    CHECK_EQ(nexus::Sha256::hash(std::string("")), std::string(
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    CHECK_EQ(nexus::Sha256::hash(std::string("abc")), std::string(
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    nexus::Sha256 hasher;
    hasher.update("a");
    hasher.update("bc");
    CHECK_EQ(hasher.final_hex(), std::string(
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
}

void test_strings() {
    CHECK_EQ(nexus::to_lower("AbC"), std::string("abc"));
    CHECK_EQ(nexus::to_upper("AbC"), std::string("ABC"));
    CHECK_EQ(nexus::trim("  a b  "), std::string("a b"));
    CHECK(nexus::starts_with("shaders/terrain.vertex", "shaders/"));
    CHECK(nexus::ends_with("terrain.vertex", ".vertex"));
    const auto parts = nexus::split("a,b,,c", ',');
    CHECK_EQ(parts.size(), static_cast<size_t>(4));
    CHECK_EQ(nexus::join({"a", "b", "c"}, "/"), std::string("a/b/c"));
    CHECK_EQ(nexus::basename("assets/shaders/terrain.vertex"),
             std::string("terrain.vertex"));
    CHECK_EQ(nexus::dirname("assets/shaders/terrain.vertex"), std::string("assets/shaders"));
    CHECK_EQ(nexus::extension("terrain.vertex"), std::string(".vertex"));
    CHECK_EQ(nexus::replace_all("a.b.c", ".", "/"), std::string("a/b/c"));
}

void test_version_mapping() {
    CHECK(nexus::is_x86_version_code(780140300));
    CHECK(nexus::is_x86_version_code(780140301));
    CHECK(!nexus::is_x86_version_code(760140300));
    CHECK(nexus::is_supported_x86_version(780140300));
    CHECK(!nexus::is_supported_x86_version(760140300));
    const auto* code = nexus::find_game_version_code(760140300);
    CHECK(code != nullptr);
    if (code != nullptr) {
        CHECK(code->architecture == nexus::GameArchitecture::ArmeabiV7aJb);
    }
    CHECK(std::string(nexus::kGameVersion) == "0.14.3");
}

void test_filesystem_and_config() {
    const std::string directory = make_temp("nexus-core-test");
    CHECK(!directory.empty());
    const std::string nested = nexus::fs::join(directory, "a/b/c");
    CHECK(nexus::fs::ensure_directory(nested));
    CHECK(nexus::fs::is_directory(nested));

    const std::string path = nexus::fs::join(directory, "settings.json");
    nexus::Config config;
    config.set("renderer.shaderPack", "vanilla");
    config.set("renderer.maxFps", 60);
    config.set("runtime.captureScreenshots", true);
    std::string error;
    CHECK(config.save(path, error));
    CHECK(error.empty());

    nexus::Config loaded;
    CHECK(loaded.load(path, error));
    CHECK_EQ(loaded.get_string("renderer.shaderPack"), std::string("vanilla"));
    CHECK_EQ(loaded.get_int("renderer.maxFps", 0), 60);
    CHECK(loaded.get_bool("runtime.captureScreenshots", false));

    const std::string binary = nexus::fs::join(directory, "blob.bin");
    const std::vector<uint8_t> bytes = {0, 1, 2, 3, 250, 255};
    CHECK(nexus::fs::write_file_binary(binary, bytes));
    CHECK_EQ(nexus::fs::file_size(binary), static_cast<uint64_t>(bytes.size()));
    std::vector<uint8_t> read_back;
    CHECK(nexus::fs::read_file_binary(binary, read_back));
    CHECK(read_back == bytes);

    CHECK(nexus::fs::is_safe_relative_path("textures/blocks/stone.png"));
    CHECK(!nexus::fs::is_safe_relative_path("../escape.png"));
    CHECK(!nexus::fs::is_safe_relative_path("/etc/passwd"));

    nexus::fs::remove_all(directory);
    CHECK(!nexus::fs::exists(directory));
}

} // namespace

int main() {
    return run_all("core", {
        {"sha256", test_sha256},
        {"strings", test_strings},
        {"version mapping", test_version_mapping},
        {"filesystem and config", test_filesystem_and_config},
    });
}
