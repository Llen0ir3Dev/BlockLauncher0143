#pragma once

#include <cstdint>
#include <cstddef>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <pthread.h>
#include <dlfcn.h>
#if defined(NEXUS_HAVE_SYSTEM_JNI)
#include <jni.h>
#else
#include <nexus/compat/arm32/jni_min.h>
#endif

/// Android API level for 0.14.3 (API 17-22)
#define ANDROID_API_LEVEL 19

/// Android log priorities
enum android_LogPriority {
    ANDROID_LOG_UNKNOWN = 0,
    ANDROID_LOG_DEFAULT = 1,
    ANDROID_LOG_VERBOSE = 2,
    ANDROID_LOG_DEBUG = 3,
    ANDROID_LOG_INFO = 4,
    ANDROID_LOG_WARN = 5,
    ANDROID_LOG_ERROR = 6,
    ANDROID_LOG_FATAL = 7,
    ANDROID_LOG_SILENT = 8
};

/// Android asset manager (opaque)
struct AAssetManager;
struct AAsset;

/// Android native window
struct ANativeWindow;

/// Android configuration
struct AConfiguration;

/// Android input queue
struct AInputQueue;

/// Android input event
struct AInputEvent;

/// Android key event
struct AKeyEvent;

/// Android motion event
struct AMotionEvent;

/// Android looper
struct ALooper;

/// Android native activity
struct ANativeActivity;

/// Android native activity callbacks
struct ANativeActivityCallbacks {
    void (*onStart)(ANativeActivity* activity);
    void (*onResume)(ANativeActivity* activity);
    void* (*onSaveInstanceState)(ANativeActivity* activity, size_t* outSize);
    void (*onPause)(ANativeActivity* activity);
    void (*onStop)(ANativeActivity* activity);
    void (*onDestroy)(ANativeActivity* activity);
    void (*onWindowFocusChanged)(ANativeActivity* activity, int focused);
    void (*onNativeWindowCreated)(ANativeActivity* activity, ANativeWindow* window);
    void (*onNativeWindowDestroyed)(ANativeActivity* activity, ANativeWindow* window);
    void (*onInputQueueCreated)(ANativeActivity* activity, AInputQueue* queue);
    void (*onInputQueueDestroyed)(ANativeActivity* activity, AInputQueue* queue);
    void (*onConfigurationChanged)(ANativeActivity* activity);
    void (*onLowMemory)(ANativeActivity* activity);
};

/// Android native activity structure
struct ANativeActivity {
    void* callbacks;
    void* instance;
    const char* internalDataPath;
    const char* externalDataPath;
    int sdkVersion;
    AAssetManager* assetManager;
    int internalDataPathLength;
    int externalDataPathLength;
    void* environment;
    void* clazz;
};

/// Android asset manager functions
extern "C" {
AAssetManager* AAssetManager_fromJava(JNIEnv* env, jobject assetManager);
AAsset* AAssetManager_open(AAssetManager* mgr, const char* filename, int mode);
void AAsset_close(AAsset* asset);
off_t AAsset_getLength(AAsset* asset);
const void* AAsset_getBuffer(AAsset* asset);
int AAsset_read(AAsset* asset, void* buf, size_t count);
off_t AAsset_seek(AAsset* asset, off_t offset, int whence);
int AAsset_openFileDescriptor(AAsset* asset, off_t* outStart, off_t* outLength);

AConfiguration* AConfiguration_new();
void AConfiguration_delete(AConfiguration* config);
void AConfiguration_fromAssetManager(AConfiguration* out, AAssetManager* mgr);
int AConfiguration_getLanguage(AConfiguration* config, char* out, size_t outSize);
int AConfiguration_getCountry(AConfiguration* config, char* out, size_t outSize);

ANativeWindow* ANativeWindow_fromSurface(JNIEnv* env, jobject surface);
void ANativeWindow_release(ANativeWindow* window);
int ANativeWindow_setBuffersGeometry(ANativeWindow* window, int width, int height, int format);
int ANativeWindow_lock(ANativeWindow* window, void** outBits, void* inOutDirtyBounds);
int ANativeWindow_unlockAndPost(ANativeWindow* window);

ALooper* ALooper_prepare(int opts);
int ALooper_pollAll(int timeout, int* outFd, int* outEvents, void** outData);
int ALooper_addFd(ALooper* looper, int fd, int ident, int events,
                  int (*callback)(int fd, int events, void* data), void* data);

int AInputQueue_getEvent(AInputQueue* queue, AInputEvent** outEvent);
int AInputQueue_preDispatchEvent(AInputQueue* queue, AInputEvent* event);
void AInputQueue_finishEvent(AInputQueue* queue, AInputEvent* event, int handled);
void AInputQueue_attachLooper(AInputQueue* queue, ALooper* looper, int ident,
                              int (*callback)(int fd, int events, void* data), void* data);
void AInputQueue_detachLooper(AInputQueue* queue);

int AInputEvent_getType(const AInputEvent* event);
int32_t AInputEvent_getSource(const AInputEvent* event);
int32_t AInputEvent_getDeviceId(const AInputEvent* event);

int32_t AKeyEvent_getAction(const AInputEvent* event);
int32_t AKeyEvent_getKeyCode(const AInputEvent* event);
int32_t AKeyEvent_getMetaState(const AInputEvent* event);
int32_t AKeyEvent_getRepeatCount(const AInputEvent* event);

int32_t AMotionEvent_getAction(const AInputEvent* event);
int32_t AMotionEvent_getPointerCount(const AInputEvent* event);
int32_t AMotionEvent_getPointerId(const AInputEvent* event, int32_t pointerIndex);
float AMotionEvent_getX(const AInputEvent* event, int32_t pointerIndex);
float AMotionEvent_getY(const AInputEvent* event, int32_t pointerIndex);
float AMotionEvent_getRawX(const AInputEvent* event, int32_t pointerIndex);
float AMotionEvent_getRawY(const AInputEvent* event, int32_t pointerIndex);
float AMotionEvent_getAxisValue(const AInputEvent* event, int32_t axis, int32_t pointerIndex);

void ANativeActivity_finish(ANativeActivity* activity);

int __android_log_write(int prio, const char* tag, const char* fmt, ...);
int __android_log_print(int prio, const char* tag, const char* fmt, ...);
int __android_log_vprint(int prio, const char* tag, const char* fmt, va_list ap);
} // extern "C"

/// Bionic libc stubs (minimal subset needed by libminecraftpe.so)
namespace bionic {

// String functions
extern "C" {
void* memcpy(void* dest, const void* src, size_t n);
void* memmove(void* dest, const void* src, size_t n);
void* memset(void* s, int c, size_t n);
int memcmp(const void* s1, const void* s2, size_t n);
size_t strlen(const char* s);
char* strcpy(char* dest, const char* src);
char* strncpy(char* dest, const char* src, size_t n);
char* strcat(char* dest, const char* src);
char* strncat(char* dest, const char* src, size_t n);
int strcmp(const char* s1, const char* s2);
int strncmp(const char* s1, const char* s2, size_t n);
char* strchr(const char* s, int c);
char* strrchr(const char* s, int c);
size_t strspn(const char* s, const char* accept);
size_t strcspn(const char* s, const char* reject);
char* strstr(const char* haystack, const char* needle);
}

// Memory allocation
extern "C" {
void* malloc(size_t size);
void* calloc(size_t nmemb, size_t size);
void* realloc(void* ptr, size_t size);
void free(void* ptr);
void* memalign(size_t alignment, size_t size);
int posix_memalign(void** memptr, size_t alignment, size_t size);
}

// File I/O
extern "C" {
int open(const char* pathname, int flags, ...);
int close(int fd);
ssize_t read(int fd, void* buf, size_t count);
ssize_t write(int fd, const void* buf, size_t count);
off_t lseek(int fd, off_t offset, int whence);
int stat(const char* pathname, struct stat* statbuf);
int fstat(int fd, struct stat* statbuf);
int mkdir(const char* pathname, mode_t mode);
int unlink(const char* pathname);
int rename(const char* oldpath, const char* newpath);
DIR* opendir(const char* name);
struct dirent* readdir(DIR* dirp);
int closedir(DIR* dirp);
}

// Threading
extern "C" {
int pthread_create(pthread_t* thread, const pthread_attr_t* attr,
                   void* (*start_routine)(void*), void* arg);
int pthread_join(pthread_t thread, void** retval);
int pthread_detach(pthread_t thread);
pthread_t pthread_self(void);
int pthread_mutex_init(pthread_mutex_t* mutex, const pthread_mutexattr_t* attr);
int pthread_mutex_destroy(pthread_mutex_t* mutex);
int pthread_mutex_lock(pthread_mutex_t* mutex);
int pthread_mutex_unlock(pthread_mutex_t* mutex);
int pthread_cond_init(pthread_cond_t* cond, const pthread_condattr_t* attr);
int pthread_cond_destroy(pthread_cond_t* cond);
int pthread_cond_signal(pthread_cond_t* cond);
int pthread_cond_broadcast(pthread_cond_t* cond);
int pthread_cond_wait(pthread_cond_t* cond, pthread_mutex_t* mutex);
int pthread_cond_timedwait(pthread_cond_t* cond, pthread_mutex_t* mutex,
                           const struct timespec* abstime);
}

// Time
extern "C" {
int clock_gettime(clockid_t clk_id, struct timespec* tp);
int clock_getres(clockid_t clk_id, struct timespec* tp);
int nanosleep(const struct timespec* req, struct timespec* rem);
unsigned int sleep(unsigned int seconds);
int usleep(useconds_t usec);
}

// Signals
extern "C" {
typedef void (*sighandler_t)(int);
sighandler_t signal(int signum, sighandler_t handler);
int sigaction(int signum, const struct sigaction* act, struct sigaction* oldact);
int sigemptyset(sigset_t* set);
int sigfillset(sigset_t* set);
int sigaddset(sigset_t* set, int signum);
int sigprocmask(int how, const sigset_t* set, sigset_t* oldset);
}

// Dynamic loading
extern "C" {
void* dlopen(const char* filename, int flag);
void* dlsym(void* handle, const char* symbol);
int dlclose(void* handle);
const char* dlerror(void);
}

// Math
extern "C" {
double sin(double x);
double cos(double x);
double sqrt(double x);
double pow(double x, double y);
double log(double x);
double exp(double x);
float sinf(float x);
float cosf(float x);
float sqrtf(float x);
}
} // namespace bionic

/// Android system properties
namespace android {
struct prop_info;
extern "C" {
int __system_property_get(const char* key, char* value);
int __system_property_find(const char* key);
const prop_info* __system_property_find_n(const char* name, unsigned len);
void __system_property_read_callback(const prop_info* pi,
                                     void (*callback)(void* cookie, const char* name,
                                                      const char* value, uint32_t serial),
                                     void* cookie);
}
} // namespace android

/// OpenSL ES minimal stubs
namespace opensl {
extern "C" {
typedef struct SLObjectItf_* SLObjectItf;
typedef struct SLEngineItf_* SLEngineItf;
typedef struct SLPlayItf_* SLPlayItf;
typedef struct SLVolumeItf_* SLVolumeItf;
typedef struct SLBufferQueueItf_* SLBufferQueueItf;

typedef uint32_t SLresult;
typedef uint32_t SLuint32;
typedef int32_t SLint32;
typedef uint16_t SLuint16;
typedef int16_t SLint16;
typedef uint32_t SLboolean;

#define SL_RESULT_SUCCESS 0
#define SL_RESULT_PRECONDITIONS_VIOLATED 0x00000001
#define SL_RESULT_PARAMETER_INVALID 0x00000002
#define SL_RESULT_MEMORY_FAILURE 0x00000003
#define SL_RESULT_RESOURCE_ERROR 0x00000004
#define SL_RESULT_RESOURCE_LOST 0x00000005
#define SL_RESULT_IO_ERROR 0x00000006
#define SL_RESULT_BUFFER_INSUFFICIENT 0x00000007
#define SL_RESULT_CONTENT_CORRUPTED 0x00000008
#define SL_RESULT_CONTENT_UNSUPPORTED 0x00000009
#define SL_RESULT_CONTENT_NOT_FOUND 0x0000000A
#define SL_RESULT_PERMISSION_DENIED 0x0000000B
#define SL_RESULT_FEATURE_UNSUPPORTED 0x0000000C
#define SL_RESULT_INTERNAL_ERROR 0x0000000D
#define SL_RESULT_UNKNOWN_ERROR 0x0000000E
#define SL_RESULT_OPERATION_ABORTED 0x0000000F
#define SL_RESULT_CONTROL_LOST 0x00000010

SLresult slCreateEngine(SLObjectItf* engine, SLuint32 numOptions,
                        const void* options, SLuint32 numInterfaces,
                        const void* interfaceIds, const SLboolean* interfaceRequired);
}
} // namespace opensl

/// FMOD minimal stubs (for libminecraftpe.so)
namespace fmod {
extern "C" {
typedef struct FMOD_SYSTEM FMOD_SYSTEM;
typedef struct FMOD_SOUND FMOD_SOUND;
typedef struct FMOD_CHANNEL FMOD_CHANNEL;
typedef struct FMOD_CHANNELGROUP FMOD_CHANNELGROUP;
typedef struct FMOD_DSP FMOD_DSP;
typedef struct FMOD_DSPCONNECTION FMOD_DSPCONNECTION;
typedef struct FMOD_GEOMETRY FMOD_GEOMETRY;
typedef struct FMOD_REVERB FMOD_REVERB;

typedef int FMOD_RESULT;
typedef unsigned int FMOD_MODE;
typedef unsigned int FMOD_TIMEUNIT;

#define FMOD_OK 0
#define FMOD_ERR_MEMORY 10
#define FMOD_ERR_FILE_NOTFOUND 11
#define FMOD_ERR_FORMAT 12
#define FMOD_ERR_INITIALIZATION 13
#define FMOD_ERR_INVALID_PARAM 14
#define FMOD_ERR_OUTPUT_CREATE 15
#define FMOD_ERR_OUTPUT_DRIVERCALL 16
#define FMOD_ERR_OUTPUT_FORMAT 17
#define FMOD_ERR_OUTPUT_INIT 17

FMOD_RESULT FMOD_System_Create(FMOD_SYSTEM** system);
FMOD_RESULT FMOD_System_Release(FMOD_SYSTEM* system);
FMOD_RESULT FMOD_System_Init(FMOD_SYSTEM* system, int maxchannels, FMOD_MODE flags, void* extradriverdata);
FMOD_RESULT FMOD_System_Close(FMOD_SYSTEM* system);
FMOD_RESULT FMOD_System_CreateSound(FMOD_SYSTEM* system, const char* name_or_data, FMOD_MODE mode, void* exinfo, FMOD_SOUND** sound);
FMOD_RESULT FMOD_Sound_Release(FMOD_SOUND* sound);
FMOD_RESULT FMOD_System_PlaySound(FMOD_SYSTEM* system, FMOD_SOUND* sound, FMOD_CHANNELGROUP* channelgroup, bool paused, FMOD_CHANNEL** channel);
FMOD_RESULT FMOD_Channel_SetVolume(FMOD_CHANNEL* channel, float volume);
FMOD_RESULT FMOD_Channel_SetPitch(FMOD_CHANNEL* channel, float pitch);
FMOD_RESULT FMOD_Channel_SetMute(FMOD_CHANNEL* channel, bool mute);
FMOD_RESULT FMOD_Channel_SetPaused(FMOD_CHANNEL* channel, bool paused);
FMOD_RESULT FMOD_Channel_IsPlaying(FMOD_CHANNEL* channel, bool* isplaying);
}
} // namespace fmod