#include <nexus/graphics/renderer.hpp>

#include <algorithm>
#include <cstring>

#include <png.h>

#include <nexus/core/fs.hpp>

namespace nexus::graphics {

namespace {

constexpr int kVertexAttribute = 0;
constexpr int kTexcoordAttribute = 1;

struct Vertex {
    float x;
    float y;
    float u;
    float v;
};

} // namespace

Renderer::Renderer(GlApi& api, RendererOptions options)
    : api_(api), options_(options), program_(api) {}

Renderer::~Renderer() {
    shutdown();
}

bool Renderer::initialize(const std::string& vertex_source,
                          const std::string& fragment_source, std::string& error) {
    if (!program_.build(vertex_source, fragment_source, error)) return false;
    return upload_geometry(error);
}

bool Renderer::upload_geometry(std::string& error) {
    const Vertex vertices[] = {
        {-1.0f, -1.0f, 0.0f, 1.0f},
        { 1.0f, -1.0f, 1.0f, 1.0f},
        { 1.0f,  1.0f, 1.0f, 0.0f},
        {-1.0f,  1.0f, 0.0f, 0.0f}
    };
    const unsigned short indices[] = {0, 1, 2, 0, 2, 3};
    api_.GenBuffers(1, &vbo_);
    api_.GenBuffers(1, &ibo_);
    if (vbo_ == 0 || ibo_ == 0) {
        error = "glGenBuffers failed";
        return false;
    }
    api_.BindBuffer(glc::kArrayBuffer, vbo_);
    api_.BufferData(glc::kArrayBuffer, sizeof(vertices), vertices, glc::kStaticDraw);
    api_.BindBuffer(glc::kElementArrayBuffer, ibo_);
    api_.BufferData(glc::kElementArrayBuffer, sizeof(indices), indices, glc::kStaticDraw);
    api_.BindBuffer(glc::kArrayBuffer, 0);
    api_.BindBuffer(glc::kElementArrayBuffer, 0);
    return true;
}

void Renderer::shutdown() {
    if (texture_ != 0) {
        api_.DeleteTextures(1, &texture_);
        texture_ = 0;
    }
    if (vbo_ != 0) {
        api_.DeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }
    if (ibo_ != 0) {
        api_.DeleteBuffers(1, &ibo_);
        ibo_ = 0;
    }
    program_.release();
}

bool Renderer::set_texture_rgba(uint32_t width, uint32_t height,
                                const std::vector<uint8_t>& rgba, std::string& error) {
    if (width == 0 || height == 0) {
        error = "texture dimensions must be non-zero";
        return false;
    }
    if (rgba.size() < static_cast<size_t>(width) * height * 4) {
        error = "texture data is smaller than width*height*4";
        return false;
    }
    if (texture_ == 0) api_.GenTextures(1, &texture_);
    if (texture_ == 0) {
        error = "glGenTextures failed";
        return false;
    }
    api_.BindTexture(glc::kTexture2D, texture_);
    api_.PixelStorei(glc::kPackAlignment, 1);
    api_.TexImage2D(glc::kTexture2D, 0, static_cast<GLint>(glc::kRgba),
                    static_cast<GLsizei>(width), static_cast<GLsizei>(height), 0,
                    glc::kRgba, glc::kUnsignedByte, rgba.data());
    const GLint filter = options_.nearest_filter ? static_cast<GLint>(glc::kNearest)
                                                 : static_cast<GLint>(glc::kLinear);
    api_.TexParameteri(glc::kTexture2D, glc::kTextureMinFilter, filter);
    api_.TexParameteri(glc::kTexture2D, glc::kTextureMagFilter, filter);
    api_.TexParameteri(glc::kTexture2D, glc::kTextureWrapS,
                       static_cast<GLint>(glc::kClampToEdge));
    api_.TexParameteri(glc::kTexture2D, glc::kTextureWrapT,
                       static_cast<GLint>(glc::kClampToEdge));
    api_.BindTexture(glc::kTexture2D, 0);
    texture_width_ = width;
    texture_height_ = height;
    if (api_.GetError() != glc::kNoError) {
        error = "OpenGL reported an error while uploading the texture";
        return false;
    }
    return true;
}

bool Renderer::create_checker_texture(uint32_t size, std::string& error) {
    if (size == 0) size = 64;
    std::vector<uint8_t> data(static_cast<size_t>(size) * size * 4, 255);
    for (uint32_t y = 0; y < size; ++y) {
        for (uint32_t x = 0; x < size; ++x) {
            const bool dark = ((x / 8) + (y / 8)) % 2 == 0;
            const size_t index = (static_cast<size_t>(y) * size + x) * 4;
            const uint8_t base = dark ? 48 : 210;
            data[index + 0] = static_cast<uint8_t>(base / 2);
            data[index + 1] = static_cast<uint8_t>(base);
            data[index + 2] = static_cast<uint8_t>(std::min(255, base + 24));
            data[index + 3] = 255;
        }
    }
    return set_texture_rgba(size, size, data, error);
}

void Renderer::render(double time_seconds, int width, int height) {
    if (!program_.valid()) return;
    api_.Viewport(0, 0, width, height);
    api_.ClearColor(0.05f, 0.06f, 0.09f, 1.0f);
    api_.Clear(glc::kColorBufferBit | (options_.depth_test ? glc::kDepthBufferBit : 0));
    if (options_.depth_test) api_.Enable(glc::kDepthTest);
    else api_.Disable(glc::kDepthTest);
    if (options_.blend) {
        api_.Enable(glc::kBlend);
        api_.BlendFunc(glc::kSrcAlpha, glc::kOneMinusSrcAlpha);
    } else {
        api_.Disable(glc::kBlend);
    }
    program_.use();
    program_.set_float("u_time", static_cast<float>(time_seconds));
    program_.set_vec2("u_resolution", static_cast<float>(width), static_cast<float>(height));
    program_.set_int("TEXTURE_0", 0);
    program_.set_int("texture0", 0);
    api_.ActiveTexture(glc::kTexture0);
    api_.BindTexture(glc::kTexture2D, texture_);
    api_.BindBuffer(glc::kArrayBuffer, vbo_);
    api_.BindBuffer(glc::kElementArrayBuffer, ibo_);
    api_.EnableVertexAttribArray(kVertexAttribute);
    api_.EnableVertexAttribArray(kTexcoordAttribute);
    api_.VertexAttribPointer(kVertexAttribute, 2, glc::kFloat, 0, sizeof(Vertex),
                             reinterpret_cast<const void*>(offsetof(Vertex, x)));
    api_.VertexAttribPointer(kTexcoordAttribute, 2, glc::kFloat, 0, sizeof(Vertex),
                             reinterpret_cast<const void*>(offsetof(Vertex, u)));
    api_.DrawElements(glc::kTriangles, 6, glc::kUnsignedShort, nullptr);
    api_.DisableVertexAttribArray(kVertexAttribute);
    api_.DisableVertexAttribArray(kTexcoordAttribute);
    api_.BindBuffer(glc::kArrayBuffer, 0);
    api_.BindBuffer(glc::kElementArrayBuffer, 0);
}

bool Renderer::capture_png(const std::string& path, int width, int height, std::string& error) {
    if (width <= 0 || height <= 0) {
        error = "screenshot dimensions must be positive";
        return false;
    }
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
    api_.PixelStorei(glc::kPackAlignment, 1);
    api_.ReadPixels(0, 0, width, height, glc::kRgba, glc::kUnsignedByte, pixels.data());
    api_.Finish();
    // OpenGL origin is bottom-left; PNG is top-down.
    std::vector<uint8_t> flipped(pixels.size());
    const size_t stride = static_cast<size_t>(width) * 4;
    for (int y = 0; y < height; ++y) {
        std::memcpy(flipped.data() + static_cast<size_t>(y) * stride,
                    pixels.data() + static_cast<size_t>(height - 1 - y) * stride, stride);
    }
    if (!fs::ensure_parent(path)) {
        error = "cannot create screenshot directory for " + path;
        return false;
    }
    png_image image;
    std::memset(&image, 0, sizeof(image));
    image.version = PNG_IMAGE_VERSION;
    image.width = static_cast<png_uint_32>(width);
    image.height = static_cast<png_uint_32>(height);
    image.format = PNG_FORMAT_RGBA;
    if (!png_image_write_to_file(&image, path.c_str(), 0, flipped.data(), 0, nullptr)) {
        error = std::string("PNG write failed: ") + image.message;
        png_image_free(&image);
        return false;
    }
    png_image_free(&image);
    return true;
}

} // namespace nexus::graphics
