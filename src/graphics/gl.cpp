#include <nexus/graphics/gl.hpp>

#include <sstream>

namespace nexus::graphics {

namespace {

std::string gl_string(GlApi* api, GLenum name) {
    if (api == nullptr || api->GetString == nullptr) return "";
    const GLubyte* value = api->GetString(name);
    return value != nullptr ? reinterpret_cast<const char*>(value) : "";
}

} // namespace

bool GlApi::load(const std::function<void*(const char*)>& loader, std::string& error) {
    error.clear();
    if (!loader) {
        error = "no GL loader function supplied";
        return false;
    }
    std::vector<std::string> missing;
#define NEXUS_LOAD_GL(member)                                                        \
    member = reinterpret_cast<decltype(member)>(loader("gl" #member));               \
    if (member == nullptr) missing.push_back("gl" #member);
    NEXUS_LOAD_GL(GetString)
    NEXUS_LOAD_GL(GetError)
    NEXUS_LOAD_GL(ClearColor)
    NEXUS_LOAD_GL(Clear)
    NEXUS_LOAD_GL(Viewport)
    NEXUS_LOAD_GL(Enable)
    NEXUS_LOAD_GL(Disable)
    NEXUS_LOAD_GL(BlendFunc)
    NEXUS_LOAD_GL(DepthFunc)
    NEXUS_LOAD_GL(CullFace)
    NEXUS_LOAD_GL(FrontFace)
    NEXUS_LOAD_GL(GenBuffers)
    NEXUS_LOAD_GL(DeleteBuffers)
    NEXUS_LOAD_GL(BindBuffer)
    NEXUS_LOAD_GL(BufferData)
    NEXUS_LOAD_GL(GenTextures)
    NEXUS_LOAD_GL(DeleteTextures)
    NEXUS_LOAD_GL(BindTexture)
    NEXUS_LOAD_GL(TexParameteri)
    NEXUS_LOAD_GL(TexImage2D)
    NEXUS_LOAD_GL(ActiveTexture)
    NEXUS_LOAD_GL(PixelStorei)
    NEXUS_LOAD_GL(CreateShader)
    NEXUS_LOAD_GL(ShaderSource)
    NEXUS_LOAD_GL(CompileShader)
    NEXUS_LOAD_GL(GetShaderiv)
    NEXUS_LOAD_GL(GetShaderInfoLog)
    NEXUS_LOAD_GL(DeleteShader)
    NEXUS_LOAD_GL(CreateProgram)
    NEXUS_LOAD_GL(AttachShader)
    NEXUS_LOAD_GL(LinkProgram)
    NEXUS_LOAD_GL(GetProgramiv)
    NEXUS_LOAD_GL(GetProgramInfoLog)
    NEXUS_LOAD_GL(UseProgram)
    NEXUS_LOAD_GL(DeleteProgram)
    NEXUS_LOAD_GL(GetUniformLocation)
    NEXUS_LOAD_GL(Uniform1i)
    NEXUS_LOAD_GL(Uniform1f)
    NEXUS_LOAD_GL(Uniform2f)
    NEXUS_LOAD_GL(Uniform3f)
    NEXUS_LOAD_GL(Uniform4f)
    NEXUS_LOAD_GL(UniformMatrix4fv)
    NEXUS_LOAD_GL(GetAttribLocation)
    NEXUS_LOAD_GL(BindAttribLocation)
    NEXUS_LOAD_GL(EnableVertexAttribArray)
    NEXUS_LOAD_GL(DisableVertexAttribArray)
    NEXUS_LOAD_GL(VertexAttribPointer)
    NEXUS_LOAD_GL(DrawArrays)
    NEXUS_LOAD_GL(DrawElements)
    NEXUS_LOAD_GL(GenFramebuffers)
    NEXUS_LOAD_GL(DeleteFramebuffers)
    NEXUS_LOAD_GL(BindFramebuffer)
    NEXUS_LOAD_GL(FramebufferTexture2D)
    NEXUS_LOAD_GL(CheckFramebufferStatus)
    NEXUS_LOAD_GL(ReadPixels)
    NEXUS_LOAD_GL(Finish)
#undef NEXUS_LOAD_GL
    if (!missing.empty()) {
        std::ostringstream out;
        out << "missing OpenGL entry points:";
        for (const auto& name : missing) out << " " << name;
        error = out.str();
        return false;
    }
    return true;
}

std::string GlApi::version() const {
    return gl_string(const_cast<GlApi*>(this), glc::kVersion);
}

std::string GlApi::renderer() const {
    return gl_string(const_cast<GlApi*>(this), glc::kRenderer);
}

std::string GlApi::info() const {
    std::ostringstream out;
    out << "renderer=" << renderer() << ", version=" << version();
    return out.str();
}

} // namespace nexus::graphics
