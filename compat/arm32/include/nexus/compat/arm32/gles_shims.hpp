#pragma once

/// OpenGL ES 1.1 / 2.0 and EGL shims for ARM32 Minecraft PE
/// These provide the GLES/EGL API that libminecraftpe.so expects,
/// implemented on top of host OpenGL/GLX/EGL.
///
/// This header provides ARM32-compatible type definitions and function
/// declarations that can be used when cross-compiling for ARM32 or when
/// providing ARM32-compatible shims on x86_64.

#ifndef NEXUS_GLES_SHIMS_HPP
#define NEXUS_GLES_SHIMS_HPP

#include <cstddef>
#include <cstdint>

/// Include official EGL and GLES headers to get standard typedefs
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

/// OpenGL ES 1.1 constants (subset needed by Minecraft PE 0.14.3)
#ifndef GL_ZERO
#define GL_ZERO 0
#endif
#ifndef GL_ONE
#define GL_ONE 1
#endif
#ifndef GL_SRC_COLOR
#define GL_SRC_COLOR 0x0300
#endif
#ifndef GL_ONE_MINUS_SRC_COLOR
#define GL_ONE_MINUS_SRC_COLOR 0x0301
#endif
#ifndef GL_SRC_ALPHA
#define GL_SRC_ALPHA 0x0302
#endif
#ifndef GL_ONE_MINUS_SRC_ALPHA
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#endif
#ifndef GL_DST_ALPHA
#define GL_DST_ALPHA 0x0304
#endif
#ifndef GL_ONE_MINUS_DST_ALPHA
#define GL_ONE_MINUS_DST_ALPHA 0x0305
#endif
#ifndef GL_DST_COLOR
#define GL_DST_COLOR 0x0306
#endif
#ifndef GL_ONE_MINUS_DST_COLOR
#define GL_ONE_MINUS_DST_COLOR 0x0307
#endif
#ifndef GL_SRC_ALPHA_SATURATE
#define GL_SRC_ALPHA_SATURATE 0x0308
#endif

#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER 0x8B30
#endif
#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER 0x8B31
#endif
#ifndef GL_COMPILE_STATUS
#define GL_COMPILE_STATUS 0x8B81
#endif
#ifndef GL_LINK_STATUS
#define GL_LINK_STATUS 0x8B82
#endif
#ifndef GL_VALIDATE_STATUS
#define GL_VALIDATE_STATUS 0x8B83
#endif
#ifndef GL_INFO_LOG_LENGTH
#define GL_INFO_LOG_LENGTH 0x8B84
#endif
#ifndef GL_SHADER_SOURCE_LENGTH
#define GL_SHADER_SOURCE_LENGTH 0x8B88
#endif

#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER 0x8892
#endif
#ifndef GL_ELEMENT_ARRAY_BUFFER
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#endif
#ifndef GL_ARRAY_BUFFER_BINDING
#define GL_ARRAY_BUFFER_BINDING 0x8894
#endif
#ifndef GL_ELEMENT_ARRAY_BUFFER_BINDING
#define GL_ELEMENT_ARRAY_BUFFER_BINDING 0x8895
#endif
#ifndef GL_VERTEX_ARRAY_BINDING
#define GL_VERTEX_ARRAY_BINDING 0x85B5
#endif

#ifndef GL_STATIC_DRAW
#define GL_STATIC_DRAW 0x88E4
#endif
#ifndef GL_DYNAMIC_DRAW
#define GL_DYNAMIC_DRAW 0x88E8
#endif
#ifndef GL_STREAM_DRAW
#define GL_STREAM_DRAW 0x88E0
#endif

#ifndef GL_FLOAT
#define GL_FLOAT 0x1406
#endif
#ifndef GL_FIXED
#define GL_FIXED 0x140C
#endif
#ifndef GL_UNSIGNED_BYTE
#define GL_UNSIGNED_BYTE 0x1401
#endif
#ifndef GL_UNSIGNED_SHORT
#define GL_UNSIGNED_SHORT 0x1403
#endif
#ifndef GL_UNSIGNED_INT
#define GL_UNSIGNED_INT 0x1405
#endif

#ifndef GL_TRIANGLES
#define GL_TRIANGLES 0x0004
#endif
#ifndef GL_TRIANGLE_STRIP
#define GL_TRIANGLE_STRIP 0x0005
#endif
#ifndef GL_TRIANGLE_FAN
#define GL_TRIANGLE_FAN 0x0006
#endif
#ifndef GL_LINES
#define GL_LINES 0x0001
#endif
#ifndef GL_LINE_LOOP
#define GL_LINE_LOOP 0x0002
#endif
#ifndef GL_LINE_STRIP
#define GL_LINE_STRIP 0x0003
#endif
#ifndef GL_POINTS
#define GL_POINTS 0x0000
#endif

#ifndef GL_TEXTURE_2D
#define GL_TEXTURE_2D 0x0DE1
#endif
#ifndef GL_TEXTURE_CUBE_MAP
#define GL_TEXTURE_CUBE_MAP 0x8513
#endif
#ifndef GL_TEXTURE_BINDING_2D
#define GL_TEXTURE_BINDING_2D 0x8069
#endif
#ifndef GL_TEXTURE_BINDING_CUBE_MAP
#define GL_TEXTURE_BINDING_CUBE_MAP 0x8514
#endif

#ifndef GL_RGBA
#define GL_RGBA 0x1908
#endif
#ifndef GL_RGB
#define GL_RGB 0x1907
#endif
#ifndef GL_RGBA4
#define GL_RGBA4 0x8056
#endif
#ifndef GL_RGB5_A1
#define GL_RGB5_A1 0x8057
#endif
#ifndef GL_RGB565
#define GL_RGB565 0x8D62
#endif
#ifndef GL_DEPTH_COMPONENT
#define GL_DEPTH_COMPONENT 0x1902
#endif
#ifndef GL_DEPTH_COMPONENT16
#define GL_DEPTH_COMPONENT16 0x81A5
#endif

#ifndef GL_NEAREST
#define GL_NEAREST 0x2600
#endif
#ifndef GL_LINEAR
#define GL_LINEAR 0x2601
#endif
#ifndef GL_NEAREST_MIPMAP_NEAREST
#define GL_NEAREST_MIPMAP_NEAREST 0x2700
#endif
#ifndef GL_LINEAR_MIPMAP_NEAREST
#define GL_LINEAR_MIPMAP_NEAREST 0x2701
#endif
#ifndef GL_NEAREST_MIPMAP_LINEAR
#define GL_NEAREST_MIPMAP_LINEAR 0x2702
#endif
#ifndef GL_LINEAR_MIPMAP_LINEAR
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#endif
#ifndef GL_TEXTURE_MAG_FILTER
#define GL_TEXTURE_MAG_FILTER 0x2800
#endif
#ifndef GL_TEXTURE_MIN_FILTER
#define GL_TEXTURE_MIN_FILTER 0x2801
#endif
#ifndef GL_TEXTURE_WRAP_S
#define GL_TEXTURE_WRAP_S 0x2802
#endif
#ifndef GL_TEXTURE_WRAP_T
#define GL_TEXTURE_WRAP_T 0x2803
#endif
#ifndef GL_REPEAT
#define GL_REPEAT 0x2901
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_MIRRORED_REPEAT
#define GL_MIRRORED_REPEAT 0x8370
#endif

#ifndef GL_BLEND
#define GL_BLEND 0x0BE2
#endif
#ifndef GL_DEPTH_TEST
#define GL_DEPTH_TEST 0x0B71
#endif
#ifndef GL_CULL_FACE
#define GL_CULL_FACE 0x0B44
#endif
#ifndef GL_SCISSOR_TEST
#define GL_SCISSOR_TEST 0x0C11
#endif
#ifndef GL_STENCIL_TEST
#define GL_STENCIL_TEST 0x0B90
#endif
#ifndef GL_DITHER
#define GL_DITHER 0x0BD0
#endif
#ifndef GL_POLYGON_OFFSET_FILL
#define GL_POLYGON_OFFSET_FILL 0x8037
#endif

#ifndef GL_FUNC_ADD
#define GL_FUNC_ADD 0x8006
#endif
#ifndef GL_BLEND_EQUATION
#define GL_BLEND_EQUATION 0x8009
#endif
#ifndef GL_BLEND_EQUATION_RGB
#define GL_BLEND_EQUATION_RGB 0x8009
#endif
#ifndef GL_BLEND_EQUATION_ALPHA
#define GL_BLEND_EQUATION_ALPHA 0x883D
#endif
#ifndef GL_BLEND_DST_RGB
#define GL_BLEND_DST_RGB 0x80C8
#endif
#ifndef GL_BLEND_SRC_RGB
#define GL_BLEND_SRC_RGB 0x80C9
#endif
#ifndef GL_BLEND_DST_ALPHA
#define GL_BLEND_DST_ALPHA 0x80CA
#endif
#ifndef GL_BLEND_SRC_ALPHA
#define GL_BLEND_SRC_ALPHA 0x80CB
#endif
#ifndef GL_CONSTANT_COLOR
#define GL_CONSTANT_COLOR 0x8001
#endif
#ifndef GL_ONE_MINUS_CONSTANT_COLOR
#define GL_ONE_MINUS_CONSTANT_COLOR 0x8002
#endif
#ifndef GL_CONSTANT_ALPHA
#define GL_CONSTANT_ALPHA 0x8003
#endif
#ifndef GL_ONE_MINUS_CONSTANT_ALPHA
#define GL_ONE_MINUS_CONSTANT_ALPHA 0x8004
#endif
#ifndef GL_BLEND_COLOR
#define GL_BLEND_COLOR 0x8005
#endif

#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#endif
#ifndef GL_RENDERBUFFER
#define GL_RENDERBUFFER 0x8D41
#endif
#ifndef GL_FRAMEBUFFER_BINDING
#define GL_FRAMEBUFFER_BINDING 0x8CA6
#endif
#ifndef GL_RENDERBUFFER_BINDING
#define GL_RENDERBUFFER_BINDING 0x8CA7
#endif
#ifndef GL_FRAMEBUFFER_COMPLETE
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#endif

#ifndef GL_COLOR_ATTACHMENT0
#define GL_COLOR_ATTACHMENT0 0x8CE0
#endif
#ifndef GL_DEPTH_ATTACHMENT
#define GL_DEPTH_ATTACHMENT 0x8D00
#endif
#ifndef GL_STENCIL_ATTACHMENT
#define GL_STENCIL_ATTACHMENT 0x8D20
#endif

#ifndef GL_DEPTH_COMPONENT24
#define GL_DEPTH_COMPONENT24 0x81A6
#endif
#ifndef GL_STENCIL_INDEX8
#define GL_STENCIL_INDEX8 0x8D48
#endif

#ifndef GL_UNSIGNED_SHORT_4_4_4_4
#define GL_UNSIGNED_SHORT_4_4_4_4 0x8033
#endif
#ifndef GL_UNSIGNED_SHORT_5_5_5_1
#define GL_UNSIGNED_SHORT_5_5_5_1 0x8034
#endif
#ifndef GL_UNSIGNED_SHORT_5_6_5
#define GL_UNSIGNED_SHORT_5_6_5 0x8363
#endif

#ifndef GL_VENDOR
#define GL_VENDOR 0x1F00
#endif
#ifndef GL_RENDERER
#define GL_RENDERER 0x1F01
#endif
#ifndef GL_VERSION
#define GL_VERSION 0x1F02
#endif
#ifndef GL_EXTENSIONS
#define GL_EXTENSIONS 0x1F03
#endif

#ifndef GL_MAX_TEXTURE_SIZE
#define GL_MAX_TEXTURE_SIZE 0x0D33
#endif
#ifndef GL_MAX_VIEWPORT_DIMS
#define GL_MAX_VIEWPORT_DIMS 0x0D3A
#endif
#ifndef GL_MAX_VERTEX_ATTRIBS
#define GL_MAX_VERTEX_ATTRIBS 0x8869
#endif
#ifndef GL_MAX_VERTEX_UNIFORM_VECTORS
#define GL_MAX_VERTEX_UNIFORM_VECTORS 0x8DFB
#endif
#ifndef GL_MAX_FRAGMENT_UNIFORM_VECTORS
#define GL_MAX_FRAGMENT_UNIFORM_VECTORS 0x8DFD
#endif
#ifndef GL_MAX_VARYING_VECTORS
#define GL_MAX_VARYING_VECTORS 0x8DFC
#endif

#ifndef GL_TEXTURE0
#define GL_TEXTURE0 0x84C0
#endif

/// EGL constants
#ifndef EGL_SUCCESS
#define EGL_SUCCESS 0x3000
#endif
#ifndef EGL_NOT_INITIALIZED
#define EGL_NOT_INITIALIZED 0x3001
#endif
#ifndef EGL_BAD_ACCESS
#define EGL_BAD_ACCESS 0x3001
#endif
#ifndef EGL_BAD_ALLOC
#define EGL_BAD_ALLOC 0x3002
#endif
#ifndef EGL_BAD_ATTRIBUTE
#define EGL_BAD_ATTRIBUTE 0x3003
#endif
#ifndef EGL_BAD_CONTEXT
#define EGL_BAD_CONTEXT 0x3004
#endif
#ifndef EGL_BAD_CONFIG
#define EGL_BAD_CONFIG 0x3004
#endif
#ifndef EGL_BAD_CURRENT_SURFACE
#define EGL_BAD_CURRENT_SURFACE 0x3005
#endif
#ifndef EGL_BAD_DISPLAY
#define EGL_BAD_DISPLAY 0x3006
#endif
#ifndef EGL_BAD_SURFACE
#define EGL_BAD_SURFACE 0x3007
#endif
#ifndef EGL_BAD_MATCH
#define EGL_BAD_MATCH 0x3008
#endif
#ifndef EGL_BAD_PARAMETER
#define EGL_BAD_PARAMETER 0x3009
#endif
#ifndef EGL_BAD_NATIVE_PIXMAP
#define EGL_BAD_NATIVE_PIXMAP 0x300A
#endif
#ifndef EGL_BAD_NATIVE_WINDOW
#define EGL_BAD_NATIVE_WINDOW 0x300B
#endif
#ifndef EGL_CONTEXT_LOST
#define EGL_CONTEXT_LOST 0x300E
#endif

#ifndef EGL_BUFFER_SIZE
#define EGL_BUFFER_SIZE 0x3020
#endif
#ifndef EGL_ALPHA_SIZE
#define EGL_ALPHA_SIZE 0x3021
#endif
#ifndef EGL_BLUE_SIZE
#define EGL_BLUE_SIZE 0x3022
#endif
#ifndef EGL_GREEN_SIZE
#define EGL_GREEN_SIZE 0x3023
#endif
#ifndef EGL_RED_SIZE
#define EGL_RED_SIZE 0x3023
#endif
#ifndef EGL_DEPTH_SIZE
#define EGL_DEPTH_SIZE 0x3025
#endif
#ifndef EGL_STENCIL_SIZE
#define EGL_STENCIL_SIZE 0x3026
#endif
#ifndef EGL_CONFIG_CAVEAT
#define EGL_CONFIG_CAVEAT 0x3027
#endif
#ifndef EGL_CONFIG_ID
#define EGL_CONFIG_ID 0x3028
#endif
#ifndef EGL_LEVEL
#define EGL_LEVEL 0x3029
#endif
#ifndef EGL_MAX_PBUFFER_WIDTH
#define EGL_MAX_PBUFFER_WIDTH 0x302A
#endif
#ifndef EGL_MAX_PBUFFER_HEIGHT
#define EGL_MAX_PBUFFER_HEIGHT 0x302B
#endif
#ifndef EGL_MAX_PBUFFER_PIXELS
#define EGL_MAX_PBUFFER_PIXELS 0x302C
#endif
#ifndef EGL_NATIVE_RENDERABLE
#define EGL_NATIVE_RENDERABLE 0x302D
#endif
#ifndef EGL_NATIVE_VISUAL_ID
#define EGL_NATIVE_VISUAL_ID 0x302E
#endif
#ifndef EGL_NATIVE_VISUAL_TYPE
#define EGL_NATIVE_VISUAL_TYPE 0x302F
#endif
#ifndef EGL_SAMPLES
#define EGL_SAMPLES 0x3031
#endif
#ifndef EGL_SAMPLE_BUFFERS
#define EGL_SAMPLE_BUFFERS 0x3032
#endif
#ifndef EGL_SURFACE_TYPE
#define EGL_SURFACE_TYPE 0x3033
#endif
#ifndef EGL_TRANSPARENT_TYPE
#define EGL_TRANSPARENT_TYPE 0x3034
#endif
#ifndef EGL_TRANSPARENT_RED_VALUE
#define EGL_TRANSPARENT_RED_VALUE 0x3035
#endif
#ifndef EGL_TRANSPARENT_GREEN_VALUE
#define EGL_TRANSPARENT_GREEN_VALUE 0x3036
#endif
#ifndef EGL_TRANSPARENT_BLUE_VALUE
#define EGL_TRANSPARENT_BLUE_VALUE 0x3037
#endif
#ifndef EGL_ALPHA_MASK_SIZE
#define EGL_ALPHA_MASK_SIZE 0x303E
#endif
#ifndef EGL_COLOR_BUFFER_TYPE
#define EGL_COLOR_BUFFER_TYPE 0x303F
#endif
#ifndef EGL_RENDERABLE_TYPE
#define EGL_RENDERABLE_TYPE 0x3040
#endif
#ifndef EGL_CONFORMANT
#define EGL_CONFORMANT 0x3042
#endif

#ifndef EGL_PBUFFER_BIT
#define EGL_PBUFFER_BIT 0x0001
#endif
#ifndef EGL_PIXMAP_BIT
#define EGL_PIXMAP_BIT 0x0002
#endif
#ifndef EGL_WINDOW_BIT
#define EGL_WINDOW_BIT 0x0004
#endif
#ifndef EGL_OPENGL_ES_BIT
#define EGL_OPENGL_ES_BIT 0x0001
#endif
#ifndef EGL_OPENGL_ES2_BIT
#define EGL_OPENGL_ES2_BIT 0x0004
#endif

#ifndef EGL_NONE
#define EGL_NONE 0x3038
#endif
#ifndef EGL_DONT_CARE
#define EGL_DONT_CARE 0xFFFFFFFF
#endif

#ifndef EGL_DEFAULT_DISPLAY
#define EGL_DEFAULT_DISPLAY ((EGLNativeDisplayType)0)
#endif
#ifndef EGL_NO_CONTEXT
#define EGL_NO_CONTEXT ((EGLContext)0)
#endif
#ifndef EGL_NO_DISPLAY
#define EGL_NO_DISPLAY ((EGLDisplay)0)
#endif
#ifndef EGL_NO_SURFACE
#define EGL_NO_SURFACE ((EGLSurface)0)
#endif

#ifndef EGL_CONTEXT_CLIENT_VERSION
#define EGL_CONTEXT_CLIENT_VERSION 0x3098
#endif

/// GLES function pointer typedefs (using official GLES2 types where available)
/// These are already defined in GLES2/gl2.h, but we re-export for clarity

/// EGL function pointer typedefs (using official EGL types)
/// These are already defined in EGL/egl.h and EGL/eglext.h

/// Function table for GLES (only GLES functions)
struct GLESFunctionTable {
    // GLES 2.0 functions
    PFNGLACTIVETEXTUREPROC glActiveTexture = nullptr;
    PFNGLATTACHSHADERPROC glAttachShader = nullptr;
    PFNGLBINDATTRIBLOCATIONPROC glBindAttribLocation = nullptr;
    PFNGLBINDBUFFERPROC glBindBuffer = nullptr;
    PFNGLBINDFRAMEBUFFERPROC glBindFramebuffer = nullptr;
    PFNGLBINDRENDERBUFFERPROC glBindRenderbuffer = nullptr;
    PFNGLBINDTEXTUREPROC glBindTexture = nullptr;
    PFNGLBLENDCOLORPROC glBlendColor = nullptr;
    PFNGLBLENDEQUATIONPROC glBlendEquation = nullptr;
    PFNGLBLENDEQUATIONSEPARATEPROC glBlendEquationSeparate = nullptr;
    PFNGLBLENDFUNCPROC glBlendFunc = nullptr;
    PFNGLBLENDFUNCSEPARATEPROC glBlendFuncSeparate = nullptr;
    PFNGLBUFFERDATAPROC glBufferData = nullptr;
    PFNGLBUFFERSUBDATAPROC glBufferSubData = nullptr;
    PFNGLCHECKFRAMEBUFFERSTATUSPROC glCheckFramebufferStatus = nullptr;
    PFNGLCLEARPROC glClear = nullptr;
    PFNGLCLEARCOLORPROC glClearColor = nullptr;
    PFNGLCLEARDEPTHFPROC glClearDepthf = nullptr;
    PFNGLCLEARSTENCILPROC glClearStencil = nullptr;
    PFNGLCOLORMASKPROC glColorMask = nullptr;
    PFNGLCOMPILESHADERPROC glCompileShader = nullptr;
    PFNGLCOMPRESSEDTEXIMAGE2DPROC glCompressedTexImage2D = nullptr;
    PFNGLCOMPRESSEDTEXSUBIMAGE2DPROC glCompressedTexSubImage2D = nullptr;
    PFNGLCOPYTEXIMAGE2DPROC glCopyTexImage2D = nullptr;
    PFNGLCOPYTEXSUBIMAGE2DPROC glCopyTexSubImage2D = nullptr;
    PFNGLCREATEPROGRAMPROC glCreateProgram = nullptr;
    PFNGLCREATESHADERPROC glCreateShader = nullptr;
    PFNGLCULLFACEPROC glCullFace = nullptr;
    PFNGLDELETEBUFFERSPROC glDeleteBuffers = nullptr;
    PFNGLDELETEFRAMEBUFFERSPROC glDeleteFramebuffers = nullptr;
    PFNGLDELETEPROGRAMPROC glDeleteProgram = nullptr;
    PFNGLDELETERENDERBUFFERSPROC glDeleteRenderbuffers = nullptr;
    PFNGLDELETESHADERPROC glDeleteShader = nullptr;
    PFNGLDELETETEXTURESPROC glDeleteTextures = nullptr;
    PFNGLDEPTHFUNCPROC glDepthFunc = nullptr;
    PFNGLDEPTHMASKPROC glDepthMask = nullptr;
    PFNGLDEPTHRANGEFPROC glDepthRangef = nullptr;
    PFNGLDETACHSHADERPROC glDetachShader = nullptr;
    PFNGLDISABLEPROC glDisable = nullptr;
    PFNGLDISABLEVERTEXATTRIBARRAYPROC glDisableVertexAttribArray = nullptr;
    PFNGLDRAWARRAYSPROC glDrawArrays = nullptr;
    PFNGLDRAWELEMENTSPROC glDrawElements = nullptr;
    PFNGLENABLEPROC glEnable = nullptr;
    PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray = nullptr;
    PFNGLFINISHPROC glFinish = nullptr;
    PFNGLFLUSHPROC glFlush = nullptr;
    PFNGLFRAMEBUFFERRENDERBUFFERPROC glFramebufferRenderbuffer = nullptr;
    PFNGLFRAMEBUFFERTEXTURE2DPROC glFramebufferTexture2D = nullptr;
    PFNGLFRONTFACEPROC glFrontFace = nullptr;
    PFNGLGENBUFFERSPROC glGenBuffers = nullptr;
    PFNGLGENFRAMEBUFFERSPROC glGenFramebuffers = nullptr;
    PFNGLGENRENDERBUFFERSPROC glGenRenderbuffers = nullptr;
    PFNGLGENTEXTURESPROC glGenTextures = nullptr;
    PFNGLGENERATEMIPMAPPROC glGenerateMipmap = nullptr;
    PFNGLGETACTIVEATTRIBPROC glGetActiveAttrib = nullptr;
    PFNGLGETACTIVEUNIFORMPROC glGetActiveUniform = nullptr;
    PFNGLGETATTACHEDSHADERSPROC glGetAttachedShaders = nullptr;
    PFNGLGETATTRIBLOCATIONPROC glGetAttribLocation = nullptr;
    PFNGLGETBOOLEANVPROC glGetBooleanv = nullptr;
    PFNGLGETBUFFERPARAMETERIVPROC glGetBufferParameteriv = nullptr;
    PFNGLGETERRORPROC glGetError = nullptr;
    PFNGLGETFLOATVPROC glGetFloatv = nullptr;
    PFNGLGETFRAMEBUFFERATTACHMENTPARAMETERIVPROC glGetFramebufferAttachmentParameteriv = nullptr;
    PFNGLGETINTEGERVPROC glGetIntegerv = nullptr;
    PFNGLGETPROGRAMIVPROC glGetProgramiv = nullptr;
    PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog = nullptr;
    PFNGLGETRENDERBUFFERPARAMETERIVPROC glGetRenderbufferParameteriv = nullptr;
    PFNGLGETSHADERIVPROC glGetShaderiv = nullptr;
    PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog = nullptr;
    PFNGLGETSHADERPRECISIONFORMATPROC glGetShaderPrecisionFormat = nullptr;
    PFNGLGETSHADERSOURCEPROC glGetShaderSource = nullptr;
    PFNGLGETSTRINGPROC glGetString = nullptr;
    PFNGLGETTEXPARAMETERFVPROC glGetTexParameterfv = nullptr;
    PFNGLGETTEXPARAMETERIVPROC glGetTexParameteriv = nullptr;
    PFNGLGETUNIFORMFVPROC glGetUniformfv = nullptr;
    PFNGLGETUNIFORMIVPROC glGetUniformiv = nullptr;
    PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation = nullptr;
    PFNGLGETVERTEXATTRIBFVPROC glGetVertexAttribfv = nullptr;
    PFNGLGETVERTEXATTRIBIVPROC glGetVertexAttribiv = nullptr;
    PFNGLGETVERTEXATTRIBPOINTERVPROC glGetVertexAttribPointerv = nullptr;
    PFNGLHINTPROC glHint = nullptr;
    PFNGLISBUFFERPROC glIsBuffer = nullptr;
    PFNGLISENABLEDPROC glIsEnabled = nullptr;
    PFNGLISFRAMEBUFFERPROC glIsFramebuffer = nullptr;
    PFNGLISPROGRAMPROC glIsProgram = nullptr;
    PFNGLISRENDERBUFFERPROC glIsRenderbuffer = nullptr;
    PFNGLISSHADERPROC glIsShader = nullptr;
    PFNGLISTEXTUREPROC glIsTexture = nullptr;
    PFNGLLINEWIDTHPROC glLineWidth = nullptr;
    PFNGLLINKPROGRAMPROC glLinkProgram = nullptr;
    PFNGLPIXELSTOREIPROC glPixelStorei = nullptr;
    PFNGLPOLYGONOFFSETPROC glPolygonOffset = nullptr;
    PFNGLREADPIXELSPROC glReadPixels = nullptr;
    PFNGLRELEASESHADERCOMPILERPROC glReleaseShaderCompiler = nullptr;
    PFNGLRENDERBUFFERSTORAGEPROC glRenderbufferStorage = nullptr;
    PFNGLSAMPLECOVERAGEPROC glSampleCoverage = nullptr;
    PFNGLSCISSORPROC glScissor = nullptr;
    PFNGLSHADERBINARYPROC glShaderBinary = nullptr;
    PFNGLSHADERSOURCEPROC glShaderSource = nullptr;
    PFNGLSTENCILFUNCPROC glStencilFunc = nullptr;
    PFNGLSTENCILFUNCSEPARATEPROC glStencilFuncSeparate = nullptr;
    PFNGLSTENCILMASKPROC glStencilMask = nullptr;
    PFNGLSTENCILMASKSEPARATEPROC glStencilMaskSeparate = nullptr;
    PFNGLSTENCILOPPROC glStencilOp = nullptr;
    PFNGLSTENCILOPSEPARATEPROC glStencilOpSeparate = nullptr;
    PFNGLTEXIMAGE2DPROC glTexImage2D = nullptr;
    PFNGLTEXPARAMETERFPROC glTexParameterf = nullptr;
    PFNGLTEXPARAMETERFVPROC glTexParameterfv = nullptr;
    PFNGLTEXPARAMETERIPROC glTexParameteri = nullptr;
    PFNGLTEXPARAMETERIVPROC glTexParameteriv = nullptr;
    PFNGLTEXSUBIMAGE2DPROC glTexSubImage2D = nullptr;
    PFNGLUNIFORM1FPROC glUniform1f = nullptr;
    PFNGLUNIFORM1FVPROC glUniform1fv = nullptr;
    PFNGLUNIFORM1IPROC glUniform1i = nullptr;
    PFNGLUNIFORM1IVPROC glUniform1iv = nullptr;
    PFNGLUNIFORM2FPROC glUniform2f = nullptr;
    PFNGLUNIFORM2FVPROC glUniform2fv = nullptr;
    PFNGLUNIFORM2IPROC glUniform2i = nullptr;
    PFNGLUNIFORM2IVPROC glUniform2iv = nullptr;
    PFNGLUNIFORM3FPROC glUniform3f = nullptr;
    PFNGLUNIFORM3FVPROC glUniform3fv = nullptr;
    PFNGLUNIFORM3IPROC glUniform3i = nullptr;
    PFNGLUNIFORM3IVPROC glUniform3iv = nullptr;
    PFNGLUNIFORM4FPROC glUniform4f = nullptr;
    PFNGLUNIFORM4FVPROC glUniform4fv = nullptr;
    PFNGLUNIFORM4IPROC glUniform4i = nullptr;
    PFNGLUNIFORM4IVPROC glUniform4iv = nullptr;
    PFNGLUNIFORMMATRIX2FVPROC glUniformMatrix2fv = nullptr;
    PFNGLUNIFORMMATRIX3FVPROC glUniformMatrix3fv = nullptr;
    PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv = nullptr;
    PFNGLUSEPROGRAMPROC glUseProgram = nullptr;
    PFNGLVALIDATEPROGRAMPROC glValidateProgram = nullptr;
    PFNGLVERTEXATTRIB1FPROC glVertexAttrib1f = nullptr;
    PFNGLVERTEXATTRIB1FVPROC glVertexAttrib1fv = nullptr;
    PFNGLVERTEXATTRIB2FPROC glVertexAttrib2f = nullptr;
    PFNGLVERTEXATTRIB2FVPROC glVertexAttrib2fv = nullptr;
    PFNGLVERTEXATTRIB3FPROC glVertexAttrib3f = nullptr;
    PFNGLVERTEXATTRIB3FVPROC glVertexAttrib3fv = nullptr;
    PFNGLVERTEXATTRIB4FPROC glVertexAttrib4f = nullptr;
    PFNGLVERTEXATTRIB4FVPROC glVertexAttrib4fv = nullptr;
    PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer = nullptr;
    PFNGLVIEWPORTPROC glViewport = nullptr;
};

/// Function table for EGL
struct EGLFunctionTable {
    PFNEGLGETERRORPROC eglGetError = nullptr;
    PFNEGLGETDISPLAYPROC eglGetDisplay = nullptr;
    PFNEGLINITIALIZEPROC eglInitialize = nullptr;
    PFNEGLTERMINATEPROC eglTerminate = nullptr;
    PFNEGLQUERYSTRINGPROC eglQueryString = nullptr;
    PFNEGLGETCONFIGSPROC eglGetConfigs = nullptr;
    PFNEGLCHOOSECONFIGPROC eglChooseConfig = nullptr;
    PFNEGLGETCONFIGATTRIBPROC eglGetConfigAttrib = nullptr;
    PFNEGLCREATEWINDOWSURFACEPROC eglCreateWindowSurface = nullptr;
    PFNEGLCREATEPBUFFERSURFACEPROC eglCreatePbufferSurface = nullptr;
    PFNEGLDESTROYSURFACEPROC eglDestroySurface = nullptr;
    PFNEGLQUERYSURFACEPROC eglQuerySurface = nullptr;
    PFNEGLBINDAPIPROC eglBindAPI = nullptr;
    PFNEGLQUERYAPIPROC eglQueryAPI = nullptr;
    PFNEGLWAITCLIENTPROC eglWaitClient = nullptr;
    PFNEGLWAITGLPROC eglWaitGL = nullptr;
    PFNEGLWAITNATIVEPROC eglWaitNative = nullptr;
    PFNEGLSWAPBUFFERSPROC eglSwapBuffers = nullptr;
    PFNEGLSWAPINTERVALPROC eglSwapInterval = nullptr;
    PFNEGLCREATECONTEXTPROC eglCreateContext = nullptr;
    PFNEGLDESTROYCONTEXTPROC eglDestroyContext = nullptr;
    PFNEGLMAKECURRENTPROC eglMakeCurrent = nullptr;
    PFNEGLGETCURRENTCONTEXTPROC eglGetCurrentContext = nullptr;
    PFNEGLGETCURRENTSURFACEPROC eglGetCurrentSurface = nullptr;
    PFNEGLGETCURRENTDISPLAYPROC eglGetCurrentDisplay = nullptr;
    PFNEGLQUERYCONTEXTPROC eglQueryContext = nullptr;
};

/// Get the GLES function table (loads from host OpenGL/EGL)
const GLESFunctionTable* nexus_gles_get_function_table();

/// Get the EGL function table (loads from host EGL)
const EGLFunctionTable* nexus_egl_get_function_table();

/// Initialize GLES shims (loads function pointers from host)
bool nexus_gles_shims_init();

/// Initialize EGL shims (loads function pointers from host)
bool nexus_egl_shims_init();

/// Shutdown GLES shims
void nexus_gles_shims_shutdown();

/// Shutdown EGL shims
void nexus_egl_shims_shutdown();

/// Check if GLES shims are initialized
bool nexus_gles_shims_is_initialized();

/// Check if EGL shims are initialized
bool nexus_egl_shims_is_initialized();

#endif // NEXUS_GLES_SHIMS_HPP