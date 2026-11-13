#pragma once

#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexus/shader/analyzer.hpp>
#include <nexus/shader/compiler.hpp>
#include <nexus/shader/translator.hpp>

namespace nexus::shader {

struct ShaderFile {
    std::string virtual_path;
    std::string source;
    bool from_archive = false;
};

struct ShaderProgram {
    std::string name;
    std::string virtual_path;
    ShaderStage stage = ShaderStage::Unknown;
    std::string source;
    std::vector<std::string> includes;
    bool resolved = false;
    std::string error;
};

enum class PackFormat { Unknown, Java, OptiFine, Mixed };

struct ShaderPack {
    std::string name;
    std::string root;
    bool archive = false;
    PackFormat format = PackFormat::Unknown;
    std::map<std::string, std::string> properties;
    std::map<std::string, ShaderFile> files;
    std::vector<ShaderProgram> programs;
    std::vector<std::string> issues;
    std::string shaders_prefix;

    const ShaderProgram* find_program(const std::string& name) const;
    std::vector<std::string> program_names() const;
};

struct PackLoadOptions {
    uint64_t max_file_bytes = 8ull * 1024 * 1024;
    uint64_t max_total_bytes = 128ull * 1024 * 1024;
    size_t max_include_depth = 16;
};

ShaderPack load_shader_pack(const std::string& path, const PackLoadOptions& options = {},
                            std::string* error = nullptr);

struct ProgramTranslation {
    std::string name;
    ShaderStage stage = ShaderStage::Unknown;
    AnalysisResult analysis;
    TranslationResult translation;
    CompileResult compile;
};

struct PackTranslationResult {
    std::string pack_name;
    PackFormat format = PackFormat::Unknown;
    std::vector<ProgramTranslation> programs;
    int full = 0;
    int partial = 0;
    int unsupported = 0;
    nlohmann::json to_json() const;
    std::string to_markdown() const;
};

PackTranslationResult analyze_and_translate_pack(const std::string& path,
                                                 const TranslationOptions& options = {},
                                                 bool compile = false);

bool write_translation_output(const PackTranslationResult& result, const std::string& output_dir,
                              std::string& error);

} // namespace nexus::shader
