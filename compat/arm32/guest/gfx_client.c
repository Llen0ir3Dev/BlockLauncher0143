/*
 * ARM32 guest EGL/GLES2 client.
 *
 * Every EGL/GLES2 entry point imported by the genuine libminecraftpe.so is
 * implemented here and forwarded to the x86_64 host renderer.  Scalars travel
 * in a fixed 16-word argument block; arrays travel in the payload.  The host
 * executes the calls against a real Mesa GLES2 context.
 *
 * This is not a software GL implementation: shader compilation, texture
 * sampling and rasterisation all happen on the host GL stack.
 */
#include "gfx_client.h"
#include "gfx_protocol.h"
#include "loader_symbols.h"

#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

/* ------------------------------------------------------------------------- */
/* transport                                                                 */
/* ------------------------------------------------------------------------- */

static int g_socket = -1;
static uint8_t* g_resp = NULL;
static uint32_t g_resp_len = 0;
static uint32_t g_resp_pos = 0;

typedef struct {
    uint8_t* data;
    size_t len;
    size_t cap;
} nxbuf;

static int nxbuf_reserve(nxbuf* b, size_t extra) {
    if (b->len + extra <= b->cap) return 0;
    size_t cap = b->cap ? b->cap : 256;
    while (cap < b->len + extra) cap *= 2;
    uint8_t* p = (uint8_t*)realloc(b->data, cap);
    if (!p) return -1;
    b->data = p;
    b->cap = cap;
    return 0;
}

static void nxbuf_u32(nxbuf* b, uint32_t v) {
    if (nxbuf_reserve(b, 4) != 0) return;
    memcpy(b->data + b->len, &v, 4);
    b->len += 4;
}

static void nxbuf_bytes(nxbuf* b, const void* p, size_t n) {
    if (!p || !n) return;
    if (nxbuf_reserve(b, n) != 0) return;
    memcpy(b->data + b->len, p, n);
    b->len += n;
}

static int send_all(const void* data, size_t len) {
    const uint8_t* p = (const uint8_t*)data;
    while (len > 0) {
        ssize_t n = send(g_socket, p, len, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

static int recv_all(void* data, size_t len) {
    uint8_t* p = (uint8_t*)data;
    while (len > 0) {
        ssize_t n = recv(g_socket, p, len, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return -1;
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

int nexus_gfx_connect(const char* socket_path) {
    if (g_socket >= 0) return 0;
    const char* path = socket_path;
    if (!path || !*path) path = getenv("NEXUS_GFX_SOCKET");
    if (!path || !*path) {
        fprintf(stderr, "[gfx] no socket path (set NEXUS_GFX_SOCKET)\n");
        return -1;
    }
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (strlen(path) >= sizeof(addr.sun_path)) return -1;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        fprintf(stderr, "[gfx] connect(%s) failed: %s\n", path, strerror(errno));
        close(fd);
        return -1;
    }
    g_socket = fd;
    return 0;
}

void nexus_gfx_disconnect(void) {
    if (g_socket >= 0) close(g_socket);
    g_socket = -1;
    free(g_resp);
    g_resp = NULL;
    g_resp_len = g_resp_pos = 0;
}

int nexus_gfx_connected(void) {
    return g_socket >= 0;
}

static int gfx_call(uint32_t cmd, const uint32_t* args, size_t nargs,
                    const void* blob, size_t bloblen) {
    if (g_socket < 0 && nexus_gfx_connect(NULL) != 0) return NEXUS_GFX_IO_ERROR;
    if (nargs > NEXUS_GFX_MAX_ARGS) nargs = NEXUS_GFX_MAX_ARGS;

    uint32_t payload_len = (uint32_t)(sizeof(uint32_t) * NEXUS_GFX_MAX_ARGS + bloblen);
    uint8_t* payload = (uint8_t*)calloc(1, payload_len);
    if (!payload) return NEXUS_GFX_IO_ERROR;
    if (args && nargs) memcpy(payload, args, nargs * sizeof(uint32_t));
    if (blob && bloblen) memcpy(payload + sizeof(uint32_t) * NEXUS_GFX_MAX_ARGS, blob, bloblen);

    uint32_t header[2] = {cmd, payload_len};
    int rc = send_all(header, sizeof(header));
    if (rc == 0) rc = send_all(payload, payload_len);
    free(payload);
    if (rc != 0) {
        nexus_gfx_disconnect();
        return NEXUS_GFX_IO_ERROR;
    }

    uint32_t rh[2];
    if (recv_all(rh, sizeof(rh)) != 0) {
        nexus_gfx_disconnect();
        return NEXUS_GFX_IO_ERROR;
    }
    free(g_resp);
    g_resp = NULL;
    g_resp_len = g_resp_pos = 0;
    if (rh[1]) {
        g_resp = (uint8_t*)malloc(rh[1]);
        if (!g_resp || recv_all(g_resp, rh[1]) != 0) {
            free(g_resp);
            g_resp = NULL;
            nexus_gfx_disconnect();
            return NEXUS_GFX_IO_ERROR;
        }
        g_resp_len = rh[1];
    }
    return (int32_t)rh[0];
}

static uint32_t r_u32(void) {
    if (g_resp_pos + 4 > g_resp_len) return 0;
    uint32_t v;
    memcpy(&v, g_resp + g_resp_pos, 4);
    g_resp_pos += 4;
    return v;
}

static int32_t r_i32(void) {
    return (int32_t)r_u32();
}

static const char* r_string(void) {
    if (g_resp_pos + 4 > g_resp_len) return "";
    uint32_t len = r_u32();
    if (g_resp_pos + len > g_resp_len) return "";
    const char* s = (const char*)(g_resp + g_resp_pos);
    g_resp_pos += len;
    return s;
}

/* ------------------------------------------------------------------------- */
/* argument helpers                                                          */
/* ------------------------------------------------------------------------- */

static uint32_t U(uint32_t v) { return v; }
static uint32_t I(int32_t v) { return (uint32_t)v; }
static uint32_t F(float v) {
    uint32_t bits;
    memcpy(&bits, &v, 4);
    return bits;
}

#define GFX_VOID(cmd) gfx_call((cmd), NULL, 0, NULL, 0)
#define GFX(cmd, ...) do { \
    uint32_t _a[] = { __VA_ARGS__ }; \
    gfx_call((cmd), _a, sizeof(_a) / sizeof(_a[0]), NULL, 0); \
} while (0)
#define GFX_DATA(cmd, ptr, len, ...) do { \
    uint32_t _a[] = { __VA_ARGS__ }; \
    gfx_call((cmd), _a, sizeof(_a) / sizeof(_a[0]), (ptr), (len)); \
} while (0)

/* ------------------------------------------------------------------------- */
/* EGL                                                                       */
/* ------------------------------------------------------------------------- */

#define NEXUS_FAKE_HANDLE ((uintptr_t)0x4e58)

EGLDisplay eglGetDisplay(EGLNativeDisplayType display_id) {
    (void)display_id;
    return (EGLDisplay)NEXUS_FAKE_HANDLE;
}

EGLBoolean eglInitialize(EGLDisplay dpy, EGLint* major, EGLint* minor) {
    (void)dpy;
    if (gfx_call(NEXUS_GFX_EGL_INITIALIZE, NULL, 0, NULL, 0) != 0) return EGL_FALSE;
    if (major) *major = r_i32();
    if (minor) *minor = r_i32();
    return EGL_TRUE;
}

EGLBoolean eglChooseConfig(EGLDisplay dpy, const EGLint* attrib_list, EGLConfig* configs,
                           EGLint config_size, EGLint* num_config) {
    (void)dpy;
    nxbuf blob = {0};
    if (attrib_list) {
        for (size_t i = 0; attrib_list[i] != EGL_NONE; i += 2) {
            nxbuf_u32(&blob, (uint32_t)attrib_list[i]);
            nxbuf_u32(&blob, (uint32_t)attrib_list[i + 1]);
        }
        nxbuf_u32(&blob, EGL_NONE);
    }
    const int rc = gfx_call(NEXUS_GFX_EGL_CHOOSE_CONFIG, NULL, 0, blob.data, blob.len);
    free(blob.data);
    if (rc != 0) return EGL_FALSE;
    const int32_t count = r_i32();
    if (num_config) *num_config = count;
    if (configs && config_size > 0) {
        configs[0] = (EGLConfig)(uintptr_t)(r_u32() ? 1u : 0u);
    }
    return EGL_TRUE;
}

EGLBoolean eglGetConfigAttrib(EGLDisplay dpy, EGLConfig config, EGLint attribute,
                              EGLint* value) {
    (void)dpy; (void)config;
    if (gfx_call(NEXUS_GFX_EGL_GET_CONFIG_ATTRIB, (uint32_t[]){ (uint32_t)attribute }, 1,
                 NULL, 0) != 0) return EGL_FALSE;
    if (value) *value = r_i32();
    return EGL_TRUE;
}

EGLSurface eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config,
                                  EGLNativeWindowType win, const EGLint* attrib_list) {
    (void)dpy; (void)config; (void)win;
    nxbuf blob = {0};
    if (attrib_list) {
        for (size_t i = 0; attrib_list[i] != EGL_NONE; i += 2) {
            nxbuf_u32(&blob, (uint32_t)attrib_list[i]);
            nxbuf_u32(&blob, (uint32_t)attrib_list[i + 1]);
        }
    }
    const int rc = gfx_call(NEXUS_GFX_EGL_CREATE_WINDOW_SURFACE, NULL, 0,
                            blob.data, blob.len);
    free(blob.data);
    (void)rc;
    return (EGLSurface)NEXUS_FAKE_HANDLE;
}

EGLContext eglCreateContext(EGLDisplay dpy, EGLConfig config, EGLContext share_context,
                            const EGLint* attrib_list) {
    (void)dpy; (void)config; (void)share_context;
    nxbuf blob = {0};
    if (attrib_list) {
        for (size_t i = 0; attrib_list[i] != EGL_NONE; i += 2) {
            nxbuf_u32(&blob, (uint32_t)attrib_list[i]);
            nxbuf_u32(&blob, (uint32_t)attrib_list[i + 1]);
        }
    }
    const int rc = gfx_call(NEXUS_GFX_EGL_CREATE_CONTEXT, NULL, 0, blob.data, blob.len);
    free(blob.data);
    (void)rc;
    return (EGLContext)NEXUS_FAKE_HANDLE;
}

EGLBoolean eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read,
                          EGLContext ctx) {
    (void)dpy; (void)draw; (void)read; (void)ctx;
    return gfx_call(NEXUS_GFX_EGL_MAKE_CURRENT, NULL, 0, NULL, 0) == 0
               ? EGL_TRUE : EGL_FALSE;
}

EGLBoolean eglSwapInterval(EGLDisplay dpy, EGLint interval) {
    (void)dpy;
    return gfx_call(NEXUS_GFX_EGL_SWAP_INTERVAL, (uint32_t[]){ (uint32_t)interval }, 1,
                    NULL, 0) == 0 ? EGL_TRUE : EGL_FALSE;
}

EGLBoolean eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) {
    (void)dpy; (void)surface;
    return gfx_call(NEXUS_GFX_EGL_SWAP_BUFFERS, NULL, 0, NULL, 0) == 0
               ? EGL_TRUE : EGL_FALSE;
}

EGLBoolean eglQuerySurface(EGLDisplay dpy, EGLSurface surface, EGLint attribute,
                           EGLint* value) {
    (void)dpy; (void)surface;
    if (gfx_call(NEXUS_GFX_EGL_QUERY_SURFACE, (uint32_t[]){ (uint32_t)attribute }, 1,
                 NULL, 0) != 0) return EGL_FALSE;
    if (value) *value = r_i32();
    return EGL_TRUE;
}

EGLint eglGetError(void) {
    if (gfx_call(NEXUS_GFX_EGL_GET_ERROR, NULL, 0, NULL, 0) != 0) return EGL_BAD_DISPLAY;
    return r_i32();
}

EGLBoolean eglTerminate(EGLDisplay dpy) {
    (void)dpy;
    gfx_call(NEXUS_GFX_EGL_TERMINATE, NULL, 0, NULL, 0);
    return EGL_TRUE;
}

EGLBoolean eglDestroySurface(EGLDisplay dpy, EGLSurface surface) {
    (void)dpy; (void)surface;
    return EGL_TRUE;
}

EGLBoolean eglDestroyContext(EGLDisplay dpy, EGLContext ctx) {
    (void)dpy; (void)ctx;
    return EGL_TRUE;
}

/* ------------------------------------------------------------------------- */
/* GL state                                                                  */
/* ------------------------------------------------------------------------- */

void glEnable(GLenum cap) { GFX(NEXUS_GFX_GL_ENABLE, U(cap)); }
void glDisable(GLenum cap) { GFX(NEXUS_GFX_GL_DISABLE, U(cap)); }
void glBlendFunc(GLenum sfactor, GLenum dfactor) {
    GFX(NEXUS_GFX_GL_BLEND_FUNC, U(sfactor), U(dfactor));
}
void glBlendFuncSeparate(GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha) {
    GFX(NEXUS_GFX_GL_BLEND_FUNC_SEPARATE, U(srcRGB), U(dstRGB), U(srcAlpha), U(dstAlpha));
}
void glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a) {
    GFX(NEXUS_GFX_GL_COLOR_MASK, U(r), U(g), U(b), U(a));
}
void glDepthFunc(GLenum func) { GFX(NEXUS_GFX_GL_DEPTH_FUNC, U(func)); }
void glDepthMask(GLboolean flag) { GFX(NEXUS_GFX_GL_DEPTH_MASK, U(flag)); }
void glStencilFuncSeparate(GLenum face, GLenum func, GLint ref, GLuint mask) {
    GFX(NEXUS_GFX_GL_STENCIL_FUNC_SEPARATE, U(face), U(func), I(ref), U(mask));
}
void glStencilOpSeparate(GLenum face, GLenum sfail, GLenum dpfail, GLenum dppass) {
    GFX(NEXUS_GFX_GL_STENCIL_OP_SEPARATE, U(face), U(sfail), U(dpfail), U(dppass));
}
void glStencilMask(GLuint mask) { GFX(NEXUS_GFX_GL_STENCIL_MASK, U(mask)); }
void glScissor(GLint x, GLint y, GLsizei width, GLsizei height) {
    GFX(NEXUS_GFX_GL_SCISSOR, I(x), I(y), I(width), I(height));
}
void glViewport(GLint x, GLint y, GLsizei width, GLsizei height) {
    GFX(NEXUS_GFX_GL_VIEWPORT, I(x), I(y), I(width), I(height));
}
void glClearStencil(GLint s) { GFX(NEXUS_GFX_GL_CLEAR_STENCIL, I(s)); }
void glClearDepthf(GLfloat d) { GFX(NEXUS_GFX_GL_CLEAR_DEPTHF, F(d)); }
void glClearColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) {
    GFX(NEXUS_GFX_GL_CLEAR_COLOR, F(red), F(green), F(blue), F(alpha));
}
void glClear(GLbitfield mask) { GFX(NEXUS_GFX_GL_CLEAR, U(mask)); }
void glPolygonOffset(GLfloat factor, GLfloat units) {
    GFX(NEXUS_GFX_GL_POLYGON_OFFSET, F(factor), F(units));
}
void glCullFace(GLenum mode) { GFX(NEXUS_GFX_GL_CULL_FACE, U(mode)); }
void glDepthRangef(GLfloat n, GLfloat f) { GFX(NEXUS_GFX_GL_DEPTH_RANGEF, F(n), F(f)); }
void glActiveTexture(GLenum texture) { GFX(NEXUS_GFX_GL_ACTIVE_TEXTURE, U(texture)); }
void glTexParameteri(GLenum target, GLenum pname, GLint param) {
    GFX(NEXUS_GFX_GL_TEX_PARAMETERI, U(target), U(pname), I(param));
}
void glPixelStorei(GLenum pname, GLint param) {
    GFX(NEXUS_GFX_GL_PIXEL_STOREI, U(pname), I(param));
}
void glHint(GLenum target, GLenum mode) { GFX(NEXUS_GFX_GL_HINT, U(target), U(mode)); }
void glLineWidth(GLfloat width) { GFX(NEXUS_GFX_GL_LINE_WIDTH, F(width)); }
void glFrontFace(GLenum mode) { GFX(NEXUS_GFX_GL_FRONT_FACE, U(mode)); }
void glGenerateMipmap(GLenum target) { GFX(NEXUS_GFX_GL_GENERATE_MIPMAP, U(target)); }
void glFlush(void) { GFX_VOID(NEXUS_GFX_GL_FLUSH); }
void glFinish(void) { GFX_VOID(NEXUS_GFX_GL_FINISH); }
void glReleaseShaderCompiler(void) { GFX_VOID(NEXUS_GFX_GL_RELEASE_SHADER_COMPILER); }

/* ------------------------------------------------------------------------- */
/* buffers and textures                                                      */
/* ------------------------------------------------------------------------- */

static GLuint g_array_buffer = 0;
static GLuint g_element_buffer = 0;

void glGenBuffers(GLsizei n, GLuint* buffers) {
    if (n <= 0 || !buffers) return;
    if (gfx_call(NEXUS_GFX_GL_GEN_BUFFERS, (uint32_t[]){ (uint32_t)n }, 1, NULL, 0) == 0) {
        for (GLsizei i = 0; i < n; ++i) buffers[i] = r_u32();
    }
}
void glBindBuffer(GLenum target, GLuint buffer) {
    if (target == GL_ARRAY_BUFFER) g_array_buffer = buffer;
    if (target == GL_ELEMENT_ARRAY_BUFFER) g_element_buffer = buffer;
    GFX(NEXUS_GFX_GL_BIND_BUFFER, U(target), U(buffer));
}
void glBufferData(GLenum target, GLsizeiptr size, const void* data, GLenum usage) {
    GFX_DATA(NEXUS_GFX_GL_BUFFER_DATA, data, (size_t)size, U(target), I((GLint)size), U(usage));
}
void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void* data) {
    GFX_DATA(NEXUS_GFX_GL_BUFFER_SUB_DATA, data, (size_t)size,
             U(target), I((GLint)offset), I((GLint)size));
}
void glDeleteBuffers(GLsizei n, const GLuint* buffers) {
    if (n <= 0 || !buffers) return;
    GFX_DATA(NEXUS_GFX_GL_DELETE_BUFFERS, buffers, (size_t)n * sizeof(GLuint), (uint32_t)n);
}
void glGenTextures(GLsizei n, GLuint* textures) {
    if (n <= 0 || !textures) return;
    if (gfx_call(NEXUS_GFX_GL_GEN_TEXTURES, (uint32_t[]){ (uint32_t)n }, 1, NULL, 0) == 0) {
        for (GLsizei i = 0; i < n; ++i) textures[i] = r_u32();
    }
}
void glBindTexture(GLenum target, GLuint texture) {
    GFX(NEXUS_GFX_GL_BIND_TEXTURE, U(target), U(texture));
}
void glDeleteTextures(GLsizei n, const GLuint* textures) {
    if (n <= 0 || !textures) return;
    GFX_DATA(NEXUS_GFX_GL_DELETE_TEXTURES, textures, (size_t)n * sizeof(GLuint), (uint32_t)n);
}
GLboolean glIsTexture(GLuint texture) {
    if (gfx_call(NEXUS_GFX_GL_IS_TEXTURE, (uint32_t[]){ (uint32_t)texture }, 1, NULL, 0) != 0)
        return GL_FALSE;
    return (GLboolean)r_i32();
}
void glGetTexParameteriv(GLenum target, GLenum pname, GLint* params) {
    if (gfx_call(NEXUS_GFX_GL_GET_TEX_PARAMETERIV,
                 (uint32_t[]){ (uint32_t)target, (uint32_t)pname }, 2, NULL, 0) == 0) {
        if (params) *params = r_i32();
    }
}

static size_t nexus_pixel_bytes(GLenum format, GLenum type) {
    if (type == GL_UNSIGNED_BYTE) {
        switch (format) {
            case GL_ALPHA:
            case GL_LUMINANCE: return 1;
            case GL_LUMINANCE_ALPHA: return 2;
            case GL_RGB: return 3;
            case GL_RGBA: return 4;
            default: return 0;
        }
    }
    if (type == GL_UNSIGNED_SHORT_5_6_5 || type == GL_UNSIGNED_SHORT_4_4_4_4 ||
        type == GL_UNSIGNED_SHORT_5_5_5_1) {
        return 2;
    }
    return 0;
}

void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width,
                  GLsizei height, GLint border, GLenum format, GLenum type,
                  const void* pixels) {
    const size_t bpp = nexus_pixel_bytes(format, type);
    const size_t bytes = (size_t)width * (size_t)height * bpp;
    GFX_DATA(NEXUS_GFX_GL_TEX_IMAGE_2D, pixels, pixels ? bytes : 0,
             U(target), I(level), I(internalformat), I(width), I(height), I(border),
             U(format), U(type));
}
void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                     GLsizei width, GLsizei height, GLenum format, GLenum type,
                     const void* pixels) {
    const size_t bpp = nexus_pixel_bytes(format, type);
    const size_t bytes = (size_t)width * (size_t)height * bpp;
    GFX_DATA(NEXUS_GFX_GL_TEX_SUB_IMAGE_2D, pixels, pixels ? bytes : 0,
             U(target), I(level), I(xoffset), I(yoffset), I(width), I(height),
             U(format), U(type));
}

/* ------------------------------------------------------------------------- */
/* framebuffers                                                              */
/* ------------------------------------------------------------------------- */

void glGenFramebuffers(GLsizei n, GLuint* framebuffers) {
    if (n <= 0 || !framebuffers) return;
    if (gfx_call(NEXUS_GFX_GL_GEN_FRAMEBUFFERS, (uint32_t[]){ (uint32_t)n }, 1, NULL, 0) == 0) {
        for (GLsizei i = 0; i < n; ++i) framebuffers[i] = r_u32();
    }
}
void glBindFramebuffer(GLenum target, GLuint framebuffer) {
    GFX(NEXUS_GFX_GL_BIND_FRAMEBUFFER, U(target), U(framebuffer));
}
void glDeleteFramebuffers(GLsizei n, const GLuint* framebuffers) {
    if (n <= 0 || !framebuffers) return;
    GFX_DATA(NEXUS_GFX_GL_DELETE_FRAMEBUFFERS, framebuffers,
             (size_t)n * sizeof(GLuint), (uint32_t)n);
}
void glGenRenderbuffers(GLsizei n, GLuint* renderbuffers) {
    if (n <= 0 || !renderbuffers) return;
    if (gfx_call(NEXUS_GFX_GL_GEN_RENDERBUFFERS, (uint32_t[]){ (uint32_t)n }, 1, NULL, 0) == 0) {
        for (GLsizei i = 0; i < n; ++i) renderbuffers[i] = r_u32();
    }
}
void glBindRenderbuffer(GLenum target, GLuint renderbuffer) {
    GFX(NEXUS_GFX_GL_BIND_RENDERBUFFER, U(target), U(renderbuffer));
}
void glDeleteRenderbuffers(GLsizei n, const GLuint* renderbuffers) {
    if (n <= 0 || !renderbuffers) return;
    GFX_DATA(NEXUS_GFX_GL_DELETE_RENDERBUFFERS, renderbuffers,
             (size_t)n * sizeof(GLuint), (uint32_t)n);
}
void glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget,
                            GLuint texture, GLint level) {
    GFX(NEXUS_GFX_GL_FRAMEBUFFER_TEXTURE_2D, U(target), U(attachment), U(textarget),
        U(texture), I(level));
}
void glFramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum renderbuffertarget,
                               GLuint renderbuffer) {
    GFX(NEXUS_GFX_GL_FRAMEBUFFER_RENDERBUFFER, U(target), U(attachment),
        U(renderbuffertarget), U(renderbuffer));
}
void glRenderbufferStorage(GLenum target, GLenum internalformat, GLsizei width,
                           GLsizei height) {
    GFX(NEXUS_GFX_GL_RENDERBUFFER_STORAGE, U(target), U(internalformat), I(width), I(height));
}
GLenum glCheckFramebufferStatus(GLenum target) {
    if (gfx_call(NEXUS_GFX_GL_CHECK_FRAMEBUFFER_STATUS, (uint32_t[]){ (uint32_t)target }, 1,
                 NULL, 0) != 0) return GL_FRAMEBUFFER_UNSUPPORTED;
    return (GLenum)r_u32();
}

/* ------------------------------------------------------------------------- */
/* shaders and programs                                                      */
/* ------------------------------------------------------------------------- */

GLuint glCreateShader(GLenum type) {
    if (gfx_call(NEXUS_GFX_GL_CREATE_SHADER, (uint32_t[]){ (uint32_t)type }, 1, NULL, 0) != 0)
        return 0;
    return r_u32();
}
void glShaderSource(GLuint shader, GLsizei count, const GLchar* const* string,
                    const GLint* length) {
    if (!string || count <= 0) return;
    nxbuf blob = {0};
    for (GLsizei i = 0; i < count; ++i) {
        const GLchar* s = string[i];
        if (!s) continue;
        size_t n = (length && length[i] >= 0) ? (size_t)length[i] : strlen(s);
        nxbuf_bytes(&blob, s, n);
    }
    nxbuf_bytes(&blob, "", 1);
    GFX_DATA(NEXUS_GFX_GL_SHADER_SOURCE, blob.data, blob.len, U(shader), I(count));
    free(blob.data);
}
void glCompileShader(GLuint shader) { GFX(NEXUS_GFX_GL_COMPILE_SHADER, U(shader)); }
void glGetShaderiv(GLuint shader, GLenum pname, GLint* params) {
    if (gfx_call(NEXUS_GFX_GL_GET_SHADERIV,
                 (uint32_t[]){ (uint32_t)shader, (uint32_t)pname }, 2, NULL, 0) == 0) {
        if (params) *params = r_i32();
    }
}
void glGetShaderInfoLog(GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog) {
    if (gfx_call(NEXUS_GFX_GL_GET_SHADER_INFO_LOG,
                 (uint32_t[]){ (uint32_t)shader, I(bufSize) }, 2, NULL, 0) == 0) {
        const char* s = r_string();
        const size_t n = strlen(s);
        if (infoLog && bufSize > 0) {
            const size_t copy = n < (size_t)(bufSize - 1) ? n : (size_t)(bufSize - 1);
            memcpy(infoLog, s, copy);
            infoLog[copy] = '\0';
            if (length) *length = (GLsizei)copy;
        } else if (length) {
            *length = (GLsizei)n;
        }
    }
}
void glGetShaderPrecisionFormat(GLenum shaderType, GLenum precisionType, GLint* range,
                                GLint* precision) {
    if (gfx_call(NEXUS_GFX_GL_GET_SHADER_PRECISION_FORMAT,
                 (uint32_t[]){ (uint32_t)shaderType, (uint32_t)precisionType }, 2,
                 NULL, 0) == 0) {
        if (range) *range = r_i32();
        if (precision) *precision = r_i32();
    }
}
void glDeleteShader(GLuint shader) { GFX(NEXUS_GFX_GL_DELETE_SHADER, U(shader)); }

GLuint glCreateProgram(void) {
    if (gfx_call(NEXUS_GFX_GL_CREATE_PROGRAM, NULL, 0, NULL, 0) != 0) return 0;
    return r_u32();
}
void glAttachShader(GLuint program, GLuint shader) {
    GFX(NEXUS_GFX_GL_ATTACH_SHADER, U(program), U(shader));
}
void glLinkProgram(GLuint program) { GFX(NEXUS_GFX_GL_LINK_PROGRAM, U(program)); }
void glGetProgramiv(GLuint program, GLenum pname, GLint* params) {
    if (gfx_call(NEXUS_GFX_GL_GET_PROGRAMIV,
                 (uint32_t[]){ (uint32_t)program, (uint32_t)pname }, 2, NULL, 0) == 0) {
        if (params) *params = r_i32();
    }
}
void glGetProgramInfoLog(GLuint program, GLsizei bufSize, GLsizei* length, GLchar* infoLog) {
    if (gfx_call(NEXUS_GFX_GL_GET_PROGRAM_INFO_LOG,
                 (uint32_t[]){ (uint32_t)program, I(bufSize) }, 2, NULL, 0) == 0) {
        const char* s = r_string();
        const size_t n = strlen(s);
        if (infoLog && bufSize > 0) {
            const size_t copy = n < (size_t)(bufSize - 1) ? n : (size_t)(bufSize - 1);
            memcpy(infoLog, s, copy);
            infoLog[copy] = '\0';
            if (length) *length = (GLsizei)copy;
        } else if (length) {
            *length = (GLsizei)n;
        }
    }
}
void glDeleteProgram(GLuint program) { GFX(NEXUS_GFX_GL_DELETE_PROGRAM, U(program)); }
void glUseProgram(GLuint program) { GFX(NEXUS_GFX_GL_USE_PROGRAM, U(program)); }

void glGetActiveUniform(GLuint program, GLuint index, GLsizei bufSize, GLsizei* length,
                        GLint* size, GLenum* type, GLchar* name) {
    if (gfx_call(NEXUS_GFX_GL_GET_ACTIVE_UNIFORM,
                 (uint32_t[]){ (uint32_t)program, (uint32_t)index, I(bufSize) }, 3,
                 NULL, 0) == 0) {
        const char* s = r_string();
        const GLint sz = r_i32();
        const GLenum ty = r_u32();
        const size_t n = strlen(s);
        if (name && bufSize > 0) {
            const size_t copy = n < (size_t)(bufSize - 1) ? n : (size_t)(bufSize - 1);
            memcpy(name, s, copy);
            name[copy] = '\0';
            if (length) *length = (GLsizei)copy;
        } else if (length) {
            *length = (GLsizei)n;
        }
        if (size) *size = sz;
        if (type) *type = ty;
    }
}
void glGetActiveAttrib(GLuint program, GLuint index, GLsizei bufSize, GLsizei* length,
                       GLint* size, GLenum* type, GLchar* name) {
    if (gfx_call(NEXUS_GFX_GL_GET_ACTIVE_ATTRIB,
                 (uint32_t[]){ (uint32_t)program, (uint32_t)index, I(bufSize) }, 3,
                 NULL, 0) == 0) {
        const char* s = r_string();
        const GLint sz = r_i32();
        const GLenum ty = r_u32();
        const size_t n = strlen(s);
        if (name && bufSize > 0) {
            const size_t copy = n < (size_t)(bufSize - 1) ? n : (size_t)(bufSize - 1);
            memcpy(name, s, copy);
            name[copy] = '\0';
            if (length) *length = (GLsizei)copy;
        } else if (length) {
            *length = (GLsizei)n;
        }
        if (size) *size = sz;
        if (type) *type = ty;
    }
}
GLint glGetUniformLocation(GLuint program, const GLchar* name) {
    if (!name) return -1;
    if (gfx_call(NEXUS_GFX_GL_GET_UNIFORM_LOCATION,
                 (uint32_t[]){ (uint32_t)program }, 1, name, strlen(name) + 1) != 0)
        return -1;
    return r_i32();
}
GLint glGetAttribLocation(GLuint program, const GLchar* name) {
    if (!name) return -1;
    if (gfx_call(NEXUS_GFX_GL_GET_ATTRIB_LOCATION,
                 (uint32_t[]){ (uint32_t)program }, 1, name, strlen(name) + 1) != 0)
        return -1;
    return r_i32();
}

/* ------------------------------------------------------------------------- */
/* uniforms and attributes                                                   */
/* ------------------------------------------------------------------------- */

void glUniform1i(GLint location, GLint v0) {
    GFX(NEXUS_GFX_GL_UNIFORM1I, I(location), I(v0));
}
#define UNIFORM_IV(name, cmd, comps) \
    void name(GLint location, GLsizei count, const GLint* value) { \
        if (count <= 0 || !value) return; \
        GFX_DATA(cmd, value, (size_t)count * (comps) * sizeof(GLint), \
                 I(location), I(count)); \
    }
#define UNIFORM_FV(name, cmd, comps) \
    void name(GLint location, GLsizei count, const GLfloat* value) { \
        if (count <= 0 || !value) return; \
        GFX_DATA(cmd, value, (size_t)count * (comps) * sizeof(GLfloat), \
                 I(location), I(count)); \
    }
UNIFORM_IV(glUniform1iv, NEXUS_GFX_GL_UNIFORM1IV, 1)
UNIFORM_FV(glUniform1fv, NEXUS_GFX_GL_UNIFORM1FV, 1)
UNIFORM_IV(glUniform2iv, NEXUS_GFX_GL_UNIFORM2IV, 2)
UNIFORM_FV(glUniform2fv, NEXUS_GFX_GL_UNIFORM2FV, 2)
UNIFORM_IV(glUniform3iv, NEXUS_GFX_GL_UNIFORM3IV, 3)
UNIFORM_FV(glUniform3fv, NEXUS_GFX_GL_UNIFORM3FV, 3)
UNIFORM_IV(glUniform4iv, NEXUS_GFX_GL_UNIFORM4IV, 4)
UNIFORM_FV(glUniform4fv, NEXUS_GFX_GL_UNIFORM4FV, 4)

void glUniformMatrix2fv(GLint location, GLsizei count, GLboolean transpose,
                        const GLfloat* value) {
    if (count <= 0 || !value) return;
    GFX_DATA(NEXUS_GFX_GL_UNIFORM_MATRIX2FV, value,
             (size_t)count * 4 * sizeof(GLfloat), I(location), I(count), U(transpose));
}
void glUniformMatrix3fv(GLint location, GLsizei count, GLboolean transpose,
                        const GLfloat* value) {
    if (count <= 0 || !value) return;
    GFX_DATA(NEXUS_GFX_GL_UNIFORM_MATRIX3FV, value,
             (size_t)count * 9 * sizeof(GLfloat), I(location), I(count), U(transpose));
}
void glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose,
                        const GLfloat* value) {
    if (count <= 0 || !value) return;
    GFX_DATA(NEXUS_GFX_GL_UNIFORM_MATRIX4FV, value,
             (size_t)count * 16 * sizeof(GLfloat), I(location), I(count), U(transpose));
}

void glEnableVertexAttribArray(GLuint index) {
    GFX(NEXUS_GFX_GL_ENABLE_VERTEX_ATTRIB_ARRAY, U(index));
}
void glDisableVertexAttribArray(GLuint index) {
    GFX(NEXUS_GFX_GL_DISABLE_VERTEX_ATTRIB_ARRAY, U(index));
}

void glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized,
                           GLsizei stride, const void* pointer) {
    if (g_array_buffer == 0 && pointer != NULL) {
        fprintf(stderr, "[gfx] glVertexAttribPointer with client array is not forwarded yet\n");
    }
    GFX(NEXUS_GFX_GL_VERTEX_ATTRIB_POINTER, U(index), I(size), U(type), U(normalized),
        I(stride), I((GLint)(uintptr_t)pointer));
}

/* ------------------------------------------------------------------------- */
/* draws and queries                                                         */
/* ------------------------------------------------------------------------- */

void glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    GFX(NEXUS_GFX_GL_DRAW_ARRAYS, U(mode), I(first), I(count));
}

static size_t index_type_bytes(GLenum type) {
    if (type == GL_UNSIGNED_BYTE) return 1;
    if (type == GL_UNSIGNED_SHORT) return 2;
    return 4;
}

void glDrawElements(GLenum mode, GLsizei count, GLenum type, const void* indices) {
    if (g_element_buffer == 0 && indices != NULL) {
        const size_t bytes = (size_t)count * index_type_bytes(type);
        GFX_DATA(NEXUS_GFX_GL_DRAW_ELEMENTS, indices, bytes, U(mode), I(count), U(type), 0);
    } else {
        GFX(NEXUS_GFX_GL_DRAW_ELEMENTS, U(mode), I(count), U(type),
            I((GLint)(uintptr_t)indices));
    }
}

void glGetIntegerv(GLenum pname, GLint* data) {
    if (!data) return;
    if (gfx_call(NEXUS_GFX_GL_GET_INTEGERV, (uint32_t[]){ (uint32_t)pname }, 1, NULL, 0) != 0)
        return;
    const uint32_t count = r_u32();
    for (uint32_t i = 0; i < count && i < 64; ++i) data[i] = r_i32();
}

static char g_string_cache[8][4096];
static GLenum g_string_keys[8];

const GLubyte* glGetString(GLenum name) {
    if (gfx_call(NEXUS_GFX_GL_GET_STRING, (uint32_t[]){ (uint32_t)name }, 1, NULL, 0) != 0)
        return (const GLubyte*)"";
    const char* s = r_string();
    for (int i = 0; i < 8; ++i) {
        if (g_string_keys[i] == name || g_string_keys[i] == 0) {
            g_string_keys[i] = name;
            snprintf(g_string_cache[i], sizeof(g_string_cache[i]), "%s", s);
            return (const GLubyte*)g_string_cache[i];
        }
    }
    snprintf(g_string_cache[0], sizeof(g_string_cache[0]), "%s", s);
    return (const GLubyte*)g_string_cache[0];
}

GLenum glGetError(void) {
    if (gfx_call(NEXUS_GFX_GL_GET_ERROR, NULL, 0, NULL, 0) != 0) return GL_INVALID_OPERATION;
    return (GLenum)r_u32();
}

void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format,
                  GLenum type, void* pixels) {
    if (!pixels || width <= 0 || height <= 0) return;
    const size_t bpp = nexus_pixel_bytes(format, type);
    const size_t bytes = (size_t)width * (size_t)height * bpp;
    if (gfx_call(NEXUS_GFX_GL_READ_PIXELS,
                 (uint32_t[]){ I(x), I(y), I(width), I(height), U(format), U(type) }, 6,
                 NULL, 0) == 0) {
        const uint32_t n = r_u32();
        const size_t copy = n < bytes ? n : bytes;
        if (g_resp_pos + copy <= g_resp_len) {
            memcpy(pixels, g_resp + g_resp_pos, copy);
            g_resp_pos += copy;
        }
    }
}

/* ------------------------------------------------------------------------- */
/* eglGetProcAddress and loader registration                                 */
/* ------------------------------------------------------------------------- */

#define NX_FN(name) { #name, (void*)(uintptr_t)&name }

static const struct {
    const char* name;
    void* address;
} g_proc_table[] = {
    NX_FN(eglGetProcAddress),
    NX_FN(eglGetDisplay), NX_FN(eglInitialize), NX_FN(eglChooseConfig),
    NX_FN(eglGetConfigAttrib), NX_FN(eglCreateWindowSurface), NX_FN(eglCreateContext),
    NX_FN(eglMakeCurrent), NX_FN(eglSwapInterval), NX_FN(eglSwapBuffers),
    NX_FN(eglQuerySurface), NX_FN(eglGetError), NX_FN(eglTerminate),
    NX_FN(eglDestroySurface), NX_FN(eglDestroyContext),
    NX_FN(glEnable), NX_FN(glDisable), NX_FN(glBlendFunc), NX_FN(glBlendFuncSeparate),
    NX_FN(glColorMask), NX_FN(glDepthFunc), NX_FN(glDepthMask),
    NX_FN(glStencilFuncSeparate), NX_FN(glStencilOpSeparate), NX_FN(glStencilMask),
    NX_FN(glScissor), NX_FN(glViewport), NX_FN(glClearStencil), NX_FN(glClearDepthf),
    NX_FN(glClearColor), NX_FN(glClear), NX_FN(glPolygonOffset), NX_FN(glCullFace),
    NX_FN(glDepthRangef), NX_FN(glActiveTexture), NX_FN(glTexParameteri),
    NX_FN(glPixelStorei), NX_FN(glHint), NX_FN(glLineWidth), NX_FN(glFrontFace),
    NX_FN(glGenerateMipmap), NX_FN(glFlush), NX_FN(glFinish),
    NX_FN(glReleaseShaderCompiler), NX_FN(glGenBuffers), NX_FN(glBindBuffer),
    NX_FN(glBufferData), NX_FN(glBufferSubData), NX_FN(glDeleteBuffers),
    NX_FN(glGenTextures), NX_FN(glBindTexture), NX_FN(glDeleteTextures),
    NX_FN(glIsTexture), NX_FN(glGetTexParameteriv), NX_FN(glTexImage2D),
    NX_FN(glTexSubImage2D), NX_FN(glGenFramebuffers), NX_FN(glBindFramebuffer),
    NX_FN(glDeleteFramebuffers), NX_FN(glGenRenderbuffers), NX_FN(glBindRenderbuffer),
    NX_FN(glDeleteRenderbuffers), NX_FN(glFramebufferTexture2D),
    NX_FN(glFramebufferRenderbuffer), NX_FN(glRenderbufferStorage),
    NX_FN(glCheckFramebufferStatus), NX_FN(glCreateShader), NX_FN(glShaderSource),
    NX_FN(glCompileShader), NX_FN(glGetShaderiv), NX_FN(glGetShaderInfoLog),
    NX_FN(glGetShaderPrecisionFormat), NX_FN(glDeleteShader), NX_FN(glCreateProgram),
    NX_FN(glAttachShader), NX_FN(glLinkProgram), NX_FN(glGetProgramiv),
    NX_FN(glGetProgramInfoLog), NX_FN(glDeleteProgram), NX_FN(glUseProgram),
    NX_FN(glGetActiveUniform), NX_FN(glGetActiveAttrib), NX_FN(glGetUniformLocation),
    NX_FN(glGetAttribLocation), NX_FN(glUniform1i), NX_FN(glUniform1iv),
    NX_FN(glUniform1fv), NX_FN(glUniform2iv), NX_FN(glUniform2fv),
    NX_FN(glUniform3iv), NX_FN(glUniform3fv), NX_FN(glUniform4iv),
    NX_FN(glUniform4fv), NX_FN(glUniformMatrix2fv), NX_FN(glUniformMatrix3fv),
    NX_FN(glUniformMatrix4fv), NX_FN(glEnableVertexAttribArray),
    NX_FN(glDisableVertexAttribArray), NX_FN(glVertexAttribPointer),
    NX_FN(glDrawArrays), NX_FN(glDrawElements), NX_FN(glGetIntegerv),
    NX_FN(glGetString), NX_FN(glGetError), NX_FN(glReadPixels),
};

__eglMustCastToProperFunctionPointerType eglGetProcAddress(const char* procname) {
    if (!procname) return NULL;
    for (size_t i = 0; i < sizeof(g_proc_table) / sizeof(g_proc_table[0]); ++i) {
        if (strcmp(g_proc_table[i].name, procname) == 0)
            return (__eglMustCastToProperFunctionPointerType)g_proc_table[i].address;
    }
    fprintf(stderr, "[gfx] eglGetProcAddress(%s): not forwarded\n", procname);
    return NULL;
}

void nexus_register_gfx_symbols(void) {
    nexus_loader_add_symbols((const nexus_symbol_entry*)0, 0); /* keeps API explicit */
    static nexus_symbol_entry table[sizeof(g_proc_table) / sizeof(g_proc_table[0])];
    const size_t n = sizeof(g_proc_table) / sizeof(g_proc_table[0]);
    for (size_t i = 0; i < n; ++i) {
        table[i].name = g_proc_table[i].name;
        table[i].address = g_proc_table[i].address;
    }
    nexus_loader_add_symbols(table, n);
}
