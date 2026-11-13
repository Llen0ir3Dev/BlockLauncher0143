#include "../test_support.hpp"

#include <nexus/shader/analyzer.hpp>
#include <nexus/shader/compiler.hpp>
#include <nexus/shader/pack.hpp>
#include <nexus/shader/preprocessor.hpp>
#include <nexus/shader/translator.hpp>

using namespace nexus_test;

namespace {

const char* kFragment =
    "precision mediump float;\n"
    "varying vec2 v_texcoord;\n"
    "uniform sampler2D TEXTURE_0;\n"
    "uniform vec4 FOG_COLOR;\n"
    "void main() {\n"
    "  vec4 tex = texture2D(TEXTURE_0, v_texcoord);\n"
    "  gl_FragColor = mix(tex * FOG_COLOR, tex, 0.25);\n"
    "}\n";

const char* kVertex =
    "precision highp float;\n"
    "attribute vec3 POSITION;\n"
    "attribute vec2 TEXCOORD_0;\n"
    "uniform mat4 WORLDVIEWPROJECTION;\n"
    "varying vec2 v_texcoord;\n"
    "void main() {\n"
    "  v_texcoord = TEXCOORD_0;\n"
    "  gl_Position = WORLDVIEWPROJECTION * vec4(POSITION, 1.0);\n"
    "}\n";

void test_translate_gles100() {
    nexus::shader::TranslationOptions options;
    options.target = nexus::shader::TargetProfile::GLES100;
    const auto result = nexus::shader::translate_glsl(
        kFragment, nexus::shader::ShaderStage::Fragment, options);
    CHECK(result.success);
    if (!result.success) std::cerr << result.error << "\n";
    CHECK(result.source.find("gl_FragColor") != std::string::npos);
    CHECK(result.source.find("texture2D") != std::string::npos);
    CHECK(result.source.find("precision") != std::string::npos);
    CHECK(!result.report.is_null());
    CHECK(result.level != nexus::shader::CompatibilityLevel::Unsupported);
}

void test_translate_vertex() {
    nexus::shader::TranslationOptions options;
    options.target = nexus::shader::TargetProfile::GLES100;
    const auto result =
        nexus::shader::translate_glsl(kVertex, nexus::shader::ShaderStage::Vertex, options);
    CHECK(result.success);
    CHECK(result.source.find("gl_Position") != std::string::npos);
    CHECK(result.source.find("POSITION") != std::string::npos);
}

void test_analyze() {
    nexus::shader::PreprocessOptions preprocess_options;
    preprocess_options.stage = nexus::shader::ShaderStage::Fragment;
    const auto pre = nexus::shader::preprocess_glsl(kFragment, preprocess_options);
    CHECK(pre.success);

    nexus::shader::ParseOptions parse_options;
    parse_options.stage = nexus::shader::ShaderStage::Fragment;
    auto parsed = nexus::shader::parse_glsl(pre.source, parse_options);
    CHECK(parsed.success);
    if (!parsed.success) std::cerr << parsed.error << "\n";
    parsed.unit.version = pre.version;
    parsed.unit.es = pre.es;

    const auto analysis =
        nexus::shader::analyze_shader(parsed.unit, nexus::shader::ShaderStage::Fragment);
    CHECK(analysis.info.has_main);
    CHECK(analysis.info.function_count >= 1);
    CHECK(analysis.info.uses_frag_color);
    CHECK(analysis.level != nexus::shader::CompatibilityLevel::Unsupported);

    bool found_texture = false;
    for (const auto& uniform : analysis.info.uniforms) {
        if (uniform.name == "TEXTURE_0") found_texture = true;
    }
    CHECK(found_texture);
}

void test_target_profiles() {
    nexus::shader::TargetProfile target = nexus::shader::TargetProfile::GLES100;
    CHECK(nexus::shader::parse_target("gles100", target));
    CHECK(target == nexus::shader::TargetProfile::GLES100);
    CHECK(nexus::shader::parse_target("es300", target));
    CHECK(target == nexus::shader::TargetProfile::GLSLES300);
    CHECK(nexus::shader::parse_target("glsl120", target));
    CHECK(target == nexus::shader::TargetProfile::GLSL120);
    CHECK(!nexus::shader::parse_target("nonsense", target));
}

void test_structural_compile() {
    const auto result = nexus::shader::validate_structure(
        kFragment, nexus::shader::ShaderStage::Fragment);
    CHECK(result.success);
}

void test_invalid_shader_rejected() {
    const auto result = nexus::shader::translate_glsl(
        "void main( { gl_FragColor = vec4(1.0); }\n",
        nexus::shader::ShaderStage::Fragment, {});
    CHECK(!result.success);
    CHECK(!result.error.empty());
}

void test_multiversion_compile() {
    // Mirrors the real MCPE shader layout: __VERSION__ branches plus MAT4 /
    // POS4 shorthand.  The compile path must preprocess these before handing
    // the source to glslang, which rejects redefining __VERSION__.
    const std::string source =
        "// __multiversion__\n"
        "#if __VERSION__ >= 300\n"
        "out vec4 FragColor;\n"
        "#define gl_FragColor FragColor\n"
        "#endif\n"
        "precision mediump float;\n"
        "#define MAT4 mat4\n"
        "#define POS4 vec4\n"
        "uniform MAT4 WORLDVIEWPROJ;\n"
        "attribute POS4 POSITION;\n"
        "void main() {\n"
        "  gl_Position = WORLDVIEWPROJ * POSITION;\n"
        "}\n";
    nexus::shader::CompileOptions options;
    options.stage = nexus::shader::ShaderStage::Vertex;
    options.glsl_version = 100;
    const auto result = nexus::shader::compile_shader(source, options);
    CHECK(result.success);
    if (!result.success) std::cerr << result.log << "\\n" << result.error << "\\n";
}

void test_es100_fragment_precision_injected() {
    const std::string source =
        "#version 100\n"
        "varying vec4 color;\n"
        "void main() { gl_FragColor = color; }\n";
    nexus::shader::CompileOptions options;
    options.stage = nexus::shader::ShaderStage::Fragment;
    const auto result = nexus::shader::compile_shader(source, options);
    CHECK(result.success);
    if (!result.success) std::cerr << result.log << "\\n" << result.error << "\\n";
}

} // namespace

int main() {
    return run_all("shader", {
        {"translate GLES100 fragment", test_translate_gles100},
        {"translate GLES100 vertex", test_translate_vertex},
        {"analyze", test_analyze},
        {"target profiles", test_target_profiles},
        {"structural compile", test_structural_compile},
        {"invalid shader rejected", test_invalid_shader_rejected},
        {"multiversion compile", test_multiversion_compile},
        {"es100 fragment precision", test_es100_fragment_precision_injected},
    });
}
