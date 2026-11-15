/*
 * ARM32 guest graphics bridge smoke test.
 *
 * Runs under qemu-arm, connects to nexus_gfx_host over a Unix socket and
 * exercises the same EGL/GLES2 entry points the genuine game imports:
 *
 *   EGL init / choose config / window surface / context / make current
 *   GL strings, state, clear, readback
 *   VBO, shader compile/link, attribute lookup, draw, readback
 *   eglSwapBuffers
 *
 * A pass means the guest really drove the host Mesa context through the
 * protocol, not that a stub returned success.
 */
#include "gfx_client.h"
#include "loader_symbols.h"

#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * The smoke binary does not include the in-tree ELF loader, so the symbol
 * registration hook is a no-op here.  The real loader links elf_loader.c and
 * uses the genuine implementation.
 */
void nexus_loader_add_symbols(const nexus_symbol_entry* entries, size_t count) {
    (void)entries;
    (void)count;
}

static int fail(const char* what) {
    fprintf(stderr, "gfx_smoke: FAIL: %s (gl=0x%x egl=0x%x)\n", what,
            (unsigned)glGetError(), (unsigned)eglGetError());
    return 1;
}

static int pixel_close(const unsigned char* px, int r, int g, int b, int tolerance) {
    return abs((int)px[0] - r) <= tolerance &&
           abs((int)px[1] - g) <= tolerance &&
           abs((int)px[2] - b) <= tolerance;
}

static const char* k_vertex_shader =
    "attribute vec2 aPos;\n"
    "attribute vec2 aUV;\n"
    "varying vec2 vUV;\n"
    "void main() { vUV = aUV; gl_Position = vec4(aPos, 0.0, 1.0); }\n";

static const char* k_fragment_shader =
    "precision mediump float;\n"
    "varying vec2 vUV;\n"
    "uniform sampler2D uTex;\n"
    "void main() { gl_FragColor = texture2D(uTex, vUV); }\n";

static GLuint compile(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    if (!shader) return 0;
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = {0};
        glGetShaderInfoLog(shader, sizeof(log) - 1, NULL, log);
        fprintf(stderr, "gfx_smoke: shader compile failed: %s\n", log);
        return 0;
    }
    return shader;
}

int main(int argc, char** argv) {
    const char* socket_path = argc > 1 ? argv[1] : getenv("NEXUS_GFX_SOCKET");
    if (!socket_path || !*socket_path) {
        fprintf(stderr, "usage: %s SOCKET\n", argv[0]);
        return 2;
    }
    if (nexus_gfx_connect(socket_path) != 0) {
        fprintf(stderr, "gfx_smoke: FAIL: cannot connect to %s\n", socket_path);
        return 1;
    }

    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major = 0, minor = 0;
    if (!eglInitialize(dpy, &major, &minor)) return fail("eglInitialize");
    printf("gfx_smoke: EGL %d.%d\n", major, minor);

    const EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE,
    };
    EGLConfig config = NULL;
    EGLint config_count = 0;
    if (!eglChooseConfig(dpy, config_attribs, &config, 1, &config_count) ||
        config_count < 1) {
        return fail("eglChooseConfig");
    }

    const EGLint surface_attribs[] = {EGL_NONE};
    EGLSurface surface = eglCreateWindowSurface(
        dpy, config, (EGLNativeWindowType)(uintptr_t)0x1234, surface_attribs);
    if (surface == EGL_NO_SURFACE) return fail("eglCreateWindowSurface");

    const EGLint context_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    EGLContext context = eglCreateContext(dpy, config, EGL_NO_CONTEXT, context_attribs);
    if (context == EGL_NO_CONTEXT) return fail("eglCreateContext");
    if (!eglMakeCurrent(dpy, surface, surface, context)) return fail("eglMakeCurrent");

    printf("gfx_smoke: GL_VENDOR   %s\n", glGetString(GL_VENDOR));
    printf("gfx_smoke: GL_RENDERER %s\n", glGetString(GL_RENDERER));
    printf("gfx_smoke: GL_VERSION  %s\n", glGetString(GL_VERSION));

    GLint max_texture = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture);
    if (max_texture <= 0) return fail("GL_MAX_TEXTURE_SIZE");

    GLint viewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, viewport);
    const int width = viewport[2];
    const int height = viewport[3];
    if (width <= 0 || height <= 0) return fail("GL_VIEWPORT");
    printf("gfx_smoke: viewport %dx%d max_texture=%d\n", width, height, max_texture);

    /* 1. Clear the whole buffer and verify the colour came back. */
    glClearColor(0.1f, 0.2f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    unsigned char clear_pixel[4] = {0, 0, 0, 0};
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, clear_pixel);
    printf("gfx_smoke: clear pixel %d,%d,%d,%d\n", clear_pixel[0], clear_pixel[1],
           clear_pixel[2], clear_pixel[3]);
    if (!pixel_close(clear_pixel, 26, 51, 77, 3)) return fail("clear readback");

    /* 2. Real shader compile, VBO upload, draw and readback. */
    GLuint vs = compile(GL_VERTEX_SHADER, k_vertex_shader);
    GLuint fs = compile(GL_FRAGMENT_SHADER, k_fragment_shader);
    if (!vs || !fs) return fail("shader compile");

    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[1024] = {0};
        glGetProgramInfoLog(program, sizeof(log) - 1, NULL, log);
        fprintf(stderr, "gfx_smoke: link failed: %s\n", log);
        return fail("link");
    }
    glUseProgram(program);

    /* 2x2 texture, uploaded from the guest: any UV must sample magenta. */
    const unsigned char texels[2 * 2 * 4] = {
        255, 0, 255, 255, 255, 0, 255, 255,
        255, 0, 255, 255, 255, 0, 255, 255,
    };
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
    const GLint sampler = glGetUniformLocation(program, "uTex");
    if (sampler < 0) return fail("glGetUniformLocation(uTex)");
    glUniform1i(sampler, 0);

    /* Interleaved position + UV, so the attribute and uniform paths are real. */
    const GLfloat vertices[] = {
        -0.8f, -0.8f, 0.0f, 0.0f,
         0.8f, -0.8f, 1.0f, 0.0f,
         0.0f,  0.8f, 0.5f, 1.0f,
    };
    GLuint vbo = 0;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    const GLint pos_attrib = glGetAttribLocation(program, "aPos");
    const GLint uv_attrib = glGetAttribLocation(program, "aUV");
    if (pos_attrib < 0 || uv_attrib < 0) return fail("glGetAttribLocation");
    glEnableVertexAttribArray((GLuint)pos_attrib);
    glVertexAttribPointer((GLuint)pos_attrib, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat),
                          (const void*)0);
    glEnableVertexAttribArray((GLuint)uv_attrib);
    glVertexAttribPointer((GLuint)uv_attrib, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat),
                          (const void*)(2 * sizeof(GLfloat)));
    glDrawArrays(GL_TRIANGLES, 0, 3);

    unsigned char centre[4] = {0, 0, 0, 0};
    glReadPixels(width / 2, height / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, centre);
    printf("gfx_smoke: centre pixel %d,%d,%d,%d\n", centre[0], centre[1], centre[2],
           centre[3]);
    if (!pixel_close(centre, 255, 0, 255, 6)) return fail("textured triangle readback");

    /* A corner must still hold the clear colour (the triangle does not cover it). */
    unsigned char corner[4] = {0, 0, 0, 0};
    glReadPixels(width - 2, height - 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, corner);
    if (!pixel_close(corner, 26, 51, 77, 3)) return fail("corner readback");

    if (glGetError() != GL_NO_ERROR) return fail("glGetError before swap");
    if (!eglSwapBuffers(dpy, surface)) return fail("eglSwapBuffers");

    glDeleteTextures(1, &texture);
    glDeleteBuffers(1, &vbo);
    glDeleteShader(vs);
    glDeleteShader(fs);
    glDeleteProgram(program);
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(dpy, context);
    eglDestroySurface(dpy, surface);
    eglTerminate(dpy);
    nexus_gfx_disconnect();

    printf("gfx_smoke: PASS\n");
    return 0;
}
