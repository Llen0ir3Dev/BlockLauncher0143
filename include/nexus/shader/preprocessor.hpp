#pragma once

#include <map>
#include <string>
#include <vector>

#include <nexus/shader/ast.hpp>

namespace nexus::shader {

struct PreprocessOptions {
    ShaderStage stage = ShaderStage::Unknown;
    std::map<std::string, std::string> defines;
    bool preserve_directives = false;
};

struct PreprocessResult {
    bool success = false;
    std::string error;
    std::string source;
    int version = 110;
    bool es = false;
    std::vector<std::string> directives;
    std::vector<std::string> extensions;
    std::vector<std::string> warnings;
};

PreprocessResult preprocess_glsl(const std::string& source, const PreprocessOptions& options = {});

} // namespace nexus::shader
