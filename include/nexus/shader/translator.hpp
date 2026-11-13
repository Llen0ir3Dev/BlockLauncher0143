#pragma once

#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexus/shader/analyzer.hpp>
#include <nexus/shader/ast.hpp>

namespace nexus::shader {

enum class TargetProfile { GLES100, GLSL120, GLSLES300 };
enum class QualityProfile { Performance, Balanced, Quality };

const char* target_name(TargetProfile target);
const char* quality_name(QualityProfile quality);
bool parse_target(const std::string& text, TargetProfile& out);
bool parse_quality(const std::string& text, QualityProfile& out);

struct TranslationOptions {
    TargetProfile target = TargetProfile::GLES100;
    QualityProfile quality = QualityProfile::Balanced;
    bool map_optifine_uniforms = true;
    bool emulate_es3_functions = true;
    bool inject_precision = true;
    bool add_derivative_extension = true;
    bool validate_structure = true;
    bool keep_unsupported = true;
};

struct UniformMapping {
    std::string source;
    std::string target;
    std::string note;
    bool supported = true;
};

struct TranslationResult {
    bool success = false;
    std::string error;
    CompatibilityLevel level = CompatibilityLevel::Unknown;
    std::string source;
    std::vector<Issue> issues;
    std::vector<UniformMapping> uniform_map;
    std::vector<std::string> required_extensions;
    nlohmann::json report = nlohmann::json::object();
};

TranslationResult translate_glsl(const std::string& source, ShaderStage stage,
                                 const TranslationOptions& options = {});

TranslationResult translate_unit(const TranslationUnit& unit, ShaderStage stage,
                                 const TranslationOptions& options = {});

// Mapping table between common Minecraft Java / OptiFine uniforms and the
// NEXUS runtime uniform names.  Unknown uniforms are passed through and
// reported; they are never silently dropped.
const std::vector<UniformMapping>& optifine_uniform_map();

} // namespace nexus::shader
