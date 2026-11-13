#include "../test_support.hpp"

#include <nexus/core/event_bus.hpp>
#include <nexus/core/fs.hpp>
#include <nexus/mod/mod.hpp>
#include <nexus/mod/registry.hpp>

using namespace nexus_test;

namespace {

std::string write_mod(const std::string& root, const std::string& id,
                      const std::string& script) {
    const std::string directory = nexus::fs::join(root, id);
    nexus::fs::ensure_directory(directory);
    const std::string manifest =
        "{\n"
        "  \"id\": \"" + id + "\",\n"
        "  \"name\": \"Test mod\",\n"
        "  \"version\": \"1.2.3\",\n"
        "  \"apiVersion\": \"1.0\",\n"
        "  \"type\": \"script\",\n"
        "  \"entry\": \"main.js\",\n"
        "  \"events\": [\"test.event\"]\n"
        "}\n";
    nexus::fs::write_file(nexus::fs::join(directory, "mod.json"), manifest);
    nexus::fs::write_file(nexus::fs::join(directory, "main.js"), script);
    return directory;
}

void test_manifest_parsing() {
    const std::string root = make_temp("nexus-mod-test");
    const std::string directory = write_mod(root, "test.mod", "nexus.log('info','hi');\n");
    nexus::mod::ModManifest manifest;
    std::string error;
    CHECK(nexus::mod::load_manifest(directory, manifest, error));
    CHECK(error.empty());
    CHECK_EQ(manifest.id, std::string("test.mod"));
    CHECK_EQ(manifest.version, std::string("1.2.3"));
    CHECK_EQ(manifest.entry, std::string("main.js"));
    CHECK(manifest.type == nexus::mod::ModType::Script);
    nexus::fs::remove_all(root);
}

void test_host_load_and_register() {
    const std::string root = make_temp("nexus-mod-host");
    const std::string script =
        "nexus.log('info', 'test mod loaded');\n"
        "nexus.on('test.event', function (data) {\n"
        "  nexus.log('info', 'event ' + data.value);\n"
        "});\n"
        "var block = nexus.registerBlock({\n"
        "  name: 'test:ruby_block',\n"
        "  id: 200,\n"
        "  texture: 'textures/blocks/ruby',\n"
        "  hardness: 2.5\n"
        "});\n"
        "var item = nexus.registerItem({\n"
        "  name: 'test:ruby',\n"
        "  id: 400,\n"
        "  texture: 'textures/items/ruby',\n"
        "  maxStack: 64\n"
        "});\n"
        "nexus.log('info', 'block=' + block + ' item=' + item);\n";
    write_mod(root, "test.mod", script);

    nexus::EventBus events;
    nexus::mod::ContentRegistry registry;
    nexus::mod::HookManager hooks;
    nexus::mod::ModHostOptions options;
    options.allow_native = false;
    nexus::mod::ModHost host(events, registry, hooks, options);

    CHECK_EQ(host.discover(root), static_cast<size_t>(1));
    const auto& mods = host.mods();
    CHECK_EQ(mods.size(), static_cast<size_t>(1));

    std::string error;
    CHECK(host.enable("test.mod", error));
    if (!error.empty()) std::cerr << error << "\n";
    CHECK_EQ(host.active_count(), static_cast<size_t>(1));

    const auto* block = registry.find_block("test:ruby_block");
    CHECK(block != nullptr);
    if (block != nullptr) {
        CHECK_EQ(block->id, static_cast<uint16_t>(200));
        CHECK_EQ(block->mod_id, std::string("test.mod"));
    }
    const auto* item = registry.find_item("test:ruby");
    CHECK(item != nullptr);
    if (item != nullptr) CHECK_EQ(item->id, static_cast<uint16_t>(400));

    CHECK(host.emit("test.event", {{"value", 42}}));
    CHECK(host.unload("test.mod", error));
    CHECK_EQ(host.active_count(), static_cast<size_t>(0));
    CHECK(registry.find_block("test:ruby_block") == nullptr);

    nexus::fs::remove_all(root);
}

void test_missing_entry_fails() {
    const std::string root = make_temp("nexus-mod-missing");
    const std::string directory = nexus::fs::join(root, "broken.mod");
    nexus::fs::ensure_directory(directory);
    nexus::fs::write_file(nexus::fs::join(directory, "mod.json"),
                          "{\"id\":\"broken.mod\",\"entry\":\"main.js\"}\n");
    nexus::EventBus events;
    nexus::mod::ContentRegistry registry;
    nexus::mod::HookManager hooks;
    nexus::mod::ModHost host(events, registry, hooks);
    CHECK_EQ(host.discover(root), static_cast<size_t>(1));
    std::string error;
    CHECK(!host.enable("broken.mod", error));
    CHECK(!error.empty());
    nexus::fs::remove_all(root);
}

void test_invalid_manifest_rejected() {
    const std::string root = make_temp("nexus-mod-invalid");
    const std::string directory = nexus::fs::join(root, "bad");
    nexus::fs::ensure_directory(directory);
    nexus::fs::write_file(nexus::fs::join(directory, "mod.json"),
                          "{\"id\":\"bad id with spaces\",\"entry\":\"main.js\"}\n");
    nexus::mod::ModManifest manifest;
    std::string error;
    CHECK(!nexus::mod::load_manifest(directory, manifest, error));
    CHECK(!error.empty());
    nexus::fs::remove_all(root);
}

} // namespace

int main() {
    return run_all("mod", {
        {"manifest parsing", test_manifest_parsing},
        {"host load and register", test_host_load_and_register},
        {"missing entry fails", test_missing_entry_fails},
        {"invalid manifest rejected", test_invalid_manifest_rejected},
    });
}
