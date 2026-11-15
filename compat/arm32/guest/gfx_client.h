#ifndef NEXUS_ARM32_GFX_CLIENT_H
#define NEXUS_ARM32_GFX_CLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Connect the ARM32 guest to the x86_64 host renderer.  Returns 0 on success.
 * The session frontend passes the socket path to the guest; NEXUS_GFX_SOCKET
 * is used as a fallback for the smoke tests.
 */
int nexus_gfx_connect(const char* socket_path);
void nexus_gfx_disconnect(void);

/*
 * Register the EGL/GLES2 shim entry points in the in-tree ELF loader symbol
 * registry.  Must be called before relocating libminecraftpe.so.
 */
void nexus_register_gfx_symbols(void);

/* True once the transport is usable. */
int nexus_gfx_connected(void);

#ifdef __cplusplus
}
#endif

#endif /* NEXUS_ARM32_GFX_CLIENT_H */
