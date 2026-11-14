#pragma once

// Minimal JNI type declarations for the host-side ARM32 compatibility layer.
//
// The compatibility library only manipulates opaque JNI pointers on the host
// (it never calls through a JNIEnv function table there).  When a real JDK or
// Android NDK header is available CMake defines NEXUS_HAVE_SYSTEM_JNI and the
// genuine <jni.h> is used instead.  This header exists so the host build never
// depends on a JDK being installed on the CI runner or a user machine.
//
// These declarations match the ABI-neutral subset from the Java Native
// Interface specification.  They are declarations only: no implementation is
// linked, and no proprietary code is involved.

#include <cstdint>

#ifndef NEXUS_HAVE_SYSTEM_JNI

typedef unsigned char jboolean;
typedef signed char jbyte;
typedef unsigned short jchar;
typedef short jshort;
typedef int jint;
typedef long long jlong;
typedef float jfloat;
typedef double jdouble;
typedef jint jsize;

typedef unsigned char jboolean_array_padding;

class _jobject {};
class _jclass : public _jobject {};
class _jstring : public _jobject {};
class _jarray : public _jobject {};
class _jobjectArray : public _jarray {};

typedef _jobject* jobject;
typedef _jclass* jclass;
typedef _jstring* jstring;
typedef _jarray* jarray;
typedef _jobjectArray* jobjectArray;

struct _jmethodID;
typedef _jmethodID* jmethodID;

struct _jfieldID;
typedef _jfieldID* jfieldID;

union jvalue {
    jboolean z;
    jbyte b;
    jchar c;
    jshort s;
    jint i;
    jlong j;
    jfloat f;
    jdouble d;
    jobject l;
};

struct JNINativeInterface;
struct JNIEnv {
    const struct JNINativeInterface* functions;
};

struct JavaVM;

// Eventual values from the JNI specification.  Only the ones the host-side
// code references are declared here.
enum {
    JNI_OK = 0,
    JNI_ERR = -1,
    JNI_EDETACHED = -2,
    JNI_EVERSION = -3,
    JNI_ENOMEM = -4,
    JNI_EEXIST = -5,
    JNI_EINVAL = -6
};

enum {
    JNI_VERSION_1_1 = 0x00010001,
    JNI_VERSION_1_2 = 0x00010002,
    JNI_VERSION_1_4 = 0x00010004,
    JNI_VERSION_1_6 = 0x00010006
};

// The invoke interface layout matches the JNI specification so the same
// member-call pattern works whether the real <jni.h> or this header is used.
struct JNIInvokeInterface {
    void* reserved0;
    void* reserved1;
    void* reserved2;
    jint (*DestroyJavaVM)(JavaVM* vm);
    jint (*AttachCurrentThread)(JavaVM* vm, void** penv, void* args);
    jint (*DetachCurrentThread)(JavaVM* vm);
    jint (*GetEnv)(JavaVM* vm, void** penv, jint version);
    jint (*AttachCurrentThreadAsDaemon)(JavaVM* vm, void** penv, void* args);
};

struct JavaVM {
    const JNIInvokeInterface* functions;
#ifdef __cplusplus
    jint DestroyJavaVM() { return functions->DestroyJavaVM(this); }
    jint AttachCurrentThread(void** penv, void* args) {
        return functions->AttachCurrentThread(this, penv, args);
    }
    jint DetachCurrentThread() { return functions->DetachCurrentThread(this); }
    jint GetEnv(void** penv, jint version) { return functions->GetEnv(this, penv, version); }
    jint AttachCurrentThreadAsDaemon(void** penv, void* args) {
        return functions->AttachCurrentThreadAsDaemon(this, penv, args);
    }
#endif
};

#endif // NEXUS_HAVE_SYSTEM_JNI
