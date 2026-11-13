#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nexus/graphics/gl.hpp>
#include <nexus/graphics/shader_program.hpp>

namespace nexus::graphics {

struct RendererOptions {
    bool nearest_filter = true;
    bool depth_test = false;
    bool blend = false;
};

// Small GL renderer used by the NEXUS runtime shell to prove that translated
// shaders compile, link and draw through the host OpenGL context.  It is not a
// substitute for the original game renderer.
class Renderer {
public:
    explicit Renderer(GlApi& api, RendererOptions options = {});
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool initialize(const std::string& vertex_source, const std::string& fragment_source,
                    std::string& error);
    void shutdown();
    bool valid() const { return program_.valid(); }

    bool set_texture_rgba(uint32_t width, uint32_t height, const std::vector<uint8_t>& rgba,
                          std::string& error);
    bool create_checker_texture(uint32_t size, std::string& error);
    void render(double time_seconds, int width, int height);
    bool capture_png(const std::string& path, int width, int height, std::string& error);

    ShaderProgram& program() { return program_; }
    const std::string& shader_log() const { return program_.log(); }

private:
    bool upload_geometry(std::string& error);

    GlApi& api_;
    RendererOptions options_;
    ShaderProgram program_;
    GLuint vbo_ = 0;
    GLuint ibo_ = 0;
    GLuint texture_ = 0;
    uint32_t texture_width_ = 0;
    uint32_t texture_height_ = 0;
};

} // namespace nexus::graphics
