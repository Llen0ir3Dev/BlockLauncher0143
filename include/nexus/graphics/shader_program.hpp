#pragma once

#include <map>
#include <string>

#include <nexus/graphics/gl.hpp>

namespace nexus::graphics {

// Compiles and links a GLSL program through the loaded GL entry points.  The
// class deliberately keeps the full compiler log: shader failures must be
// visible to the launcher and to logs, not silently replaced by a fallback.
class ShaderProgram {
public:
    explicit ShaderProgram(GlApi& api) : api_(api) {}
    ~ShaderProgram();
    ShaderProgram(const ShaderProgram&) = delete;
    ShaderProgram& operator=(const ShaderProgram&) = delete;

    bool build(const std::string& vertex_source, const std::string& fragment_source,
               std::string& error);
    void release();
    bool valid() const { return program_ != 0; }
    GLuint id() const { return program_; }
    void use() const;

    GLint uniform(const std::string& name);
    GLint attribute(const std::string& name) const;

    bool set_int(const std::string& name, int value);
    bool set_float(const std::string& name, float value);
    bool set_vec2(const std::string& name, float x, float y);
    bool set_vec3(const std::string& name, float x, float y, float z);
    bool set_vec4(const std::string& name, float x, float y, float z, float w);
    bool set_mat4(const std::string& name, const float* values);

    const std::string& log() const { return log_; }

private:
    GLint require_uniform(const std::string& name);

    GlApi& api_;
    GLuint program_ = 0;
    std::map<std::string, GLint> uniform_cache_;
    std::string log_;
};

} // namespace nexus::graphics
