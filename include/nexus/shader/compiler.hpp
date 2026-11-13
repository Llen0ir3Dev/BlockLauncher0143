#pragma once

#include <string>
#include <vector>

#include <nexus/shader/ast.hpp>

namespace nexus::shader {

struct CompileOptions {
    ShaderStage stage = ShaderStage::Unknown;
    std::string glslang_path = "";
    bool run_glslang = true;
    // GLSL version used to resolve MCPE `#if __VERSION__` multiversion sources
    // before handing the shader to glslang.  0 means auto-detect (100 unless
    // the source already declares `#version 300 es`).
    int glsl_version = 0;
};

struct CompileResult {
    bool success = false;
    bool validator_available = false;
    std::string validator;
    std::string log;
    std::string error;
    int exit_code = -1;
};

CompileResult compile_shader(const std::string& source, const CompileOptions& options = {});
CompileResult validate_structure(const std::string& source, ShaderStage stage);
std::string find_glslang_validator();

} // namespace nexus::shader
