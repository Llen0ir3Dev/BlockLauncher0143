#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace nexus::graphics {

using GLenum = unsigned int;
using GLuint = unsigned int;
using GLint = int;
using GLsizei = int;
using GLfloat = float;
using GLboolean = unsigned char;
using GLchar = char;
using GLbitfield = unsigned int;
using GLubyte = unsigned char;
using GLushort = unsigned short;
using GLsizeiptr = std::ptrdiff_t;
using GLintptr = std::ptrdiff_t;

// Minimal GL constants used by the integration layer.  Keeping the loader
// independent of GL headers lets the same source build for desktop GL and
// GLES contexts through SDL_GL_GetProcAddress.
namespace glc {
constexpr GLenum kVertexShader = 0x8B31;
constexpr GLenum kFragmentShader = 0x8B30;
constexpr GLenum kCompileStatus = 0x8B81;
constexpr GLenum kLinkStatus = 0x8B82;
constexpr GLenum kInfoLogLength = 0x8B84;
constexpr GLenum kArrayBuffer = 0x8892;
constexpr GLenum kElementArrayBuffer = 0x8893;
constexpr GLenum kStaticDraw = 0x88E4;
constexpr GLenum kFloat = 0x1406;
constexpr GLenum kUnsignedShort = 0x1403;
constexpr GLenum kTriangles = 0x0004;
constexpr GLenum kTexture2D = 0x0DE1;
constexpr GLenum kRgba = 0x1908;
constexpr GLenum kUnsignedByte = 0x1401;
constexpr GLenum kTextureMinFilter = 0x2801;
constexpr GLenum kTextureMagFilter = 0x2800;
constexpr GLenum kLinear = 0x2601;
constexpr GLenum kNearest = 0x2600;
constexpr GLenum kClampToEdge = 0x812F;
constexpr GLenum kTextureWrapS = 0x2802;
constexpr GLenum kTextureWrapT = 0x2803;
constexpr GLenum kColorBufferBit = 0x4000;
constexpr GLenum kDepthBufferBit = 0x0100;
constexpr GLenum kTexture0 = 0x84C0;
constexpr GLenum kBlend = 0x0BE2;
constexpr GLenum kSrcAlpha = 0x0302;
constexpr GLenum kOneMinusSrcAlpha = 0x0303;
constexpr GLenum kDepthTest = 0x0B71;
constexpr GLenum kLequal = 0x0203;
constexpr GLenum kCullFace = 0x0B44;
constexpr GLenum kBack = 0x0405;
constexpr GLenum kCcw = 0x0901;
constexpr GLenum kFramebuffer = 0x8D40;
constexpr GLenum kColorAttachment0 = 0x8CE0;
constexpr GLenum kFramebufferComplete = 0x8CD5;
constexpr GLenum kRgba8 = 0x8058;
constexpr GLenum kPackAlignment = 0x0D05;
constexpr GLenum kVersion = 0x1F02;
constexpr GLenum kRenderer = 0x1F01;
constexpr GLenum kShadingLanguageVersion = 0x8B8C;
constexpr GLenum kNoError = 0;
constexpr GLenum kFramebufferBinding = 0x8CA6;
} // namespace glc

// OpenGL entry points loaded through the platform layer.  Every member is null
// until load() succeeds.  No global function pointers are used, so multiple
// contexts can be represented explicitly.
struct GlApi {
    const GLubyte* (*GetString)(GLenum) = nullptr;
    GLenum (*GetError)() = nullptr;
    void (*ClearColor)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*Clear)(GLbitfield) = nullptr;
    void (*Viewport)(GLint, GLint, GLsizei, GLsizei) = nullptr;
    void (*Enable)(GLenum) = nullptr;
    void (*Disable)(GLenum) = nullptr;
    void (*BlendFunc)(GLenum, GLenum) = nullptr;
    void (*DepthFunc)(GLenum) = nullptr;
    void (*CullFace)(GLenum) = nullptr;
    void (*FrontFace)(GLenum) = nullptr;
    void (*GenBuffers)(GLsizei, GLuint*) = nullptr;
    void (*DeleteBuffers)(GLsizei, const GLuint*) = nullptr;
    void (*BindBuffer)(GLenum, GLuint) = nullptr;
    void (*BufferData)(GLenum, GLsizeiptr, const void*, GLenum) = nullptr;
    void (*GenTextures)(GLsizei, GLuint*) = nullptr;
    void (*DeleteTextures)(GLsizei, const GLuint*) = nullptr;
    void (*BindTexture)(GLenum, GLuint) = nullptr;
    void (*TexParameteri)(GLenum, GLenum, GLint) = nullptr;
    void (*TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                       const void*) = nullptr;
    void (*ActiveTexture)(GLenum) = nullptr;
    void (*PixelStorei)(GLenum, GLint) = nullptr;
    GLuint (*CreateShader)(GLenum) = nullptr;
    void (*ShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*) = nullptr;
    void (*CompileShader)(GLuint) = nullptr;
    void (*GetShaderiv)(GLuint, GLenum, GLint*) = nullptr;
    void (*GetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*) = nullptr;
    void (*DeleteShader)(GLuint) = nullptr;
    GLuint (*CreateProgram)() = nullptr;
    void (*AttachShader)(GLuint, GLuint) = nullptr;
    void (*LinkProgram)(GLuint) = nullptr;
    void (*GetProgramiv)(GLuint, GLenum, GLint*) = nullptr;
    void (*GetProgramInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*) = nullptr;
    void (*UseProgram)(GLuint) = nullptr;
    void (*DeleteProgram)(GLuint) = nullptr;
    GLint (*GetUniformLocation)(GLuint, const GLchar*) = nullptr;
    void (*Uniform1i)(GLint, GLint) = nullptr;
    void (*Uniform1f)(GLint, GLfloat) = nullptr;
    void (*Uniform2f)(GLint, GLfloat, GLfloat) = nullptr;
    void (*Uniform3f)(GLint, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*Uniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*UniformMatrix4fv)(GLint, GLsizei, GLboolean, const GLfloat*) = nullptr;
    GLint (*GetAttribLocation)(GLuint, const GLchar*) = nullptr;
    void (*BindAttribLocation)(GLuint, GLuint, const GLchar*) = nullptr;
    void (*EnableVertexAttribArray)(GLuint) = nullptr;
    void (*DisableVertexAttribArray)(GLuint) = nullptr;
    void (*VertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*) = nullptr;
    void (*DrawArrays)(GLenum, GLint, GLsizei) = nullptr;
    void (*DrawElements)(GLenum, GLsizei, GLenum, const void*) = nullptr;
    void (*GenFramebuffers)(GLsizei, GLuint*) = nullptr;
    void (*DeleteFramebuffers)(GLsizei, const GLuint*) = nullptr;
    void (*BindFramebuffer)(GLenum, GLuint) = nullptr;
    void (*FramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint) = nullptr;
    GLenum (*CheckFramebufferStatus)(GLenum) = nullptr;
    void (*ReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) = nullptr;
    void (*Finish)() = nullptr;

    bool load(const std::function<void*(const char*)>& loader, std::string& error);
    bool loaded() const { return CreateProgram != nullptr && DrawArrays != nullptr; }
    std::string info() const;
    std::string version() const;
    std::string renderer() const;
};

} // namespace nexus::graphics
