#include <nexus/graphics/shader_program.hpp>

#include <vector>

namespace nexus::graphics {

namespace {

bool compile_stage(GlApi& api, GLenum type, const std::string& source, GLuint& shader,
                   std::string& log) {
    shader = api.CreateShader(type);
    if (shader == 0) {
        log = "glCreateShader failed";
        return false;
    }
    const GLchar* text = source.c_str();
    const GLint length = static_cast<GLint>(source.size());
    api.ShaderSource(shader, 1, &text, &length);
    api.CompileShader(shader);
    GLint status = 0;
    api.GetShaderiv(shader, glc::kCompileStatus, &status);
    GLint log_length = 0;
    api.GetShaderiv(shader, glc::kInfoLogLength, &log_length);
    if (log_length > 0) {
        std::vector<char> buffer(static_cast<size_t>(log_length) + 1, '\0');
        GLsizei written = 0;
        api.GetShaderInfoLog(shader, log_length, &written, buffer.data());
        log.assign(buffer.data(), static_cast<size_t>(written));
    }
    if (status == 0) {
        api.DeleteShader(shader);
        shader = 0;
        return false;
    }
    return true;
}

} // namespace

ShaderProgram::~ShaderProgram() {
    release();
}

void ShaderProgram::release() {
    if (program_ != 0 && api_.DeleteProgram != nullptr) {
        api_.DeleteProgram(program_);
        program_ = 0;
    }
    uniform_cache_.clear();
}

bool ShaderProgram::build(const std::string& vertex_source,
                          const std::string& fragment_source, std::string& error) {
    release();
    if (!api_.loaded()) {
        error = "OpenGL entry points are not loaded";
        return false;
    }
    GLuint vertex = 0;
    GLuint fragment = 0;
    std::string vertex_log;
    std::string fragment_log;
    if (!compile_stage(api_, glc::kVertexShader, vertex_source, vertex, vertex_log)) {
        error = "vertex shader compilation failed:\n" + vertex_log;
        log_ = vertex_log;
        return false;
    }
    if (!compile_stage(api_, glc::kFragmentShader, fragment_source, fragment, fragment_log)) {
        api_.DeleteShader(vertex);
        error = "fragment shader compilation failed:\n" + fragment_log;
        log_ = fragment_log;
        return false;
    }
    program_ = api_.CreateProgram();
    if (program_ == 0) {
        api_.DeleteShader(vertex);
        api_.DeleteShader(fragment);
        error = "glCreateProgram failed";
        return false;
    }
    api_.AttachShader(program_, vertex);
    api_.AttachShader(program_, fragment);
    api_.BindAttribLocation(program_, 0, "a_position");
    api_.BindAttribLocation(program_, 1, "a_texcoord");
    api_.LinkProgram(program_);
    GLint status = 0;
    api_.GetProgramiv(program_, glc::kLinkStatus, &status);
    GLint log_length = 0;
    api_.GetProgramiv(program_, glc::kInfoLogLength, &log_length);
    if (log_length > 0) {
        std::vector<char> buffer(static_cast<size_t>(log_length) + 1, '\0');
        GLsizei written = 0;
        api_.GetProgramInfoLog(program_, log_length, &written, buffer.data());
        log_.assign(buffer.data(), static_cast<size_t>(written));
    } else {
        log_.clear();
    }
    api_.DeleteShader(vertex);
    api_.DeleteShader(fragment);
    if (status == 0) {
        api_.DeleteProgram(program_);
        program_ = 0;
        error = "shader link failed:\n" + log_;
        return false;
    }
    return true;
}

void ShaderProgram::use() const {
    if (program_ != 0) api_.UseProgram(program_);
}

GLint ShaderProgram::require_uniform(const std::string& name) {
    const auto it = uniform_cache_.find(name);
    if (it != uniform_cache_.end()) return it->second;
    const GLint location = api_.GetUniformLocation(program_, name.c_str());
    uniform_cache_[name] = location;
    return location;
}

GLint ShaderProgram::uniform(const std::string& name) {
    return require_uniform(name);
}

GLint ShaderProgram::attribute(const std::string& name) const {
    if (program_ == 0) return -1;
    return api_.GetAttribLocation(program_, name.c_str());
}

bool ShaderProgram::set_int(const std::string& name, int value) {
    const GLint location = require_uniform(name);
    if (location < 0) return false;
    api_.Uniform1i(location, value);
    return true;
}

bool ShaderProgram::set_float(const std::string& name, float value) {
    const GLint location = require_uniform(name);
    if (location < 0) return false;
    api_.Uniform1f(location, value);
    return true;
}

bool ShaderProgram::set_vec2(const std::string& name, float x, float y) {
    const GLint location = require_uniform(name);
    if (location < 0) return false;
    api_.Uniform2f(location, x, y);
    return true;
}

bool ShaderProgram::set_vec3(const std::string& name, float x, float y, float z) {
    const GLint location = require_uniform(name);
    if (location < 0) return false;
    api_.Uniform3f(location, x, y, z);
    return true;
}

bool ShaderProgram::set_vec4(const std::string& name, float x, float y, float z, float w) {
    const GLint location = require_uniform(name);
    if (location < 0) return false;
    api_.Uniform4f(location, x, y, z, w);
    return true;
}

bool ShaderProgram::set_mat4(const std::string& name, const float* values) {
    const GLint location = require_uniform(name);
    if (location < 0) return false;
    api_.UniformMatrix4fv(location, 1, 0, values);
    return true;
}

} // namespace nexus::graphics
