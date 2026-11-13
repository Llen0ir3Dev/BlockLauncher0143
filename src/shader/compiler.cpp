#include <nexus/shader/compiler.hpp>
#include <nexus/shader/preprocessor.hpp>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <sstream>

#if !defined(_WIN32)
#include <sys/wait.h>
#endif

#include <nexus/core/fs.hpp>
#include <nexus/core/log.hpp>
#include <nexus/core/strings.hpp>

namespace nexus::shader {

std::string find_glslang_validator() {
    const char* env = std::getenv("GLSLANG_VALIDATOR");
    if (env && *env && fs::is_regular_file(env)) return env;
    const char* path_env = std::getenv("PATH");
    if (!path_env) return "";
    const auto dirs = split(path_env, ':');
    for (const auto& dir : dirs) {
        for (const char* name : {"glslangValidator", "glslang"}) {
            const std::string candidate = fs::join(dir, name);
            if (fs::is_regular_file(candidate)) return candidate;
        }
    }
    return "";
}

CompileResult validate_structure(const std::string& source, ShaderStage stage) {
    CompileResult result;
    result.validator_available = false;
    const ParseResult parsed = parse_glsl(source, ParseOptions{stage});
    if (parsed.success) {
        result.success = true;
        result.log = "structural validation passed";
    } else {
        result.success = false;
        result.error = parsed.error;
        result.log = "structural validation failed: " + parsed.error;
    }
    return result;
}

CompileResult compile_shader(const std::string& source, const CompileOptions& options) {
    CompileResult result;

    // MCPE ships "multiversion" shaders that branch on the reserved macro
    // __VERSION__ and rely on the game's loader to inject a matching
    // `#define __VERSION__ ...`.  glslangValidator rejects redefining a name
    // with consecutive underscores, so resolve those branches with our own
    // preprocessor and emit a single flat GLSL source before validation.
    std::string compile_source = source;
    if (source.find("__VERSION__") != std::string::npos) {
        int version = options.glsl_version;
        if (version <= 0) {
            version = source.find("#version 300") != std::string::npos ? 300 : 100;
        }
        PreprocessOptions preprocess_options;
        preprocess_options.stage = options.stage;
        preprocess_options.defines["__VERSION__"] = std::to_string(version);
        const PreprocessResult pre = preprocess_glsl(source, preprocess_options);
        if (!pre.success) {
            result.success = false;
            result.error = "preprocess failed: " + pre.error;
            result.log = result.error;
            return result;
        }
        std::string header =
            version >= 300 ? std::string("#version 300 es\n") : std::string("#version 100\n");
        // GLSL ES 1.00 fragment shaders have no default float precision, and
        // the shipped MCPE sources rely on the loader to supply one.
        if (version < 300 && options.stage == ShaderStage::Fragment &&
            pre.source.find("precision") == std::string::npos) {
            header += "precision mediump float;\n";
        }
        if (pre.source.find("#define MAT4") == std::string::npos &&
            (pre.source.find("MAT4") != std::string::npos ||
             pre.source.find("MAT3") != std::string::npos ||
             pre.source.find("MAT2") != std::string::npos ||
             pre.source.find("POS4") != std::string::npos ||
             pre.source.find("POS3") != std::string::npos ||
             pre.source.find("POS2") != std::string::npos)) {
            header +=
                "#define MAT4 mat4\n"
                "#define MAT3 mat3\n"
                "#define MAT2 mat2\n"
                "#define POS4 vec4\n"
                "#define POS3 vec3\n"
                "#define POS2 vec2\n";
        }
        for (const auto& extension : pre.extensions) {
            header += "#extension " + extension + "\n";
        }
        compile_source = header + pre.source;
    } else {
        // Plain (non-multiversion) MCPE shaders use the loader's MAT4/POS4
        // shorthand and may omit the ES fragment float precision.  Supply
        // both only when the shader is actually an ES 1.00 source.
        const bool es100 = source.find("#version 100") != std::string::npos;
        std::string injection;
        if (es100 && options.stage == ShaderStage::Fragment &&
            source.find("precision") == std::string::npos) {
            injection += "precision mediump float;\n";
        }
        const bool uses_shorthand =
            source.find("MAT4") != std::string::npos || source.find("MAT3") != std::string::npos ||
            source.find("MAT2") != std::string::npos || source.find("POS4") != std::string::npos ||
            source.find("POS3") != std::string::npos || source.find("POS2") != std::string::npos;
        if (uses_shorthand && source.find("#define MAT4") == std::string::npos) {
            injection +=
                "#define MAT4 mat4\n"
                "#define MAT3 mat3\n"
                "#define MAT2 mat2\n"
                "#define POS4 vec4\n"
                "#define POS3 vec3\n"
                "#define POS2 vec2\n";
        }
        if (!injection.empty()) {
            const size_t newline = source.find('\n');
            if (source.rfind("#version", 0) == 0 && newline != std::string::npos) {
                compile_source =
                    source.substr(0, newline + 1) + injection + source.substr(newline + 1);
            } else {
                compile_source = injection + source;
            }
        }
    }

    const CompileResult structural = validate_structure(compile_source, options.stage);
    result.success = structural.success;
    result.log = structural.log;
    result.error = structural.error;
    if (!structural.success) return result;
    if (!options.run_glslang) return result;

    const std::string validator = options.glslang_path.empty() ? find_glslang_validator()
                                                               : options.glslang_path;
    result.validator = validator;
    if (validator.empty() || !fs::is_regular_file(validator)) {
        result.validator_available = false;
        result.log += "; glslangValidator not found, structural validation only";
        return result;
    }
    result.validator_available = true;
    const std::string temp = fs::make_temp_dir("nexus-shader-");
    if (temp.empty()) {
        result.success = false;
        result.error = "cannot create temporary directory";
        return result;
    }
    const std::string ext = options.stage == ShaderStage::Vertex ? ".vert" : ".frag";
    const std::string path = fs::join(temp, "shader" + ext);
    if (!fs::write_file(path, compile_source)) {
        result.success = false;
        result.error = "cannot write temporary shader";
        fs::remove_all(temp);
        return result;
    }
    const std::string stage_arg = options.stage == ShaderStage::Vertex ? "vert" : "frag";
    const std::string command = "\"" + validator + "\" -S " + stage_arg + " \"" + path + "\" 2>&1";
#if defined(_WIN32)
    FILE* pipe = _popen(command.c_str(), "r");
#else
    FILE* pipe = popen(command.c_str(), "r");
#endif
    if (!pipe) {
        result.success = false;
        result.error = "cannot execute glslangValidator";
        fs::remove_all(temp);
        return result;
    }
    std::array<char, 4096> buffer{};
    std::string output;
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) output += buffer.data();
#if defined(_WIN32)
    const int status = _pclose(pipe);
    result.exit_code = status;
    result.success = status == 0;
#else
    const int status = pclose(pipe);
    result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    result.success = result.exit_code == 0;
#endif
    result.log = output;
    if (!result.success) result.error = "glslangValidator rejected the generated shader";
    fs::remove_all(temp);
    return result;
}

} // namespace nexus::shader
