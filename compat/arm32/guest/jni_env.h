#ifndef NEXUS_GUEST_JNI_ENV_H
#define NEXUS_GUEST_JNI_ENV_H

#include <jni.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Build the synthetic JavaVM/JNIEnv tables. Safe to call more than once. */
void nexus_jni_install(void);

/* The facade handles used by the activity/session layer. */
JavaVM* nexus_jni_vm(void);
JNIEnv* nexus_jni_env(void);

/*
 * Invoke the genuine JNI_OnLoad entry point of libminecraftpe.so through the
 * supplied function address.  Returns the value returned by the library, or a
 * negative value if the entry point is missing.
 */
jint nexus_run_jni_onload(void* fn, int log_calls);

#ifdef __cplusplus
}
#endif

#endif /* NEXUS_GUEST_JNI_ENV_H */
