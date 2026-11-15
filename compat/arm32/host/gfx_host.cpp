/*
 * NEXUS-MCPE x86_64 host renderer.
 *
 * The ARM32 guest cannot load the host's EGL/GLES2 libraries, so it forwards
 * every EGL/GLES2 call through a Unix socket and this process executes those
 * calls against a real Mesa GLES2 context.  Nothing here emulates GL: shader
 * compilation, rasterisation, texturing and readback all use the host driver.
 *
 * Modes:
 *   --headless      surfaceless Mesa display + pbuffer (tests and CI)
 *   --window        SDL2 window + EGL window surface (interactive session)
 *
 * The socket speaks the framing documented in gfx/gfx_protocol.h.
 */
#include "gfx_protocol.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

/* ------------------------------------------------------------------------- */
/* PNG writer (stored deflate stream; no external image dependency)          */
/* ------------------------------------------------------------------------- */

uint32_t crc32_update(uint32_t crc, const uint8_t* data, size_t len) {
    crc = ~crc;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            crc = (crc >> 1) ^ (0xedb88320u & (~(crc & 1u) + 1u));
        }
    }
    return ~crc;
}

uint32_t adler32_update(uint32_t adler, const uint8_t* data, size_t len) {
    uint32_t a = adler & 0xffffu;
    uint32_t b = (adler >> 16) & 0xffffu;
    for (size_t i = 0; i < len; ++i) {
        a = (a + data[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

void put_be32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v >> 24));
    out.push_back(static_cast<uint8_t>(v >> 16));
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v));
}

void png_chunk(std::vector<uint8_t>& out, const char type[4],
               const std::vector<uint8_t>& data) {
    put_be32(out, static_cast<uint32_t>(data.size()));
    std::vector<uint8_t> body(type, type + 4);
    body.insert(body.end(), data.begin(), data.end());
    out.insert(out.end(), body.begin(), body.end());
    put_be32(out, crc32_update(0, body.data(), body.size()));
}

bool write_png(const char* path, int width, int height, const std::vector<uint8_t>& rgba) {
    if (width <= 0 || height <= 0 || rgba.size() < static_cast<size_t>(width) * height * 4) {
        return false;
    }

    // PNG scanlines are top-down; GL readback is bottom-up.
    std::vector<uint8_t> raw;
    raw.reserve(static_cast<size_t>(height) * (1 + static_cast<size_t>(width) * 4));
    for (int y = height - 1; y >= 0; --y) {
        raw.push_back(0);  // filter: none
        const uint8_t* row = rgba.data() + static_cast<size_t>(y) * width * 4;
        raw.insert(raw.end(), row, row + static_cast<size_t>(width) * 4);
    }

    // zlib stream with stored deflate blocks (maximum 65535 bytes each).
    std::vector<uint8_t> z;
    z.push_back(0x78);
    z.push_back(0x01);
    size_t offset = 0;
    while (offset < raw.size() || raw.empty()) {
        const size_t chunk = std::min<size_t>(65535, raw.size() - offset);
        const bool final = (offset + chunk) >= raw.size();
        z.push_back(final ? 1 : 0);
        z.push_back(static_cast<uint8_t>(chunk & 0xff));
        z.push_back(static_cast<uint8_t>((chunk >> 8) & 0xff));
        z.push_back(static_cast<uint8_t>((~chunk) & 0xff));
        z.push_back(static_cast<uint8_t>(((~chunk) >> 8) & 0xff));
        z.insert(z.end(), raw.begin() + offset, raw.begin() + offset + chunk);
        offset += chunk;
        if (final) break;
    }
    put_be32(z, adler32_update(1, raw.data(), raw.size()));

    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    std::vector<uint8_t> ihdr;
    put_be32(ihdr, static_cast<uint32_t>(width));
    put_be32(ihdr, static_cast<uint32_t>(height));
    ihdr.push_back(8);  // bit depth
    ihdr.push_back(6);  // colour type: RGBA
    ihdr.push_back(0);  // compression
    ihdr.push_back(0);  // filter
    ihdr.push_back(0);  // interlace
    png_chunk(png, "IHDR", ihdr);
    png_chunk(png, "IDAT", z);
    png_chunk(png, "IEND", {});

    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    const bool ok = std::fwrite(png.data(), 1, png.size(), f) == png.size();
    std::fclose(f);
    return ok;
}

/* ------------------------------------------------------------------------- */
/* renderer state                                                            */
/* ------------------------------------------------------------------------- */

struct Renderer {
    bool windowed = false;
    int width = 640;
    int height = 480;
    std::string dump_path;

    SDL_Window* window = nullptr;
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLConfig config = nullptr;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    EGLNativeWindowType native_window = 0;
    EGLint egl_major = 0;
    EGLint egl_minor = 0;
    bool initialized = false;
};

Renderer g_r;

void log_egl_error(const char* where) {
    std::fprintf(stderr, "[gfx-host] %s failed: EGL error 0x%x\n", where,
                 static_cast<unsigned>(eglGetError()));
    std::fflush(stderr);
}

bool sdl_get_native_display(EGLNativeDisplayType* out_display,
                            EGLNativeWindowType* out_window) {
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(g_r.window, &info)) {
        std::fprintf(stderr, "[gfx-host] SDL_GetWindowWMInfo: %s\n", SDL_GetError());
        return false;
    }
    switch (info.subsystem) {
        case SDL_SYSWM_X11:
            *out_display = reinterpret_cast<EGLNativeDisplayType>(info.info.x11.display);
            *out_window = reinterpret_cast<EGLNativeWindowType>(info.info.x11.window);
            return true;
#if defined(SDL_VIDEO_DRIVER_WAYLAND)
        case SDL_SYSWM_WAYLAND:
            *out_display = reinterpret_cast<EGLNativeDisplayType>(info.info.wl.display);
            *out_window = reinterpret_cast<EGLNativeWindowType>(info.info.wl.egl_window);
            return true;
#endif
        default:
            std::fprintf(stderr, "[gfx-host] unsupported SDL window subsystem %d\n",
                         static_cast<int>(info.subsystem));
            return false;
    }
}

bool renderer_init() {
    if (g_r.initialized) return true;

    if (g_r.windowed) {
        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            std::fprintf(stderr, "[gfx-host] SDL_Init: %s\n", SDL_GetError());
            return false;
        }
        g_r.window = SDL_CreateWindow("NEXUS-MCPE", SDL_WINDOWPOS_CENTERED,
                                      SDL_WINDOWPOS_CENTERED, g_r.width, g_r.height,
                                      SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
        if (!g_r.window) {
            std::fprintf(stderr, "[gfx-host] SDL_CreateWindow: %s\n", SDL_GetError());
            return false;
        }
        EGLNativeDisplayType native_display = 0;
        if (!sdl_get_native_display(&native_display, &g_r.native_window)) return false;
        g_r.display = eglGetDisplay(native_display);
    } else {
        PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display =
            reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
                eglGetProcAddress("eglGetPlatformDisplayEXT"));
        if (get_platform_display) {
            g_r.display = get_platform_display(EGL_PLATFORM_SURFACELESS_MESA,
                                               EGL_DEFAULT_DISPLAY, nullptr);
        } else {
            g_r.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        }
    }

    if (g_r.display == EGL_NO_DISPLAY) {
        log_egl_error("eglGetDisplay");
        return false;
    }
    EGLint major = 0, minor = 0;
    if (!eglInitialize(g_r.display, &major, &minor)) {
        log_egl_error("eglInitialize");
        return false;
    }
    g_r.egl_major = major;
    g_r.egl_minor = minor;
    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        log_egl_error("eglBindAPI");
        return false;
    }
    std::fprintf(stderr, "[gfx-host] EGL %d.%d vendor=%s renderer=%s mode=%s %dx%d\n",
                 major, minor, eglQueryString(g_r.display, EGL_VENDOR),
                 eglQueryString(g_r.display, EGL_VERSION), g_r.windowed ? "window" : "headless",
                 g_r.width, g_r.height);
    std::fflush(stderr);
    g_r.initialized = true;
    return true;
}

std::vector<EGLint> translate_config(const std::vector<uint32_t>& requested) {
    std::vector<EGLint> out;
    bool has_surface = false;
    bool has_renderable = false;
    for (size_t i = 0; i + 1 < requested.size(); i += 2) {
        const EGLint key = static_cast<EGLint>(requested[i]);
        EGLint value = static_cast<EGLint>(requested[i + 1]);
        if (key == EGL_NONE) break;
        if (key == EGL_SURFACE_TYPE) {
            has_surface = true;
            if (!g_r.windowed) value = EGL_PBUFFER_BIT;
        }
        if (key == EGL_RENDERABLE_TYPE) has_renderable = true;
        out.push_back(key);
        out.push_back(value);
    }
    if (!has_surface) {
        out.push_back(EGL_SURFACE_TYPE);
        out.push_back(g_r.windowed ? EGL_WINDOW_BIT : EGL_PBUFFER_BIT);
    }
    if (!has_renderable) {
        out.push_back(EGL_RENDERABLE_TYPE);
        out.push_back(EGL_OPENGL_ES2_BIT);
    }
    out.push_back(EGL_NONE);
    return out;
}

bool ensure_config(const std::vector<uint32_t>& requested) {
    if (g_r.config) return true;
    if (!renderer_init()) return false;

    std::vector<EGLint> attribs = translate_config(requested);
    EGLConfig configs[16];
    EGLint count = 0;
    if (!eglChooseConfig(g_r.display, attribs.data(), configs, 16, &count) || count <= 0) {
        log_egl_error("eglChooseConfig");
        return false;
    }
    g_r.config = configs[0];
    return true;
}

bool ensure_surface(const std::vector<uint32_t>& requested) {
    if (g_r.surface != EGL_NO_SURFACE) return true;
    if (!ensure_config(requested)) return false;

    if (g_r.windowed) {
        std::vector<EGLint> attribs;
        for (size_t i = 0; i + 1 < requested.size(); i += 2) {
            if (static_cast<EGLint>(requested[i]) == EGL_NONE) break;
            attribs.push_back(static_cast<EGLint>(requested[i]));
            attribs.push_back(static_cast<EGLint>(requested[i + 1]));
        }
        attribs.push_back(EGL_NONE);
        g_r.surface = eglCreateWindowSurface(g_r.display, g_r.config, g_r.native_window,
                                             attribs.data());
    } else {
        // The game requests a window surface.  Headless mode substitutes a
        // pbuffer of the session size so the exact same GL command stream runs.
        const EGLint attribs[] = {EGL_WIDTH, g_r.width, EGL_HEIGHT, g_r.height, EGL_NONE};
        g_r.surface = eglCreatePbufferSurface(g_r.display, g_r.config, attribs);
    }
    if (g_r.surface == EGL_NO_SURFACE) {
        log_egl_error("eglCreateWindowSurface");
        return false;
    }
    return true;
}

bool ensure_context(const std::vector<uint32_t>& requested) {
    if (g_r.context != EGL_NO_CONTEXT) return true;
    if (!ensure_config(requested)) return false;

    std::vector<EGLint> attribs;
    for (size_t i = 0; i + 1 < requested.size(); i += 2) {
        if (static_cast<EGLint>(requested[i]) == EGL_NONE) break;
        attribs.push_back(static_cast<EGLint>(requested[i]));
        attribs.push_back(static_cast<EGLint>(requested[i + 1]));
    }
    if (attribs.empty()) {
        attribs.push_back(EGL_CONTEXT_CLIENT_VERSION);
        attribs.push_back(2);
    }
    attribs.push_back(EGL_NONE);
    g_r.context = eglCreateContext(g_r.display, g_r.config, EGL_NO_CONTEXT, attribs.data());
    if (g_r.context == EGL_NO_CONTEXT) {
        log_egl_error("eglCreateContext");
        return false;
    }
    return true;
}

void dump_frame() {
    if (g_r.dump_path.empty() || g_r.surface == EGL_NO_SURFACE) return;
    EGLint width = g_r.width, height = g_r.height;
    eglQuerySurface(g_r.display, g_r.surface, EGL_WIDTH, &width);
    eglQuerySurface(g_r.display, g_r.surface, EGL_HEIGHT, &height);
    if (width <= 0 || height <= 0) return;
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    if (write_png(g_r.dump_path.c_str(), width, height, pixels)) {
        std::fprintf(stderr, "[gfx-host] wrote %s (%dx%d)\n", g_r.dump_path.c_str(),
                     width, height);
    } else {
        std::fprintf(stderr, "[gfx-host] failed to write %s\n", g_r.dump_path.c_str());
    }
    std::fflush(stderr);
}

/* ------------------------------------------------------------------------- */
/* request/response framing                                                  */
/* ------------------------------------------------------------------------- */

struct Request {
    uint32_t command = 0;
    uint32_t args[NEXUS_GFX_MAX_ARGS] = {0};
    std::vector<uint8_t> blob;
};

struct Response {
    int32_t status = NEXUS_GFX_OK;
    std::vector<uint8_t> payload;

    void u32(uint32_t v) {
        payload.push_back(static_cast<uint8_t>(v));
        payload.push_back(static_cast<uint8_t>(v >> 8));
        payload.push_back(static_cast<uint8_t>(v >> 16));
        payload.push_back(static_cast<uint8_t>(v >> 24));
    }
    void i32(int32_t v) { u32(static_cast<uint32_t>(v)); }
    void f32(float v) {
        uint32_t bits;
        std::memcpy(&bits, &v, 4);
        u32(bits);
    }
    void bytes(const void* data, size_t len) {
        const uint8_t* p = static_cast<const uint8_t*>(data);
        payload.insert(payload.end(), p, p + len);
    }
    void str(const char* s) {
        const size_t n = s ? std::strlen(s) + 1 : 1;
        u32(static_cast<uint32_t>(n));
        if (s) bytes(s, n);
        else payload.push_back(0);
    }
};

bool read_all(int fd, void* buffer, size_t len) {
    uint8_t* p = static_cast<uint8_t*>(buffer);
    while (len > 0) {
        const ssize_t n = ::read(fd, p, len);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (n == 0) return false;
        p += n;
        len -= static_cast<size_t>(n);
    }
    return true;
}

bool write_all(int fd, const void* buffer, size_t len) {
    const uint8_t* p = static_cast<const uint8_t*>(buffer);
    while (len > 0) {
        const ssize_t n = ::write(fd, p, len);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        p += n;
        len -= static_cast<size_t>(n);
    }
    return true;
}

std::vector<uint32_t> list_from_blob(const std::vector<uint8_t>& blob) {
    std::vector<uint32_t> out(blob.size() / 4);
    if (!out.empty()) std::memcpy(out.data(), blob.data(), out.size() * 4);
    return out;
}

/* ------------------------------------------------------------------------- */
/* command dispatch                                                          */
/* ------------------------------------------------------------------------- */

void handle_egl(const Request& req, Response& resp) {
    const uint32_t* a = req.args;
    switch (req.command) {
        case NEXUS_GFX_EGL_INITIALIZE: {
            if (!renderer_init()) {
                resp.status = NEXUS_GFX_IO_ERROR;
                return;
            }
            resp.i32(g_r.egl_major);
            resp.i32(g_r.egl_minor);
            return;
        }
        case NEXUS_GFX_EGL_CHOOSE_CONFIG: {
            if (!ensure_config(list_from_blob(req.blob))) {
                resp.status = NEXUS_GFX_IO_ERROR;
                return;
            }
            resp.i32(1);
            resp.u32(1);
            return;
        }
        case NEXUS_GFX_EGL_GET_CONFIG_ATTRIB: {
            if (!g_r.config) {
                resp.status = NEXUS_GFX_IO_ERROR;
                return;
            }
            EGLint value = 0;
            if (!eglGetConfigAttrib(g_r.display, g_r.config,
                                    static_cast<EGLint>(a[0]), &value)) {
                resp.status = NEXUS_GFX_IO_ERROR;
                return;
            }
            resp.i32(value);
            return;
        }
        case NEXUS_GFX_EGL_CREATE_WINDOW_SURFACE: {
            if (!ensure_surface(list_from_blob(req.blob))) {
                resp.status = NEXUS_GFX_IO_ERROR;
                return;
            }
            return;
        }
        case NEXUS_GFX_EGL_CREATE_CONTEXT: {
            if (!ensure_context(list_from_blob(req.blob))) {
                resp.status = NEXUS_GFX_IO_ERROR;
                return;
            }
            return;
        }
        case NEXUS_GFX_EGL_MAKE_CURRENT: {
            if (!ensure_surface({}) || !ensure_context({})) {
                resp.status = NEXUS_GFX_IO_ERROR;
                return;
            }
            if (!eglMakeCurrent(g_r.display, g_r.surface, g_r.surface, g_r.context)) {
                log_egl_error("eglMakeCurrent");
                resp.status = NEXUS_GFX_IO_ERROR;
            }
            return;
        }
        case NEXUS_GFX_EGL_SWAP_INTERVAL: {
            if (g_r.display != EGL_NO_DISPLAY &&
                !eglSwapInterval(g_r.display, static_cast<EGLint>(a[0]))) {
                resp.status = NEXUS_GFX_IO_ERROR;
            }
            return;
        }
        case NEXUS_GFX_EGL_SWAP_BUFFERS: {
            dump_frame();
            if (g_r.display != EGL_NO_DISPLAY && g_r.surface != EGL_NO_SURFACE) {
                if (!eglSwapBuffers(g_r.display, g_r.surface)) {
                    log_egl_error("eglSwapBuffers");
                    resp.status = NEXUS_GFX_IO_ERROR;
                }
            }
            if (g_r.windowed) SDL_PumpEvents();
            return;
        }
        case NEXUS_GFX_EGL_QUERY_SURFACE: {
            if (g_r.surface == EGL_NO_SURFACE) {
                resp.status = NEXUS_GFX_IO_ERROR;
                return;
            }
            EGLint value = 0;
            if (!eglQuerySurface(g_r.display, g_r.surface, static_cast<EGLint>(a[0]), &value)) {
                resp.status = NEXUS_GFX_IO_ERROR;
                return;
            }
            resp.i32(value);
            return;
        }
        case NEXUS_GFX_EGL_GET_ERROR:
            resp.i32(static_cast<int32_t>(eglGetError()));
            return;
        case NEXUS_GFX_EGL_TERMINATE:
            if (g_r.context != EGL_NO_CONTEXT) {
                eglMakeCurrent(g_r.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
                eglDestroyContext(g_r.display, g_r.context);
                g_r.context = EGL_NO_CONTEXT;
            }
            if (g_r.surface != EGL_NO_SURFACE) {
                eglDestroySurface(g_r.display, g_r.surface);
                g_r.surface = EGL_NO_SURFACE;
            }
            eglTerminate(g_r.display);
            g_r.initialized = false;
            return;
        default:
            resp.status = NEXUS_GFX_UNIMPLEMENTED;
            return;
    }
}

void handle_gl(const Request& req, Response& resp) {
    const uint32_t* a = req.args;
    const size_t blob_len = req.blob.size();
    const auto* f = reinterpret_cast<const GLfloat*>(req.blob.data());
    const auto* i = reinterpret_cast<const GLint*>(req.blob.data());
    const auto* u = reinterpret_cast<const GLuint*>(req.blob.data());

    switch (req.command) {
        /* ---- state ---- */
        case NEXUS_GFX_GL_ENABLE: glEnable(a[0]); return;
        case NEXUS_GFX_GL_DISABLE: glDisable(a[0]); return;
        case NEXUS_GFX_GL_BLEND_FUNC: glBlendFunc(a[0], a[1]); return;
        case NEXUS_GFX_GL_BLEND_FUNC_SEPARATE:
            glBlendFuncSeparate(a[0], a[1], a[2], a[3]);
            return;
        case NEXUS_GFX_GL_COLOR_MASK:
            glColorMask(static_cast<GLboolean>(a[0]), static_cast<GLboolean>(a[1]),
                        static_cast<GLboolean>(a[2]), static_cast<GLboolean>(a[3]));
            return;
        case NEXUS_GFX_GL_DEPTH_FUNC: glDepthFunc(a[0]); return;
        case NEXUS_GFX_GL_DEPTH_MASK: glDepthMask(static_cast<GLboolean>(a[0])); return;
        case NEXUS_GFX_GL_STENCIL_FUNC_SEPARATE:
            glStencilFuncSeparate(a[0], a[1], static_cast<GLint>(a[2]), a[3]);
            return;
        case NEXUS_GFX_GL_STENCIL_OP_SEPARATE:
            glStencilOpSeparate(a[0], a[1], a[2], a[3]);
            return;
        case NEXUS_GFX_GL_STENCIL_MASK: glStencilMask(a[0]); return;
        case NEXUS_GFX_GL_SCISSOR:
            glScissor(static_cast<GLint>(a[0]), static_cast<GLint>(a[1]),
                      static_cast<GLsizei>(a[2]), static_cast<GLsizei>(a[3]));
            return;
        case NEXUS_GFX_GL_VIEWPORT:
            glViewport(static_cast<GLint>(a[0]), static_cast<GLint>(a[1]),
                       static_cast<GLsizei>(a[2]), static_cast<GLsizei>(a[3]));
            return;
        case NEXUS_GFX_GL_CLEAR_STENCIL: glClearStencil(static_cast<GLint>(a[0])); return;
        case NEXUS_GFX_GL_CLEAR_DEPTHF:
            glClearDepthf(*reinterpret_cast<const GLfloat*>(&a[0]));
            return;
        case NEXUS_GFX_GL_CLEAR_COLOR:
            glClearColor(*reinterpret_cast<const GLfloat*>(&a[0]),
                         *reinterpret_cast<const GLfloat*>(&a[1]),
                         *reinterpret_cast<const GLfloat*>(&a[2]),
                         *reinterpret_cast<const GLfloat*>(&a[3]));
            return;
        case NEXUS_GFX_GL_CLEAR: glClear(a[0]); return;
        case NEXUS_GFX_GL_POLYGON_OFFSET:
            glPolygonOffset(*reinterpret_cast<const GLfloat*>(&a[0]),
                            *reinterpret_cast<const GLfloat*>(&a[1]));
            return;
        case NEXUS_GFX_GL_CULL_FACE: glCullFace(a[0]); return;
        case NEXUS_GFX_GL_DEPTH_RANGEF:
            glDepthRangef(*reinterpret_cast<const GLfloat*>(&a[0]),
                          *reinterpret_cast<const GLfloat*>(&a[1]));
            return;
        case NEXUS_GFX_GL_ACTIVE_TEXTURE: glActiveTexture(a[0]); return;
        case NEXUS_GFX_GL_TEX_PARAMETERI:
            glTexParameteri(a[0], a[1], static_cast<GLint>(a[2]));
            return;
        case NEXUS_GFX_GL_PIXEL_STOREI:
            glPixelStorei(a[0], static_cast<GLint>(a[1]));
            return;
        case NEXUS_GFX_GL_HINT: glHint(a[0], a[1]); return;
        case NEXUS_GFX_GL_LINE_WIDTH:
            glLineWidth(*reinterpret_cast<const GLfloat*>(&a[0]));
            return;
        case NEXUS_GFX_GL_FRONT_FACE: glFrontFace(a[0]); return;
        case NEXUS_GFX_GL_GENERATE_MIPMAP: glGenerateMipmap(a[0]); return;
        case NEXUS_GFX_GL_FLUSH: glFlush(); return;
        case NEXUS_GFX_GL_FINISH: glFinish(); return;
        case NEXUS_GFX_GL_RELEASE_SHADER_COMPILER: glReleaseShaderCompiler(); return;

        /* ---- buffers / textures ---- */
        case NEXUS_GFX_GL_GEN_BUFFERS: {
            std::vector<GLuint> ids(a[0]);
            glGenBuffers(static_cast<GLsizei>(a[0]), ids.data());
            for (GLuint id : ids) resp.u32(id);
            return;
        }
        case NEXUS_GFX_GL_BIND_BUFFER: glBindBuffer(a[0], a[1]); return;
        case NEXUS_GFX_GL_BUFFER_DATA:
            glBufferData(a[0], static_cast<GLsizeiptr>(a[1]),
                         blob_len ? req.blob.data() : nullptr, a[2]);
            return;
        case NEXUS_GFX_GL_BUFFER_SUB_DATA:
            glBufferSubData(a[0], static_cast<GLintptr>(a[1]),
                            static_cast<GLsizeiptr>(a[2]),
                            blob_len ? req.blob.data() : nullptr);
            return;
        case NEXUS_GFX_GL_DELETE_BUFFERS:
            glDeleteBuffers(static_cast<GLsizei>(a[0]), u);
            return;
        case NEXUS_GFX_GL_GEN_TEXTURES: {
            std::vector<GLuint> ids(a[0]);
            glGenTextures(static_cast<GLsizei>(a[0]), ids.data());
            for (GLuint id : ids) resp.u32(id);
            return;
        }
        case NEXUS_GFX_GL_BIND_TEXTURE: glBindTexture(a[0], a[1]); return;
        case NEXUS_GFX_GL_TEX_IMAGE_2D:
            glTexImage2D(a[0], static_cast<GLint>(a[1]), static_cast<GLint>(a[2]),
                         static_cast<GLsizei>(a[3]), static_cast<GLsizei>(a[4]),
                         static_cast<GLint>(a[5]), a[6], a[7],
                         blob_len ? req.blob.data() : nullptr);
            return;
        case NEXUS_GFX_GL_TEX_SUB_IMAGE_2D:
            glTexSubImage2D(a[0], static_cast<GLint>(a[1]), static_cast<GLint>(a[2]),
                            static_cast<GLint>(a[3]), static_cast<GLsizei>(a[4]),
                            static_cast<GLsizei>(a[5]), a[6], a[7],
                            blob_len ? req.blob.data() : nullptr);
            return;
        case NEXUS_GFX_GL_DELETE_TEXTURES:
            glDeleteTextures(static_cast<GLsizei>(a[0]), u);
            return;
        case NEXUS_GFX_GL_IS_TEXTURE: resp.i32(glIsTexture(a[0])); return;
        case NEXUS_GFX_GL_GET_TEX_PARAMETERIV: {
            GLint value = 0;
            glGetTexParameteriv(a[0], a[1], &value);
            resp.i32(value);
            return;
        }

        /* ---- framebuffers ---- */
        case NEXUS_GFX_GL_GEN_FRAMEBUFFERS: {
            std::vector<GLuint> ids(a[0]);
            glGenFramebuffers(static_cast<GLsizei>(a[0]), ids.data());
            for (GLuint id : ids) resp.u32(id);
            return;
        }
        case NEXUS_GFX_GL_BIND_FRAMEBUFFER: glBindFramebuffer(a[0], a[1]); return;
        case NEXUS_GFX_GL_DELETE_FRAMEBUFFERS:
            glDeleteFramebuffers(static_cast<GLsizei>(a[0]), u);
            return;
        case NEXUS_GFX_GL_GEN_RENDERBUFFERS: {
            std::vector<GLuint> ids(a[0]);
            glGenRenderbuffers(static_cast<GLsizei>(a[0]), ids.data());
            for (GLuint id : ids) resp.u32(id);
            return;
        }
        case NEXUS_GFX_GL_BIND_RENDERBUFFER: glBindRenderbuffer(a[0], a[1]); return;
        case NEXUS_GFX_GL_DELETE_RENDERBUFFERS:
            glDeleteRenderbuffers(static_cast<GLsizei>(a[0]), u);
            return;
        case NEXUS_GFX_GL_FRAMEBUFFER_TEXTURE_2D:
            glFramebufferTexture2D(a[0], a[1], a[2], a[3], static_cast<GLint>(a[4]));
            return;
        case NEXUS_GFX_GL_FRAMEBUFFER_RENDERBUFFER:
            glFramebufferRenderbuffer(a[0], a[1], a[2], a[3]);
            return;
        case NEXUS_GFX_GL_RENDERBUFFER_STORAGE:
            glRenderbufferStorage(a[0], a[1], static_cast<GLsizei>(a[2]),
                                  static_cast<GLsizei>(a[3]));
            return;
        case NEXUS_GFX_GL_CHECK_FRAMEBUFFER_STATUS:
            resp.u32(glCheckFramebufferStatus(a[0]));
            return;

        /* ---- shaders / programs ---- */
        case NEXUS_GFX_GL_CREATE_SHADER: resp.u32(glCreateShader(a[0])); return;
        case NEXUS_GFX_GL_SHADER_SOURCE: {
            if (blob_len == 0) return;
            size_t len = blob_len;
            if (req.blob[len - 1] == 0) len -= 1;
            const char* text = reinterpret_cast<const char*>(req.blob.data());
            const GLint length = static_cast<GLint>(len);
            glShaderSource(a[0], 1, &text, &length);
            return;
        }
        case NEXUS_GFX_GL_COMPILE_SHADER: glCompileShader(a[0]); return;
        case NEXUS_GFX_GL_GET_SHADERIV: {
            GLint value = 0;
            glGetShaderiv(a[0], a[1], &value);
            resp.i32(value);
            return;
        }
        case NEXUS_GFX_GL_GET_SHADER_INFO_LOG: {
            std::vector<char> log(a[1] + 1);
            GLsizei len = 0;
            glGetShaderInfoLog(a[0], static_cast<GLsizei>(a[1]), &len, log.data());
            log[a[1]] = 0;
            resp.str(log.data());
            return;
        }
        case NEXUS_GFX_GL_GET_SHADER_PRECISION_FORMAT: {
            GLint range[2] = {0, 0};
            GLint precision = 0;
            glGetShaderPrecisionFormat(a[0], a[1], range, &precision);
            resp.i32(range[0]);
            resp.i32(precision);
            return;
        }
        case NEXUS_GFX_GL_DELETE_SHADER: glDeleteShader(a[0]); return;
        case NEXUS_GFX_GL_CREATE_PROGRAM: resp.u32(glCreateProgram()); return;
        case NEXUS_GFX_GL_ATTACH_SHADER: glAttachShader(a[0], a[1]); return;
        case NEXUS_GFX_GL_LINK_PROGRAM: glLinkProgram(a[0]); return;
        case NEXUS_GFX_GL_GET_PROGRAMIV: {
            GLint value = 0;
            glGetProgramiv(a[0], a[1], &value);
            resp.i32(value);
            return;
        }
        case NEXUS_GFX_GL_GET_PROGRAM_INFO_LOG: {
            std::vector<char> log(a[1] + 1);
            GLsizei len = 0;
            glGetProgramInfoLog(a[0], static_cast<GLsizei>(a[1]), &len, log.data());
            log[a[1]] = 0;
            resp.str(log.data());
            return;
        }
        case NEXUS_GFX_GL_DELETE_PROGRAM: glDeleteProgram(a[0]); return;
        case NEXUS_GFX_GL_USE_PROGRAM: glUseProgram(a[0]); return;
        case NEXUS_GFX_GL_GET_ACTIVE_UNIFORM:
        case NEXUS_GFX_GL_GET_ACTIVE_ATTRIB: {
            char name[256] = {0};
            GLsizei length = 0;
            GLint size = 0;
            GLenum type = 0;
            if (req.command == NEXUS_GFX_GL_GET_ACTIVE_UNIFORM) {
                glGetActiveUniform(a[0], a[1], sizeof(name), &length, &size, &type, name);
            } else {
                glGetActiveAttrib(a[0], a[1], sizeof(name), &length, &size, &type, name);
            }
            resp.str(name);
            resp.i32(size);
            resp.u32(type);
            return;
        }
        case NEXUS_GFX_GL_GET_UNIFORM_LOCATION: {
            const char* name = blob_len ? reinterpret_cast<const char*>(req.blob.data()) : "";
            resp.i32(glGetUniformLocation(a[0], name));
            return;
        }
        case NEXUS_GFX_GL_GET_ATTRIB_LOCATION: {
            const char* name = blob_len ? reinterpret_cast<const char*>(req.blob.data()) : "";
            resp.i32(glGetAttribLocation(a[0], name));
            return;
        }

        /* ---- uniforms / attributes ---- */
        case NEXUS_GFX_GL_UNIFORM1I: glUniform1i(static_cast<GLint>(a[0]), static_cast<GLint>(a[1])); return;
        case NEXUS_GFX_GL_UNIFORM1IV: glUniform1iv(static_cast<GLint>(a[0]), static_cast<GLsizei>(a[1]), i); return;
        case NEXUS_GFX_GL_UNIFORM1FV: glUniform1fv(static_cast<GLint>(a[0]), static_cast<GLsizei>(a[1]), f); return;
        case NEXUS_GFX_GL_UNIFORM2IV: glUniform2iv(static_cast<GLint>(a[0]), static_cast<GLsizei>(a[1]), i); return;
        case NEXUS_GFX_GL_UNIFORM2FV: glUniform2fv(static_cast<GLint>(a[0]), static_cast<GLsizei>(a[1]), f); return;
        case NEXUS_GFX_GL_UNIFORM3IV: glUniform3iv(static_cast<GLint>(a[0]), static_cast<GLsizei>(a[1]), i); return;
        case NEXUS_GFX_GL_UNIFORM3FV: glUniform3fv(static_cast<GLint>(a[0]), static_cast<GLsizei>(a[1]), f); return;
        case NEXUS_GFX_GL_UNIFORM4IV: glUniform4iv(static_cast<GLint>(a[0]), static_cast<GLsizei>(a[1]), i); return;
        case NEXUS_GFX_GL_UNIFORM4FV: glUniform4fv(static_cast<GLint>(a[0]), static_cast<GLsizei>(a[1]), f); return;
        case NEXUS_GFX_GL_UNIFORM_MATRIX2FV:
            glUniformMatrix2fv(static_cast<GLint>(a[0]), static_cast<GLsizei>(a[1]),
                               static_cast<GLboolean>(a[2]), f);
            return;
        case NEXUS_GFX_GL_UNIFORM_MATRIX3FV:
            glUniformMatrix3fv(static_cast<GLint>(a[0]), static_cast<GLsizei>(a[1]),
                               static_cast<GLboolean>(a[2]), f);
            return;
        case NEXUS_GFX_GL_UNIFORM_MATRIX4FV:
            glUniformMatrix4fv(static_cast<GLint>(a[0]), static_cast<GLsizei>(a[1]),
                               static_cast<GLboolean>(a[2]), f);
            return;
        case NEXUS_GFX_GL_ENABLE_VERTEX_ATTRIB_ARRAY: glEnableVertexAttribArray(a[0]); return;
        case NEXUS_GFX_GL_DISABLE_VERTEX_ATTRIB_ARRAY: glDisableVertexAttribArray(a[0]); return;
        case NEXUS_GFX_GL_VERTEX_ATTRIB_POINTER:
            glVertexAttribPointer(a[0], static_cast<GLint>(a[1]), a[2],
                                  static_cast<GLboolean>(a[3]), static_cast<GLsizei>(a[4]),
                                  reinterpret_cast<const void*>(static_cast<uintptr_t>(a[5])));
            return;

        /* ---- draws / queries ---- */
        case NEXUS_GFX_GL_DRAW_ARRAYS:
            glDrawArrays(a[0], static_cast<GLint>(a[1]), static_cast<GLsizei>(a[2]));
            return;
        case NEXUS_GFX_GL_DRAW_ELEMENTS:
            glDrawElements(a[0], static_cast<GLsizei>(a[1]), a[2],
                           blob_len ? req.blob.data()
                                    : reinterpret_cast<const void*>(static_cast<uintptr_t>(a[3])));
            return;
        case NEXUS_GFX_GL_GET_INTEGERV: {
            int count = 1;
            switch (a[0]) {
                case GL_VIEWPORT:
                case GL_SCISSOR_BOX:
                    count = 4;
                    break;
                case GL_MAX_VIEWPORT_DIMS:
                    count = 2;
                    break;
                case GL_COMPRESSED_TEXTURE_FORMATS:
                    count = 0;
                    break;
                default:
                    count = 1;
                    break;
            }
            GLint values[16] = {0};
            if (count > 0) glGetIntegerv(a[0], values);
            resp.u32(static_cast<uint32_t>(count));
            for (int i = 0; i < count; ++i) resp.i32(values[i]);
            return;
        }
        case NEXUS_GFX_GL_GET_STRING: {
            const GLubyte* s = glGetString(a[0]);
            resp.str(s ? reinterpret_cast<const char*>(s) : "");
            return;
        }
        case NEXUS_GFX_GL_GET_ERROR: resp.u32(glGetError()); return;
        case NEXUS_GFX_GL_READ_PIXELS: {
            const GLsizei width = static_cast<GLsizei>(a[2]);
            const GLsizei height = static_cast<GLsizei>(a[3]);
            // Conservative maximum: 4 bytes per pixel.
            std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(static_cast<GLint>(a[0]), static_cast<GLint>(a[1]), width, height,
                         a[4], a[5], pixels.data());
            resp.u32(static_cast<uint32_t>(pixels.size()));
            resp.bytes(pixels.data(), pixels.size());
            return;
        }
        default:
            resp.status = NEXUS_GFX_UNIMPLEMENTED;
            return;
    }
}

void dispatch(const Request& req, Response& resp) {
    if (req.command >= 0x0100 && req.command < 0x0200) {
        handle_egl(req, resp);
    } else if (req.command >= 0x0200 && req.command < 0x0300) {
        handle_gl(req, resp);
    } else {
        resp.status = NEXUS_GFX_BAD_REQUEST;
    }
}

/* ------------------------------------------------------------------------- */
/* server                                                                    */
/* ------------------------------------------------------------------------- */

bool serve_connection(int fd) {
    while (true) {
        uint32_t header[2] = {0, 0};
        if (!read_all(fd, header, sizeof(header))) return true;  // clean disconnect
        if (header[1] > (64u << 20)) {
            std::fprintf(stderr, "[gfx-host] oversized payload: %u\n", header[1]);
            return false;
        }
        std::vector<uint8_t> payload(header[1]);
        if (header[1] && !read_all(fd, payload.data(), payload.size())) return false;

        Request req;
        req.command = header[0];
        std::memcpy(req.args, payload.data(), std::min<size_t>(payload.size(), 64));
        if (payload.size() > 64) req.blob.assign(payload.begin() + 64, payload.end());

        Response resp;
        dispatch(req, resp);

        uint32_t rh[2] = {static_cast<uint32_t>(resp.status),
                          static_cast<uint32_t>(resp.payload.size())};
        if (!write_all(fd, rh, sizeof(rh))) return false;
        if (!resp.payload.empty() && !write_all(fd, resp.payload.data(), resp.payload.size()))
            return false;
    }
}

void renderer_shutdown() {
    if (g_r.display != EGL_NO_DISPLAY) {
        if (g_r.context != EGL_NO_CONTEXT) {
            eglMakeCurrent(g_r.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            eglDestroyContext(g_r.display, g_r.context);
        }
        if (g_r.surface != EGL_NO_SURFACE) eglDestroySurface(g_r.display, g_r.surface);
        eglTerminate(g_r.display);
    }
    if (g_r.window) SDL_DestroyWindow(g_r.window);
    if (g_r.windowed) SDL_Quit();
}

int usage(const char* argv0) {
    std::fprintf(stderr,
                 "usage: %s --socket PATH [--headless|--window] [--size WxH]\n"
                 "          [--dump frame.png]\n",
                 argv0);
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    std::string socket_path;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--socket" && i + 1 < argc) {
            socket_path = argv[++i];
        } else if (arg == "--headless") {
            g_r.windowed = false;
        } else if (arg == "--window") {
            g_r.windowed = true;
        } else if (arg == "--size" && i + 1 < argc) {
            std::sscanf(argv[++i], "%dx%d", &g_r.width, &g_r.height);
        } else if (arg == "--dump" && i + 1 < argc) {
            g_r.dump_path = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            return usage(argv[0]);
        } else {
            std::fprintf(stderr, "[gfx-host] unknown argument: %s\n", arg.c_str());
            return usage(argv[0]);
        }
    }
    if (socket_path.empty()) return usage(argv[0]);

    ::unlink(socket_path.c_str());
    const int server = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (server < 0) {
        std::perror("[gfx-host] socket");
        return 1;
    }
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (socket_path.size() >= sizeof(addr.sun_path)) {
        std::fprintf(stderr, "[gfx-host] socket path too long\n");
        return 1;
    }
    std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", socket_path.c_str());
    if (::bind(server, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::perror("[gfx-host] bind");
        ::close(server);
        return 1;
    }
    if (::listen(server, 1) != 0) {
        std::perror("[gfx-host] listen");
        ::close(server);
        return 1;
    }
    std::fprintf(stderr, "[gfx-host] listening on %s\n", socket_path.c_str());
    std::fflush(stderr);

    bool ok = true;
    while (true) {
        const int client = ::accept(server, nullptr, nullptr);
        if (client < 0) {
            if (errno == EINTR) continue;
            std::perror("[gfx-host] accept");
            ok = false;
            break;
        }
        std::fprintf(stderr, "[gfx-host] guest connected\n");
        std::fflush(stderr);
        ok = serve_connection(client);
        ::close(client);
        std::fprintf(stderr, "[gfx-host] guest disconnected\n");
        std::fflush(stderr);
        break;
    }

    renderer_shutdown();
    ::close(server);
    ::unlink(socket_path.c_str());
    return ok ? 0 : 1;
}
