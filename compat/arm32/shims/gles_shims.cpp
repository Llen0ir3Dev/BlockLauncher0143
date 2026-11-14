#include <nexus/compat/arm32/gles_shims.hpp>
#include <nexus/core/log.hpp>
#include <EGL/egl.h>
#include <GL/gl.h>
#include <GL/glx.h>
#include <dlfcn.h>
#include <mutex>

namespace nexus::compat::arm32::gles {

static GLESFunctionTable s_gles_function_table{};
static EGLFunctionTable s_egl_function_table{};
static bool s_gles_initialized = false;
static bool s_egl_initialized = false;
static std::mutex s_init_mutex;

/// Resolve a symbol from the first shared object in `libs` that exports it.
///
/// The handle is intentionally kept open for the lifetime of the process:
/// once a function pointer is resolved, closing the owning library could
/// invalidate it. Keeping the handle open is cheap (the loader refcounts it).
template<typename T>
static T load_symbol_from_libraries(const char* const* libs, std::size_t count,
                                    const char* symbol) {
    for (std::size_t i = 0; i < count; ++i) {
        void* handle = dlopen(libs[i], RTLD_LAZY | RTLD_LOCAL);
        if (!handle) {
            continue;
        }
        void* ptr = dlsym(handle, symbol);
        if (ptr) {
            return reinterpret_cast<T>(ptr);
        }
        dlclose(handle);
    }
    return nullptr;
}

/// Load a GLES entry point from the host OpenGL ES implementation.
/// GLES must be resolved from libGLESv2 (never from libGL: desktop GL has
/// different semantics for several entry points).
template<typename T>
static T load_gles_function(const char* name) {
    static const char* const libs[] = {"libGLESv2.so.2", "libGLESv2.so"};
    return load_symbol_from_libraries<T>(libs, sizeof(libs) / sizeof(libs[0]), name);
}

/// Load an EGL entry point from the host EGL implementation.
/// EGL and GLES live in separate libraries and are kept strictly separate.
template<typename T>
static T load_egl_function(const char* name) {
    static const char* const libs[] = {"libEGL.so.1", "libEGL.so"};
    return load_symbol_from_libraries<T>(libs, sizeof(libs) / sizeof(libs[0]), name);
}

static bool load_all_gles_functions() {
    auto load_func = [&](auto GLESFunctionTable::* member, const char* name) -> bool {
        using MemberType = decltype(s_gles_function_table.*member);
        using FuncType = std::remove_reference_t<MemberType>;
        auto ptr = load_gles_function<FuncType>(name);
        if (!ptr) {
            NEXUS_LOG_WARN("GLES: Failed to load function: " + std::string(name));
            return false;
        }
        s_gles_function_table.*member = ptr;
        return true;
    };

    #define LOAD_GLES_FUNC(name) \
        if (!load_func(&GLESFunctionTable::name, #name)) return false;

    LOAD_GLES_FUNC(glActiveTexture)
    LOAD_GLES_FUNC(glAttachShader)
    LOAD_GLES_FUNC(glBindAttribLocation)
    LOAD_GLES_FUNC(glBindBuffer)
    LOAD_GLES_FUNC(glBindFramebuffer)
    LOAD_GLES_FUNC(glBindRenderbuffer)
    LOAD_GLES_FUNC(glBindTexture)
    LOAD_GLES_FUNC(glBlendColor)
    LOAD_GLES_FUNC(glBlendEquation)
    LOAD_GLES_FUNC(glBlendEquationSeparate)
    LOAD_GLES_FUNC(glBlendFunc)
    LOAD_GLES_FUNC(glBlendFuncSeparate)
    LOAD_GLES_FUNC(glBufferData)
    LOAD_GLES_FUNC(glBufferSubData)
    LOAD_GLES_FUNC(glCheckFramebufferStatus)
    LOAD_GLES_FUNC(glClear)
    LOAD_GLES_FUNC(glClearColor)
    LOAD_GLES_FUNC(glClearDepthf)
    LOAD_GLES_FUNC(glClearStencil)
    LOAD_GLES_FUNC(glColorMask)
    LOAD_GLES_FUNC(glCompileShader)
    LOAD_GLES_FUNC(glCompressedTexImage2D)
    LOAD_GLES_FUNC(glCompressedTexSubImage2D)
    LOAD_GLES_FUNC(glCopyTexImage2D)
    LOAD_GLES_FUNC(glCopyTexSubImage2D)
    LOAD_GLES_FUNC(glCreateProgram)
    LOAD_GLES_FUNC(glCreateShader)
    LOAD_GLES_FUNC(glCullFace)
    LOAD_GLES_FUNC(glDeleteBuffers)
    LOAD_GLES_FUNC(glDeleteFramebuffers)
    LOAD_GLES_FUNC(glDeleteProgram)
    LOAD_GLES_FUNC(glDeleteRenderbuffers)
    LOAD_GLES_FUNC(glDeleteShader)
    LOAD_GLES_FUNC(glDeleteTextures)
    LOAD_GLES_FUNC(glDepthFunc)
    LOAD_GLES_FUNC(glDepthMask)
    LOAD_GLES_FUNC(glDepthRangef)
    LOAD_GLES_FUNC(glDetachShader)
    LOAD_GLES_FUNC(glDisable)
    LOAD_GLES_FUNC(glDisableVertexAttribArray)
    LOAD_GLES_FUNC(glDrawArrays)
    LOAD_GLES_FUNC(glDrawElements)
    LOAD_GLES_FUNC(glEnable)
    LOAD_GLES_FUNC(glEnableVertexAttribArray)
    LOAD_GLES_FUNC(glFinish)
    LOAD_GLES_FUNC(glFlush)
    LOAD_GLES_FUNC(glFramebufferRenderbuffer)
    LOAD_GLES_FUNC(glFramebufferTexture2D)
    LOAD_GLES_FUNC(glFrontFace)
    LOAD_GLES_FUNC(glGenBuffers)
    LOAD_GLES_FUNC(glGenFramebuffers)
    LOAD_GLES_FUNC(glGenRenderbuffers)
    LOAD_GLES_FUNC(glGenTextures)
    LOAD_GLES_FUNC(glGenerateMipmap)
    LOAD_GLES_FUNC(glGetActiveAttrib)
    LOAD_GLES_FUNC(glGetActiveUniform)
    LOAD_GLES_FUNC(glGetAttachedShaders)
    LOAD_GLES_FUNC(glGetAttribLocation)
    LOAD_GLES_FUNC(glGetBooleanv)
    LOAD_GLES_FUNC(glGetBufferParameteriv)
    LOAD_GLES_FUNC(glGetError)
    LOAD_GLES_FUNC(glGetFloatv)
    LOAD_GLES_FUNC(glGetFramebufferAttachmentParameteriv)
    LOAD_GLES_FUNC(glGetIntegerv)
    LOAD_GLES_FUNC(glGetProgramiv)
    LOAD_GLES_FUNC(glGetProgramInfoLog)
    LOAD_GLES_FUNC(glGetRenderbufferParameteriv)
    LOAD_GLES_FUNC(glGetShaderiv)
    LOAD_GLES_FUNC(glGetShaderInfoLog)
    LOAD_GLES_FUNC(glGetShaderPrecisionFormat)
    LOAD_GLES_FUNC(glGetShaderSource)
    LOAD_GLES_FUNC(glGetString)
    LOAD_GLES_FUNC(glGetTexParameterfv)
    LOAD_GLES_FUNC(glGetTexParameteriv)
    LOAD_GLES_FUNC(glGetUniformfv)
    LOAD_GLES_FUNC(glGetUniformiv)
    LOAD_GLES_FUNC(glGetUniformLocation)
    LOAD_GLES_FUNC(glGetVertexAttribfv)
    LOAD_GLES_FUNC(glGetVertexAttribiv)
    LOAD_GLES_FUNC(glGetVertexAttribPointerv)
    LOAD_GLES_FUNC(glHint)
    LOAD_GLES_FUNC(glIsBuffer)
    LOAD_GLES_FUNC(glIsEnabled)
    LOAD_GLES_FUNC(glIsFramebuffer)
    LOAD_GLES_FUNC(glIsProgram)
    LOAD_GLES_FUNC(glIsRenderbuffer)
    LOAD_GLES_FUNC(glIsShader)
    LOAD_GLES_FUNC(glIsTexture)
    LOAD_GLES_FUNC(glLineWidth)
    LOAD_GLES_FUNC(glLinkProgram)
    LOAD_GLES_FUNC(glPixelStorei)
    LOAD_GLES_FUNC(glPolygonOffset)
    LOAD_GLES_FUNC(glReadPixels)
    LOAD_GLES_FUNC(glReleaseShaderCompiler)
    LOAD_GLES_FUNC(glRenderbufferStorage)
    LOAD_GLES_FUNC(glSampleCoverage)
    LOAD_GLES_FUNC(glScissor)
    LOAD_GLES_FUNC(glShaderBinary)
    LOAD_GLES_FUNC(glShaderSource)
    LOAD_GLES_FUNC(glStencilFunc)
    LOAD_GLES_FUNC(glStencilFuncSeparate)
    LOAD_GLES_FUNC(glStencilMask)
    LOAD_GLES_FUNC(glStencilMaskSeparate)
    LOAD_GLES_FUNC(glStencilOp)
    LOAD_GLES_FUNC(glStencilOpSeparate)
    LOAD_GLES_FUNC(glTexImage2D)
    LOAD_GLES_FUNC(glTexParameterf)
    LOAD_GLES_FUNC(glTexParameterfv)
    LOAD_GLES_FUNC(glTexParameteri)
    LOAD_GLES_FUNC(glTexParameteriv)
    LOAD_GLES_FUNC(glTexSubImage2D)
    LOAD_GLES_FUNC(glUniform1f)
    LOAD_GLES_FUNC(glUniform1fv)
    LOAD_GLES_FUNC(glUniform1i)
    LOAD_GLES_FUNC(glUniform1iv)
    LOAD_GLES_FUNC(glUniform2f)
    LOAD_GLES_FUNC(glUniform2fv)
    LOAD_GLES_FUNC(glUniform2i)
    LOAD_GLES_FUNC(glUniform2iv)
    LOAD_GLES_FUNC(glUniform3f)
    LOAD_GLES_FUNC(glUniform3fv)
    LOAD_GLES_FUNC(glUniform3i)
    LOAD_GLES_FUNC(glUniform3iv)
    LOAD_GLES_FUNC(glUniform4f)
    LOAD_GLES_FUNC(glUniform4fv)
    LOAD_GLES_FUNC(glUniform4i)
    LOAD_GLES_FUNC(glUniform4iv)
    LOAD_GLES_FUNC(glUniformMatrix2fv)
    LOAD_GLES_FUNC(glUniformMatrix3fv)
    LOAD_GLES_FUNC(glUniformMatrix4fv)
    LOAD_GLES_FUNC(glUseProgram)
    LOAD_GLES_FUNC(glValidateProgram)
    LOAD_GLES_FUNC(glVertexAttrib1f)
    LOAD_GLES_FUNC(glVertexAttrib1fv)
    LOAD_GLES_FUNC(glVertexAttrib2f)
    LOAD_GLES_FUNC(glVertexAttrib2fv)
    LOAD_GLES_FUNC(glVertexAttrib3f)
    LOAD_GLES_FUNC(glVertexAttrib3fv)
    LOAD_GLES_FUNC(glVertexAttrib4f)
    LOAD_GLES_FUNC(glVertexAttrib4fv)
    LOAD_GLES_FUNC(glVertexAttribPointer)
    LOAD_GLES_FUNC(glViewport)

    return true;
}

static bool load_all_egl_functions() {
    auto load_func = [&](auto EGLFunctionTable::* member, const char* name) -> bool {
        using MemberType = decltype(s_egl_function_table.*member);
        using FuncType = std::remove_reference_t<MemberType>;
        auto ptr = load_egl_function<FuncType>(name);
        if (!ptr) {
            NEXUS_LOG_WARN("EGL: Failed to load function: " + std::string(name));
            return false;
        }
        s_egl_function_table.*member = ptr;
        return true;
    };

    #define LOAD_EGL_FUNC(name) \
        if (!load_func(&EGLFunctionTable::name, #name)) return false;

    LOAD_EGL_FUNC(eglGetError)
    LOAD_EGL_FUNC(eglGetDisplay)
    LOAD_EGL_FUNC(eglInitialize)
    LOAD_EGL_FUNC(eglTerminate)
    LOAD_EGL_FUNC(eglQueryString)
    LOAD_EGL_FUNC(eglGetConfigs)
    LOAD_EGL_FUNC(eglChooseConfig)
    LOAD_EGL_FUNC(eglGetConfigAttrib)
    LOAD_EGL_FUNC(eglCreateWindowSurface)
    LOAD_EGL_FUNC(eglCreatePbufferSurface)
    LOAD_EGL_FUNC(eglDestroySurface)
    LOAD_EGL_FUNC(eglQuerySurface)
    LOAD_EGL_FUNC(eglBindAPI)
    LOAD_EGL_FUNC(eglQueryAPI)
    LOAD_EGL_FUNC(eglWaitClient)
    LOAD_EGL_FUNC(eglWaitGL)
    LOAD_EGL_FUNC(eglWaitNative)
    LOAD_EGL_FUNC(eglSwapBuffers)
    LOAD_EGL_FUNC(eglSwapInterval)
    LOAD_EGL_FUNC(eglCreateContext)
    LOAD_EGL_FUNC(eglDestroyContext)
    LOAD_EGL_FUNC(eglMakeCurrent)
    LOAD_EGL_FUNC(eglGetCurrentContext)
    LOAD_EGL_FUNC(eglGetCurrentSurface)
    LOAD_EGL_FUNC(eglGetCurrentDisplay)
    LOAD_EGL_FUNC(eglQueryContext)

    return true;
}

} // namespace nexus::compat::arm32::gles

// Global shim functions that provide access to the namespace's static variables
const GLESFunctionTable* nexus_gles_get_function_table() {
    return &nexus::compat::arm32::gles::s_gles_function_table;
}

const EGLFunctionTable* nexus_egl_get_function_table() {
    return &nexus::compat::arm32::gles::s_egl_function_table;
}

bool nexus_gles_shims_init() {
    std::lock_guard<std::mutex> lock(nexus::compat::arm32::gles::s_init_mutex);
    if (nexus::compat::arm32::gles::s_gles_initialized) {
        return true;
    }
    
    if (!nexus::compat::arm32::gles::load_all_gles_functions()) {
        NEXUS_LOG_ERROR("GLES: Failed to load all OpenGL ES functions");
        return false;
    }
    
    nexus::compat::arm32::gles::s_gles_initialized = true;
    NEXUS_LOG_INFO("GLES shims initialized successfully");
    return true;
}

bool nexus_egl_shims_init() {
    std::lock_guard<std::mutex> lock(nexus::compat::arm32::gles::s_init_mutex);
    if (nexus::compat::arm32::gles::s_egl_initialized) {
        return true;
    }
    
    if (!nexus::compat::arm32::gles::load_all_egl_functions()) {
        NEXUS_LOG_ERROR("EGL: Failed to load all EGL functions");
        return false;
    }
    
    nexus::compat::arm32::gles::s_egl_initialized = true;
    NEXUS_LOG_INFO("EGL shims initialized successfully");
    return true;
}

void nexus_gles_shims_shutdown() {
    std::lock_guard<std::mutex> lock(nexus::compat::arm32::gles::s_init_mutex);
    nexus::compat::arm32::gles::s_gles_initialized = false;
}

void nexus_egl_shims_shutdown() {
    std::lock_guard<std::mutex> lock(nexus::compat::arm32::gles::s_init_mutex);
    nexus::compat::arm32::gles::s_egl_initialized = false;
}

bool nexus_gles_shims_is_initialized() {
    return nexus::compat::arm32::gles::s_gles_initialized;
}

bool nexus_egl_shims_is_initialized() {
    return nexus::compat::arm32::gles::s_egl_initialized;
}