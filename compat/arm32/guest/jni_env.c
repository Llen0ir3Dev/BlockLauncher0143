/*
 * Synthetic JavaVM/JNIEnv facade for the genuine Minecraft PE 0.14.3 ARM32
 * library.
 *
 * The game is an Android NativeActivity: Java only supplies the activity shell
 * and a handful of callbacks.  NEXUS runs the native side directly, so this
 * facade answers the JNI calls the native library makes during JNI_OnLoad and
 * activity startup.  It logs every class/method lookup so missing Java
 * behaviour is visible instead of silently faked.
 */
#include "jni_env.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_log_jni = 1;

#define LOG(...) do { \
    if (g_log_jni) { fprintf(stderr, "[jni] " __VA_ARGS__); fputc('\n', stderr); } \
} while (0)

#define JCLASS_DUMMY   ((jclass)(uintptr_t)0x1001)
#define JMETHOD_DUMMY  ((jmethodID)(uintptr_t)0x2001)
#define JFIELD_DUMMY   ((jfieldID)(uintptr_t)0x3001)
#define JSTRING_DUMMY  ((jstring)(uintptr_t)0x4001)
#define JOBJECT_DUMMY  ((jobject)(uintptr_t)0x5001)
#define JARRAY_DUMMY   ((jarray)(uintptr_t)0x6001)

static const char k_empty[] = "";

/* ------------------------------------------------------------------------- */
/* JNIEnv entry points                                                       */
/* ------------------------------------------------------------------------- */

static jint JNICALL sh_GetVersion(JNIEnv* env) { (void)env; return 0x00010006; }

static jclass JNICALL sh_FindClass(JNIEnv* env, const char* name) {
    (void)env;
    LOG("FindClass(\"%s\")", name ? name : "(null)");
    return JCLASS_DUMMY;
}

static jclass JNICALL sh_GetObjectClass(JNIEnv* env, jobject o) {
    (void)env; (void)o; return JCLASS_DUMMY;
}

static jclass JNICALL sh_GetSuperclass(JNIEnv* env, jclass c) {
    (void)env; (void)c; return JCLASS_DUMMY;
}

static jboolean JNICALL sh_IsAssignableFrom(JNIEnv* env, jclass a, jclass b) {
    (void)env; (void)a; (void)b; return JNI_TRUE;
}

static jint JNICALL sh_Throw(JNIEnv* env, jthrowable t) { (void)env; (void)t; return 0; }

static jint JNICALL sh_ThrowNew(JNIEnv* env, jclass c, const char* m) {
    (void)env; (void)c; LOG("ThrowNew(%s)", m ? m : ""); return 0;
}

static jthrowable JNICALL sh_ExceptionOccurred(JNIEnv* env) { (void)env; return NULL; }
static void JNICALL sh_ExceptionDescribe(JNIEnv* env) { (void)env; }
static void JNICALL sh_ExceptionClear(JNIEnv* env) { (void)env; }

static void JNICALL sh_FatalError(JNIEnv* env, const char* m) {
    (void)env; fprintf(stderr, "[jni] FatalError: %s\n", m ? m : ""); abort();
}

static jint JNICALL sh_PushLocalFrame(JNIEnv* env, jint cap) { (void)env; (void)cap; return 0; }
static jobject JNICALL sh_PopLocalFrame(JNIEnv* env, jobject r) { (void)env; return r; }
static jint JNICALL sh_EnsureLocalCapacity(JNIEnv* env, jint cap) { (void)env; (void)cap; return 0; }

static jobject JNICALL sh_NewGlobalRef(JNIEnv* env, jobject o) {
    (void)env; LOG("NewGlobalRef(%p)", (void*)o); return o;
}
static void JNICALL sh_DeleteGlobalRef(JNIEnv* env, jobject o) { (void)env; (void)o; }
static void JNICALL sh_DeleteLocalRef(JNIEnv* env, jobject o) { (void)env; (void)o; }
static jboolean JNICALL sh_IsSameObject(JNIEnv* env, jobject a, jobject b) {
    (void)env; return a == b;
}
static jobject JNICALL sh_NewLocalRef(JNIEnv* env, jobject o) { (void)env; return o; }

static jobject JNICALL sh_AllocObject(JNIEnv* env, jclass c) {
    (void)env; (void)c; return JOBJECT_DUMMY;
}
static jobject JNICALL sh_NewObject(JNIEnv* env, jclass c, jmethodID m, ...) {
    (void)env; (void)c; (void)m; return JOBJECT_DUMMY;
}
static jobject JNICALL sh_NewObjectV(JNIEnv* env, jclass c, jmethodID m, va_list a) {
    (void)env; (void)c; (void)m; (void)a; return JOBJECT_DUMMY;
}
static jobject JNICALL sh_NewObjectA(JNIEnv* env, jclass c, jmethodID m, const jvalue* a) {
    (void)env; (void)c; (void)m; (void)a; return JOBJECT_DUMMY;
}

static jmethodID JNICALL sh_GetMethodID(JNIEnv* env, jclass c, const char* name,
                                        const char* sig) {
    (void)env; (void)c;
    LOG("GetMethodID(\"%s\", \"%s\")", name ? name : "", sig ? sig : "");
    return JMETHOD_DUMMY;
}
static jmethodID JNICALL sh_GetStaticMethodID(JNIEnv* env, jclass c, const char* name,
                                              const char* sig) {
    (void)env; (void)c;
    LOG("GetStaticMethodID(\"%s\", \"%s\")", name ? name : "", sig ? sig : "");
    return JMETHOD_DUMMY;
}
static jfieldID JNICALL sh_GetFieldID(JNIEnv* env, jclass c, const char* name,
                                      const char* sig) {
    (void)env; (void)c;
    LOG("GetFieldID(\"%s\", \"%s\")", name ? name : "", sig ? sig : "");
    return JFIELD_DUMMY;
}
static jfieldID JNICALL sh_GetStaticFieldID(JNIEnv* env, jclass c, const char* name,
                                            const char* sig) {
    (void)env; (void)c;
    LOG("GetStaticFieldID(\"%s\", \"%s\")", name ? name : "", sig ? sig : "");
    return JFIELD_DUMMY;
}

static jint JNICALL sh_RegisterNatives(JNIEnv* env, jclass c, const JNINativeMethod* m,
                                       jint n) {
    (void)env; (void)c;
    LOG("RegisterNatives(count=%d)", (int)n);
    for (int i = 0; i < n; ++i) {
        LOG("  native %s %s -> %p", m[i].name ? m[i].name : "",
            m[i].signature ? m[i].signature : "", m[i].fnPtr);
    }
    return 0;
}
static jint JNICALL sh_UnregisterNatives(JNIEnv* env, jclass c) { (void)env; (void)c; return 0; }

static jint JNICALL sh_GetJavaVM(JNIEnv* env, JavaVM** vm);

/* strings */
static jstring JNICALL sh_NewStringUTF(JNIEnv* env, const char* s) {
    (void)env; LOG("NewStringUTF(\"%s\")", s ? s : ""); return JSTRING_DUMMY;
}
static jsize JNICALL sh_GetStringUTFLength(JNIEnv* env, jstring s) {
    (void)env; (void)s; return 0;
}
static const char* JNICALL sh_GetStringUTFChars(JNIEnv* env, jstring s, jboolean* isCopy) {
    (void)env; (void)s; if (isCopy) *isCopy = JNI_FALSE; return k_empty;
}
static void JNICALL sh_ReleaseStringUTFChars(JNIEnv* env, jstring s, const char* c) {
    (void)env; (void)s; (void)c;
}
static jsize JNICALL sh_GetStringLength(JNIEnv* env, jstring s) { (void)env; (void)s; return 0; }
static const jchar* JNICALL sh_GetStringChars(JNIEnv* env, jstring s, jboolean* isCopy) {
    (void)env; (void)s; if (isCopy) *isCopy = JNI_FALSE; return NULL;
}
static void JNICALL sh_ReleaseStringChars(JNIEnv* env, jstring s, const jchar* c) {
    (void)env; (void)s; (void)c;
}
static jstring JNICALL sh_NewString(JNIEnv* env, const jchar* u, jsize n) {
    (void)env; (void)u; (void)n; return JSTRING_DUMMY;
}
static void JNICALL sh_GetStringRegion(JNIEnv* env, jstring s, jsize start, jsize len, jchar* buf) {
    (void)env; (void)s; (void)start; (void)len;
    if (buf && len > 0) buf[0] = 0;
}
static void JNICALL sh_GetStringUTFRegion(JNIEnv* env, jstring s, jsize start, jsize len,
                                          char* buf) {
    (void)env; (void)s; (void)start;
    if (buf && len > 0) buf[0] = 0;
}

/* static calls */
static void JNICALL sh_CallStaticVoidMethod(JNIEnv* env, jclass c, jmethodID m, ...) {
    (void)env; (void)c; (void)m; LOG("CallStaticVoidMethod(%p)", (void*)m);
}
static jint JNICALL sh_CallStaticIntMethod(JNIEnv* env, jclass c, jmethodID m, ...) {
    (void)env; (void)c; (void)m; LOG("CallStaticIntMethod(%p)", (void*)m); return 0;
}
static jboolean JNICALL sh_CallStaticBooleanMethod(JNIEnv* env, jclass c, jmethodID m, ...) {
    (void)env; (void)c; (void)m; return JNI_FALSE;
}
static jlong JNICALL sh_CallStaticLongMethod(JNIEnv* env, jclass c, jmethodID m, ...) {
    (void)env; (void)c; (void)m; return 0;
}
static jfloat JNICALL sh_CallStaticFloatMethod(JNIEnv* env, jclass c, jmethodID m, ...) {
    (void)env; (void)c; (void)m; return 0.0f;
}
static jdouble JNICALL sh_CallStaticDoubleMethod(JNIEnv* env, jclass c, jmethodID m, ...) {
    (void)env; (void)c; (void)m; return 0.0;
}
static jobject JNICALL sh_CallStaticObjectMethod(JNIEnv* env, jclass c, jmethodID m, ...) {
    (void)env; (void)c; (void)m; LOG("CallStaticObjectMethod(%p)", (void*)m); return JSTRING_DUMMY;
}

/* instance calls */
static void JNICALL sh_CallVoidMethod(JNIEnv* env, jobject o, jmethodID m, ...) {
    (void)env; (void)o; (void)m;
}
static jint JNICALL sh_CallIntMethod(JNIEnv* env, jobject o, jmethodID m, ...) {
    (void)env; (void)o; (void)m; return 0;
}
static jboolean JNICALL sh_CallBooleanMethod(JNIEnv* env, jobject o, jmethodID m, ...) {
    (void)env; (void)o; (void)m; return JNI_FALSE;
}
static jlong JNICALL sh_CallLongMethod(JNIEnv* env, jobject o, jmethodID m, ...) {
    (void)env; (void)o; (void)m; return 0;
}
static jfloat JNICALL sh_CallFloatMethod(JNIEnv* env, jobject o, jmethodID m, ...) {
    (void)env; (void)o; (void)m; return 0.0f;
}
static jdouble JNICALL sh_CallDoubleMethod(JNIEnv* env, jobject o, jmethodID m, ...) {
    (void)env; (void)o; (void)m; return 0.0;
}
static jobject JNICALL sh_CallObjectMethod(JNIEnv* env, jobject o, jmethodID m, ...) {
    (void)env; (void)o; (void)m; return JSTRING_DUMMY;
}

/* fields */
static jboolean JNICALL sh_GetBooleanField(JNIEnv* e, jobject o, jfieldID f) {
    (void)e; (void)o; (void)f; return JNI_FALSE;
}
static jint JNICALL sh_GetIntField(JNIEnv* e, jobject o, jfieldID f) {
    (void)e; (void)o; (void)f; return 0;
}
static jlong JNICALL sh_GetLongField(JNIEnv* e, jobject o, jfieldID f) {
    (void)e; (void)o; (void)f; return 0;
}
static jfloat JNICALL sh_GetFloatField(JNIEnv* e, jobject o, jfieldID f) {
    (void)e; (void)o; (void)f; return 0.0f;
}
static jobject JNICALL sh_GetObjectField(JNIEnv* e, jobject o, jfieldID f) {
    (void)e; (void)o; (void)f; return NULL;
}
static void JNICALL sh_SetBooleanField(JNIEnv* e, jobject o, jfieldID f, jboolean v) {
    (void)e; (void)o; (void)f; (void)v;
}
static void JNICALL sh_SetIntField(JNIEnv* e, jobject o, jfieldID f, jint v) {
    (void)e; (void)o; (void)f; (void)v;
}
static void JNICALL sh_SetLongField(JNIEnv* e, jobject o, jfieldID f, jlong v) {
    (void)e; (void)o; (void)f; (void)v;
}
static void JNICALL sh_SetFloatField(JNIEnv* e, jobject o, jfieldID f, jfloat v) {
    (void)e; (void)o; (void)f; (void)v;
}
static void JNICALL sh_SetObjectField(JNIEnv* e, jobject o, jfieldID f, jobject v) {
    (void)e; (void)o; (void)f; (void)v;
}

/* arrays */
static jsize JNICALL sh_GetArrayLength(JNIEnv* e, jarray a) { (void)e; (void)a; return 0; }
static jbooleanArray JNICALL sh_NewBooleanArray(JNIEnv* e, jsize n) {
    (void)e; (void)n; return (jbooleanArray)JARRAY_DUMMY;
}
static jbyteArray JNICALL sh_NewByteArray(JNIEnv* e, jsize n) {
    (void)e; (void)n; return (jbyteArray)JARRAY_DUMMY;
}
static jintArray JNICALL sh_NewIntArray(JNIEnv* e, jsize n) {
    (void)e; (void)n; return (jintArray)JARRAY_DUMMY;
}
static jobjectArray JNICALL sh_NewObjectArray(JNIEnv* e, jsize n, jclass c, jobject init) {
    (void)e; (void)n; (void)c; (void)init; return (jobjectArray)JARRAY_DUMMY;
}
static jboolean* JNICALL sh_GetBooleanArrayElements(JNIEnv* e, jbooleanArray a, jboolean* c) {
    (void)e; (void)a; (void)c; return NULL;
}
static jbyte* JNICALL sh_GetByteArrayElements(JNIEnv* e, jbyteArray a, jboolean* c) {
    (void)e; (void)a; (void)c; return NULL;
}
static jint* JNICALL sh_GetIntArrayElements(JNIEnv* e, jintArray a, jboolean* c) {
    (void)e; (void)a; (void)c; return NULL;
}
static void JNICALL sh_ReleaseBooleanArrayElements(JNIEnv* e, jbooleanArray a, jboolean* p,
                                                   jint m) {
    (void)e; (void)a; (void)p; (void)m;
}
static void JNICALL sh_ReleaseByteArrayElements(JNIEnv* e, jbyteArray a, jbyte* p, jint m) {
    (void)e; (void)a; (void)p; (void)m;
}
static void JNICALL sh_ReleaseIntArrayElements(JNIEnv* e, jintArray a, jint* p, jint m) {
    (void)e; (void)a; (void)p; (void)m;
}

static jint JNICALL sh_monitorEnter(JNIEnv* e, jobject o) { (void)e; (void)o; return 0; }
static jint JNICALL sh_monitorExit(JNIEnv* e, jobject o) { (void)e; (void)o; return 0; }

/* ------------------------------------------------------------------------- */
/* JavaVM entry points                                                       */
/* ------------------------------------------------------------------------- */

static struct JNINativeInterface g_native;
static JNIEnv g_env = (JNIEnv)&g_native;
static struct JNIInvokeInterface g_invoke;
static JavaVM g_vm = (JavaVM)&g_invoke;
static int g_jni_installed = 0;

static jint JNICALL sh_GetJavaVM(JNIEnv* env, JavaVM** vm) {
    (void)env;
    if (vm) *vm = &g_vm;
    return 0;
}

static jint JNICALL vm_DestroyJavaVM(JavaVM* vm) { (void)vm; return 0; }

static jint JNICALL vm_AttachCurrentThread(JavaVM* vm, JNIEnv** penv, void* args) {
    (void)vm; (void)args;
    if (penv) *penv = &g_env;
    return 0;
}

static jint JNICALL vm_DetachCurrentThread(JavaVM* vm) { (void)vm; return 0; }

static jint JNICALL vm_GetEnv(JavaVM* vm, void** penv, jint version) {
    (void)vm; (void)version;
    if (penv) *penv = (void*)&g_env;
    return 0;
}

static jint JNICALL vm_AttachCurrentThreadAsDaemon(JavaVM* vm, JNIEnv** penv, void* args) {
    (void)vm; (void)args;
    if (penv) *penv = &g_env;
    return 0;
}

/* ------------------------------------------------------------------------- */
/* table construction                                                        */
/* ------------------------------------------------------------------------- */

static jint JNICALL generic_stub(void) { return 0; }

static void install_native_table(void) {
    struct JNINativeInterface* t = (struct JNINativeInterface*)&g_native;
    void** raw = (void**)t;
    size_t n = sizeof(g_native) / sizeof(void*);
    for (size_t i = 0; i < n; ++i) raw[i] = (void*)&generic_stub;

    t->GetVersion = sh_GetVersion;
    t->FindClass = sh_FindClass;
    t->GetObjectClass = sh_GetObjectClass;
    t->GetSuperclass = sh_GetSuperclass;
    t->IsAssignableFrom = sh_IsAssignableFrom;
    t->Throw = sh_Throw;
    t->ThrowNew = sh_ThrowNew;
    t->ExceptionOccurred = sh_ExceptionOccurred;
    t->ExceptionDescribe = sh_ExceptionDescribe;
    t->ExceptionClear = sh_ExceptionClear;
    t->FatalError = sh_FatalError;
    t->PushLocalFrame = sh_PushLocalFrame;
    t->PopLocalFrame = sh_PopLocalFrame;
    t->EnsureLocalCapacity = sh_EnsureLocalCapacity;
    t->NewGlobalRef = sh_NewGlobalRef;
    t->DeleteGlobalRef = sh_DeleteGlobalRef;
    t->DeleteLocalRef = sh_DeleteLocalRef;
    t->IsSameObject = sh_IsSameObject;
    t->NewLocalRef = sh_NewLocalRef;
    t->AllocObject = sh_AllocObject;
    t->NewObject = sh_NewObject;
    t->NewObjectV = sh_NewObjectV;
    t->NewObjectA = sh_NewObjectA;
    t->GetMethodID = sh_GetMethodID;
    t->GetStaticMethodID = sh_GetStaticMethodID;
    t->GetFieldID = sh_GetFieldID;
    t->GetStaticFieldID = sh_GetStaticFieldID;
    t->RegisterNatives = sh_RegisterNatives;
    t->UnregisterNatives = sh_UnregisterNatives;
    t->GetJavaVM = sh_GetJavaVM;
    t->NewStringUTF = sh_NewStringUTF;
    t->GetStringUTFLength = sh_GetStringUTFLength;
    t->GetStringUTFChars = sh_GetStringUTFChars;
    t->ReleaseStringUTFChars = sh_ReleaseStringUTFChars;
    t->GetStringLength = sh_GetStringLength;
    t->GetStringChars = sh_GetStringChars;
    t->ReleaseStringChars = sh_ReleaseStringChars;
    t->NewString = sh_NewString;
    t->GetStringRegion = sh_GetStringRegion;
    t->GetStringUTFRegion = sh_GetStringUTFRegion;
    t->CallStaticVoidMethod = sh_CallStaticVoidMethod;
    t->CallStaticIntMethod = sh_CallStaticIntMethod;
    t->CallStaticBooleanMethod = sh_CallStaticBooleanMethod;
    t->CallStaticLongMethod = sh_CallStaticLongMethod;
    t->CallStaticFloatMethod = sh_CallStaticFloatMethod;
    t->CallStaticDoubleMethod = sh_CallStaticDoubleMethod;
    t->CallStaticObjectMethod = sh_CallStaticObjectMethod;
    t->CallVoidMethod = sh_CallVoidMethod;
    t->CallIntMethod = sh_CallIntMethod;
    t->CallBooleanMethod = sh_CallBooleanMethod;
    t->CallLongMethod = sh_CallLongMethod;
    t->CallFloatMethod = sh_CallFloatMethod;
    t->CallDoubleMethod = sh_CallDoubleMethod;
    t->CallObjectMethod = sh_CallObjectMethod;
    t->GetBooleanField = sh_GetBooleanField;
    t->GetIntField = sh_GetIntField;
    t->GetLongField = sh_GetLongField;
    t->GetFloatField = sh_GetFloatField;
    t->GetObjectField = sh_GetObjectField;
    t->SetBooleanField = sh_SetBooleanField;
    t->SetIntField = sh_SetIntField;
    t->SetLongField = sh_SetLongField;
    t->SetFloatField = sh_SetFloatField;
    t->SetObjectField = sh_SetObjectField;
    t->GetArrayLength = sh_GetArrayLength;
    t->NewBooleanArray = sh_NewBooleanArray;
    t->NewByteArray = sh_NewByteArray;
    t->NewIntArray = sh_NewIntArray;
    t->NewObjectArray = sh_NewObjectArray;
    t->GetBooleanArrayElements = sh_GetBooleanArrayElements;
    t->GetByteArrayElements = sh_GetByteArrayElements;
    t->GetIntArrayElements = sh_GetIntArrayElements;
    t->ReleaseBooleanArrayElements = sh_ReleaseBooleanArrayElements;
    t->ReleaseByteArrayElements = sh_ReleaseByteArrayElements;
    t->ReleaseIntArrayElements = sh_ReleaseIntArrayElements;
    t->MonitorEnter = sh_monitorEnter;
    t->MonitorExit = sh_monitorExit;
}

static void install_invoke_table(void) {
    struct JNIInvokeInterface* t = (struct JNIInvokeInterface*)&g_invoke;
    t->DestroyJavaVM = vm_DestroyJavaVM;
    t->AttachCurrentThread = vm_AttachCurrentThread;
    t->DetachCurrentThread = vm_DetachCurrentThread;
    t->GetEnv = vm_GetEnv;
    t->AttachCurrentThreadAsDaemon = vm_AttachCurrentThreadAsDaemon;
}

void nexus_jni_install(void) {
    if (g_jni_installed) return;
    install_native_table();
    install_invoke_table();
    g_jni_installed = 1;
}

JavaVM* nexus_jni_vm(void) {
    nexus_jni_install();
    return &g_vm;
}

JNIEnv* nexus_jni_env(void) {
    nexus_jni_install();
    return &g_env;
}

jint nexus_run_jni_onload(void* fn, int log_calls) {
    if (!fn) return -1;
    nexus_jni_install();
    g_log_jni = log_calls;
    LOG("calling JNI_OnLoad(vm=%p, reserved=NULL)", (void*)&g_vm);
    fflush(stderr);
    jint (*onload)(JavaVM*, void*) = (jint (*)(JavaVM*, void*))fn;
    jint version = onload(&g_vm, NULL);
    LOG("JNI_OnLoad returned 0x%x (%d)", (unsigned)version, (int)version);
    fflush(stderr);
    return version;
}
