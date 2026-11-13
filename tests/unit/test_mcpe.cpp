#include "../test_support.hpp"

#include <nexus/core/fs.hpp>
#include <nexus/mcpe/mcpe.hpp>

using namespace nexus_test;

namespace {

void write_assets(const std::string& root) {
    const std::string shaders = nexus::fs::join(root, "shaders");
    const std::string materials = nexus::fs::join(root, "materials");
    nexus::fs::ensure_directory(shaders);
    nexus::fs::ensure_directory(materials);
    nexus::fs::write_file(nexus::fs::join(shaders, "uniforms.json"), R"({
  "PerFrameConstants": [
    {"Name": "TIME", "Type": "Float1"},
    {"Name": "VIEW_POS", "Type": "Float3"}
  ],
  "EntityConstants": [
    {"Name": "OVERLAY_COLOR", "Type": "Float4"}
  ],
  "ShaderConstants": [
    {"Name": "WORLDVIEWPROJECTION", "Type": "Matrix4x4"}
  ]
})");
    nexus::fs::write_file(nexus::fs::join(shaders, "color.vertex"),
                          "// __multiversion__\n"
                          "attribute vec3 POSITION;\n"
                          "uniform mat4 WORLDVIEWPROJECTION;\n"
                          "void main() { gl_Position = WORLDVIEWPROJECTION * vec4(POSITION, 1.0); }\n");
    nexus::fs::write_file(nexus::fs::join(shaders, "color.fragment"),
                          "// __multiversion__\n"
                          "#if __VERSION__ >= 300\n"
                          "out vec4 FragColor;\n"
                          "#endif\n"
                          "uniform vec4 OVERLAY_COLOR;\n"
                          "void main() {\n"
                          "#if __VERSION__ >= 300\n"
                          "  FragColor = OVERLAY_COLOR;\n"
                          "#else\n"
                          "  gl_FragColor = OVERLAY_COLOR;\n"
                          "#endif\n"
                          "}\n");
    nexus::fs::write_file(nexus::fs::join(materials, "test.material"), R"({
  "color": {
    "states": ["Blending"],
    "blendSrc": "SourceAlpha",
    "blendDst": "OneMinusSourceAlpha",
    "vertexShader": "shaders/color.vertex",
    "fragmentShader": "shaders/color.fragment"
  }
})");
}

void test_uniform_catalog() {
    const std::string root = make_temp("nexus-mcpe-test");
    write_assets(root);
    nexus::mcpe::UniformCatalog catalog;
    std::string error;
    CHECK(catalog.load(nexus::fs::join(root, "shaders/uniforms.json"), error));
    CHECK(error.empty());
    CHECK_EQ(catalog.groups.size(), static_cast<size_t>(3));
    const auto* time = catalog.find("TIME");
    CHECK(time != nullptr);
    if (time != nullptr) {
        CHECK_EQ(time->glsl_type, std::string("float"));
        CHECK_EQ(time->count, 1);
    }
    const auto* matrix = catalog.find("WORLDVIEWPROJECTION");
    CHECK(matrix != nullptr);
    if (matrix != nullptr) {
        CHECK_EQ(matrix->glsl_type, std::string("mat4"));
        CHECK_EQ(matrix->matrix_dim, 4);
    }
    CHECK(catalog.find("MISSING") == nullptr);
    CHECK(!catalog.to_json().is_null());
    nexus::fs::remove_all(root);
}

void test_asset_summary() {
    const std::string root = make_temp("nexus-mcpe-assets");
    write_assets(root);
    const auto summary = nexus::mcpe::inspect_assets(root);
    CHECK(summary.uniforms.loaded);
    CHECK_EQ(summary.materials.files.size(), static_cast<size_t>(1));
    CHECK_EQ(summary.materials.pass_count, static_cast<size_t>(1));
    CHECK_EQ(summary.materials.referenced_shaders.size(), static_cast<size_t>(2));
    CHECK(summary.missing_shaders.empty());
    CHECK(summary.shaders.size() >= 2);
    CHECK(summary.to_json().value("assetsRoot", "") == root);
    const std::string markdown = summary.to_markdown();
    CHECK(markdown.find("Uniform groups") != std::string::npos);
    nexus::fs::remove_all(root);
}

void test_multiversion_preparation() {
    const std::string source =
        "// __multiversion__\n"
        "// comment\n"
        "#if __VERSION__ >= 300\n"
        "out vec4 FragColor;\n"
        "#endif\n"
        "void main() {}\n";
    CHECK(nexus::mcpe::has_multiversion_marker(source));
    const std::string gles100 = nexus::mcpe::prepare_multiversion_source(source, 100);
    CHECK(gles100.find("#version 100") != std::string::npos);
    CHECK(gles100.find("__VERSION__ 100") != std::string::npos);
    CHECK(gles100.find("__multiversion__") == std::string::npos);
    const std::string gles300 = nexus::mcpe::prepare_multiversion_source(source, 300);
    CHECK(gles300.find("#version 300 es") != std::string::npos);
    CHECK(gles300.find("__VERSION__ 300") != std::string::npos);
    CHECK(!nexus::mcpe::has_multiversion_marker("void main() {}"));
}

void test_shader_path_resolution() {
    const std::string root = "/tmp/nexus-assets";
    CHECK_EQ(nexus::mcpe::resolve_shader_path(root, "shaders/color.vertex"),
             nexus::fs::join(root, "shaders/color.vertex"));
    CHECK(nexus::mcpe::resolve_shader_path(root, "../../etc/passwd").empty());
    CHECK(nexus::mcpe::resolve_shader_path(root, "/etc/passwd").empty());
}

} // namespace

int main() {
    return run_all("mcpe", {
        {"uniform catalog", test_uniform_catalog},
        {"asset summary", test_asset_summary},
        {"multiversion preparation", test_multiversion_preparation},
        {"shader path resolution", test_shader_path_resolution},
    });
}
