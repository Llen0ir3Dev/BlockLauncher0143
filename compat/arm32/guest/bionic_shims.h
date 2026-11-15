#ifndef NEXUS_GUEST_BIONIC_SHIMS_H
#define NEXUS_GUEST_BIONIC_SHIMS_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Register the guest-side Android platform shims (liblog, libandroid,
 * ANativeWindow, ALooper, AInputQueue, AAssetManager, AConfiguration) in the
 * in-tree ELF loader symbol registry.
 *
 * Must be called before relocating libminecraftpe.so.  The shims are
 * deliberately ordinary C functions: the loader resolves the genuine
 * library's imports to these addresses.
 */
void nexus_register_android_shims(void);

/*
 * Return non-zero once the game called ANativeActivity_finish().  The session
 * loop uses this to stop pumping events.
 */
int nexus_activity_should_finish(void);

#ifdef __cplusplus
}
#endif

#endif /* NEXUS_GUEST_BIONIC_SHIMS_H */
