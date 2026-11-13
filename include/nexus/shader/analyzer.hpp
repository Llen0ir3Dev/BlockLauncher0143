#pragma once

#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexus/shader/ast.hpp>

namespace nexus::shader {

enum class IssueSeverity { Info, Warning, Error };

struct Issue {
    IssueSeverity severity = IssueSeverity::Info;
    std::string code;
    std::string message;
    SourceLoc loc;
    std::string hint;
};

const char* severity_name(IssueSeverity severity);

struct ShaderVariable {
    std::string name;
    std::string type;
    std::string qualifier;
    int array_size = 0;
    SourceLoc loc;
};

struct ShaderInfo {
    ShaderStage stage = ShaderStage::Unknown;
    int version = 110;
    bool es = false;
    int function_count = 0;
    int global_count = 0;
    bool has_main = false;
    bool uses_frag_color = false;
    bool uses_frag_data = false;
    bool uses_texel_fetch = false;
    bool uses_texture_lod = false;
    bool uses_derivatives = false;
    bool uses_discard = false;
    bool uses_switch = false;
    bool uses_uint = false;
    bool uses_ubo = false;
    bool uses_geometry = false;
    bool uses_compute = false;
    bool uses_double = false;
    std::vector<ShaderVariable> uniforms;
    std::vector<ShaderVariable> attributes;
    std::vector<ShaderVariable> varyings;
    std::vector<ShaderVariable> outputs;
    std::vector<std::string> samplers;
    std::vector<std::string> functions;
    std::vector<std::string> optifine_uniforms;
    std::vector<std::string> unsupported_builtins;
};

struct AnalysisResult {
    ShaderInfo info;
    std::vector<Issue> issues;
    CompatibilityLevel level = CompatibilityLevel::Unknown;
    nlohmann::json to_json() const;
    std::string to_markdown() const;
};

AnalysisResult analyze_shader(const TranslationUnit& unit, ShaderStage stage);

} // namespace nexus::shader
