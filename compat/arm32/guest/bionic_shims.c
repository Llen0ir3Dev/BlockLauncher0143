/*
 * Guest-side Android platform shims for the genuine Minecraft PE 0.14.3
 * ARM32 library.
 *
 * These are not emulations of the game: they implement the small Android NDK
 * surface that libminecraftpe.so imports (liblog, libandroid, EGL window
 * metadata, looper, input queue, asset manager, configuration).  Graphics and
 * audio calls are forwarded by separate shim files once the session protocol
 * is available; this file is the platform foundation.
 *
 * Runs inside the static ARM32 guest under qemu-arm.
 */
#include "bionic_shims.h"
#include "loader_symbols.h"

#include <android/asset_manager.h>
#include <android/configuration.h>
#include <android/input.h>
#include <android/looper.h>
#include <android/log.h>
#include <android/native_activity.h>
#include <android/native_window.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/* ------------------------------------------------------------------------- */
/* logging                                                                   */
/* ------------------------------------------------------------------------- */

static void nexus_android_log(int prio, const char* tag, const char* fmt, va_list ap) {
    char text[2048];
    vsnprintf(text, sizeof(text), fmt, ap);
    fprintf(stderr, "[android:%d] %s: %s\n", prio, tag ? tag : "-", text);
    fflush(stderr);
}

int __android_log_print(int prio, const char* tag, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    nexus_android_log(prio, tag, fmt, ap);
    va_end(ap);
    return 1;
}

int __android_log_vprint(int prio, const char* tag, const char* fmt, va_list ap) {
    nexus_android_log(prio, tag, fmt, ap);
    return 1;
}

int __android_log_write(int prio, const char* tag, const char* text) {
    fprintf(stderr, "[android:%d] %s: %s\n", prio, tag ? tag : "-", text ? text : "");
    fflush(stderr);
    return 1;
}

void __android_log_assert(const char* cond, const char* tag, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    nexus_android_log(ANDROID_LOG_FATAL, tag, fmt, ap);
    va_end(ap);
    if (cond) {
        fprintf(stderr, "[android] assertion failed: %s\n", cond);
    }
    abort();
}

/* ------------------------------------------------------------------------- */
/* ctype compatibility                                                       */
/* ------------------------------------------------------------------------- */

/*
 * Old MCPE builds import bionic's private tolower table.  Its ABI is the
 * NetBSD/BSD convention: index 0 is unused, character c is at [c + 1].
 */
unsigned char _tolower_tab_[257];

static void init_tolower_table(void) {
    _tolower_tab_[0] = 0;
    for (int c = 0; c < 256; ++c) {
        _tolower_tab_[c + 1] = (unsigned char)((c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c);
    }
}

sighandler_t bsd_signal(int sig, sighandler_t handler) {
    return signal(sig, handler);
}

/* ------------------------------------------------------------------------- */
/* ANativeWindow                                                             */
/* ------------------------------------------------------------------------- */

struct ANativeWindow {
    int32_t width;
    int32_t height;
    int32_t format;
};

int32_t ANativeWindow_setBuffersGeometry(ANativeWindow* window, int32_t width,
                                         int32_t height, int32_t format) {
    if (!window) return -EINVAL;
    window->width = width;
    window->height = height;
    window->format = format;
    return 0;
}

/* ------------------------------------------------------------------------- */
/* AConfiguration                                                           */
/* ------------------------------------------------------------------------- */

struct AConfiguration {
    char language[8];
    char country[8];
};

AConfiguration* AConfiguration_new(void) {
    AConfiguration* config = (AConfiguration*)calloc(1, sizeof(AConfiguration));
    if (!config) return NULL;
    snprintf(config->language, sizeof(config->language), "en");
    snprintf(config->country, sizeof(config->country), "US");
    return config;
}

void AConfiguration_delete(AConfiguration* config) {
    free(config);
}

void AConfiguration_fromAssetManager(AConfiguration* out, AAssetManager* am) {
    (void)am;
    if (!out) return;
    snprintf(out->language, sizeof(out->language), "en");
    snprintf(out->country, sizeof(out->country), "US");
}

void AConfiguration_getLanguage(AConfiguration* config, char* outLanguage) {
    if (!outLanguage) return;
    snprintf(outLanguage, 3, "%s", config ? config->language : "en");
}

void AConfiguration_getCountry(AConfiguration* config, char* outCountry) {
    if (!outCountry) return;
    snprintf(outCountry, 3, "%s", config ? config->country : "US");
}

/* ------------------------------------------------------------------------- */
/* AAssetManager                                                             */
/* ------------------------------------------------------------------------- */

/*
 * The real ZIP-backed asset manager is implemented by the session layer.  For
 * now every open is logged and fails explicitly; the engine must never believe
 * it received a valid asset that does not exist.
 */
AAsset* AAssetManager_open(AAssetManager* mgr, const char* filename, int mode) {
    (void)mgr;
    fprintf(stderr, "[android] AAssetManager_open(%s, mode=%d): asset layer not ready\n",
            filename ? filename : "(null)", mode);
    fflush(stderr);
    return NULL;
}

off_t AAsset_getLength(AAsset* asset) {
    (void)asset;
    return 0;
}

const void* AAsset_getBuffer(AAsset* asset) {
    (void)asset;
    return NULL;
}

void AAsset_close(AAsset* asset) {
    (void)asset;
}

/* ------------------------------------------------------------------------- */
/* ALooper                                                                   */
/* ------------------------------------------------------------------------- */

#define NEXUS_MAX_LOOPER_FDS 64

struct ALooper {
    pthread_mutex_t lock;
    int count;
    struct {
        int fd;
        int ident;
        int events;
        ALooper_callbackFunc callback;
        void* data;
    } sources[NEXUS_MAX_LOOPER_FDS];
    int wake_pipe[2];
};

static struct ALooper g_looper;
static pthread_once_t g_looper_once = PTHREAD_ONCE_INIT;

static void looper_init_once(void) {
    memset(&g_looper, 0, sizeof(g_looper));
    pthread_mutex_init(&g_looper.lock, NULL);
    if (pipe(g_looper.wake_pipe) == 0) {
        fcntl(g_looper.wake_pipe[0], F_SETFL, O_NONBLOCK);
        fcntl(g_looper.wake_pipe[1], F_SETFL, O_NONBLOCK);
    } else {
        g_looper.wake_pipe[0] = g_looper.wake_pipe[1] = -1;
    }
}

ALooper* ALooper_prepare(int opts) {
    (void)opts;
    pthread_once(&g_looper_once, looper_init_once);
    return &g_looper;
}

static int poll_events_from_alooper(int events) {
    int out = 0;
    if (events & ALOOPER_EVENT_INPUT) out |= POLLIN;
    if (events & ALOOPER_EVENT_OUTPUT) out |= POLLOUT;
    if (events & ALOOPER_EVENT_ERROR) out |= POLLERR;
    if (events & ALOOPER_EVENT_HANGUP) out |= POLLHUP;
    return out ? out : POLLIN;
}

int ALooper_addFd(ALooper* looper, int fd, int ident, int events,
                  ALooper_callbackFunc callback, void* data) {
    if (!looper) looper = ALooper_prepare(0);
    if (fd < 0 || looper->count >= NEXUS_MAX_LOOPER_FDS) return -1;
    pthread_mutex_lock(&looper->lock);
    looper->sources[looper->count].fd = fd;
    looper->sources[looper->count].ident = ident;
    looper->sources[looper->count].events = events;
    looper->sources[looper->count].callback = callback;
    looper->sources[looper->count].data = data;
    looper->count++;
    pthread_mutex_unlock(&looper->lock);
    return 1;
}

int ALooper_removeFd(ALooper* looper, int fd) {
    if (!looper) return -1;
    int removed = -1;
    pthread_mutex_lock(&looper->lock);
    for (int i = 0; i < looper->count; ++i) {
        if (looper->sources[i].fd == fd) {
            looper->sources[i] = looper->sources[looper->count - 1];
            looper->count--;
            removed = 1;
            break;
        }
    }
    pthread_mutex_unlock(&looper->lock);
    return removed;
}

/*
 * pollAll is the loop used by Android's NativeActivity glue.  The callback
 * form (-2) is supported because the game's lifecycle thread uses it.
 */
int ALooper_pollAll(int timeoutMillis, int* outFd, int* outEvents, void** outData) {
    ALooper* looper = ALooper_prepare(0);

    struct pollfd *pfds = NULL;
    int *idents = NULL;
    ALooper_callbackFunc *callbacks = NULL;
    void **datas = NULL;
    int n = 0;

    pthread_mutex_lock(&looper->lock);
    n = looper->count;
    pfds = (struct pollfd*)calloc((size_t)n + 1, sizeof(struct pollfd));
    idents = (int*)calloc((size_t)n + 1, sizeof(int));
    callbacks = (ALooper_callbackFunc*)calloc((size_t)n + 1, sizeof(ALooper_callbackFunc));
    datas = (void**)calloc((size_t)n + 1, sizeof(void*));
    if (!pfds || !idents || !callbacks || !datas) {
        pthread_mutex_unlock(&looper->lock);
        free(pfds); free(idents); free(callbacks); free(datas);
        return ALOOPER_POLL_ERROR;
    }
    for (int i = 0; i < n; ++i) {
        pfds[i].fd = looper->sources[i].fd;
        pfds[i].events = (short)poll_events_from_alooper(looper->sources[i].events);
        idents[i] = looper->sources[i].ident;
        callbacks[i] = looper->sources[i].callback;
        datas[i] = looper->sources[i].data;
    }
    pthread_mutex_unlock(&looper->lock);

    int wake_index = n;
    if (looper->wake_pipe[0] >= 0) {
        pfds[wake_index].fd = looper->wake_pipe[0];
        pfds[wake_index].events = POLLIN;
    }

    int rc = poll(pfds, (nfds_t)(n + 1), timeoutMillis);
    if (rc < 0) {
        int err = errno;
        free(pfds); free(idents); free(callbacks); free(datas);
        return (err == EINTR) ? ALOOPER_POLL_TIMEOUT : ALOOPER_POLL_ERROR;
    }
    if (rc == 0) {
        free(pfds); free(idents); free(callbacks); free(datas);
        return ALOOPER_POLL_TIMEOUT;
    }

    if (looper->wake_pipe[0] >= 0 && (pfds[wake_index].revents & POLLIN)) {
        char drain[64];
        while (read(looper->wake_pipe[0], drain, sizeof(drain)) > 0) {}
        free(pfds); free(idents); free(callbacks); free(datas);
        if (outFd) *outFd = looper->wake_pipe[0];
        if (outEvents) *outEvents = 0;
        if (outData) *outData = NULL;
        return ALOOPER_POLL_WAKE;
    }

    for (int i = 0; i < n; ++i) {
        if (pfds[i].revents) {
            int ident = idents[i];
            int events = 0;
            if (pfds[i].revents & POLLIN) events |= ALOOPER_EVENT_INPUT;
            if (pfds[i].revents & POLLOUT) events |= ALOOPER_EVENT_OUTPUT;
            if (pfds[i].revents & POLLERR) events |= ALOOPER_EVENT_ERROR;
            if (pfds[i].revents & POLLHUP) events |= ALOOPER_EVENT_HANGUP;
            if (callbacks[i]) {
                int keep = callbacks[i](pfds[i].fd, events, datas[i]);
                if (keep == 0) ALooper_removeFd(looper, pfds[i].fd);
                free(pfds); free(idents); free(callbacks); free(datas);
                return ALOOPER_POLL_CALLBACK;
            }
            if (outFd) *outFd = pfds[i].fd;
            if (outEvents) *outEvents = events;
            if (outData) *outData = datas[i];
            free(pfds); free(idents); free(callbacks); free(datas);
            return ident;
        }
    }

    free(pfds); free(idents); free(callbacks); free(datas);
    return ALOOPER_POLL_TIMEOUT;
}

/* ------------------------------------------------------------------------- */
/* AInputQueue / input events                                                */
/* ------------------------------------------------------------------------- */

#define NEXUS_MAX_QUEUED_EVENTS 64

struct AInputEvent {
    int32_t type;
    int32_t device_id;
    int32_t source;
    union {
        struct {
            int32_t action;
            int32_t key_code;
            int32_t meta_state;
            int32_t repeat_count;
        } key;
        struct {
            int32_t action;
            size_t pointer_count;
            struct {
                int32_t id;
                float x, y;
                float raw_x, raw_y;
            } pointers[8];
            float axis_value;
        } motion;
    } u;
};

struct AInputQueue {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    AInputEvent* events[NEXUS_MAX_QUEUED_EVENTS];
    int head;
    int count;
    int attached;
    int looper_ident;
};

static struct AInputQueue g_input_queue;
static pthread_once_t g_input_once = PTHREAD_ONCE_INIT;

static void input_init_once(void) {
    memset(&g_input_queue, 0, sizeof(g_input_queue));
    pthread_mutex_init(&g_input_queue.lock, NULL);
    pthread_cond_init(&g_input_queue.cond, NULL);
}

static struct AInputQueue* input_queue_get(void) {
    pthread_once(&g_input_once, input_init_once);
    return &g_input_queue;
}

void AInputQueue_attachLooper(AInputQueue* queue, ALooper* looper, int ident,
                              ALooper_callbackFunc callback, void* data) {
    (void)looper;
    (void)callback;
    (void)data;
    if (!queue) queue = input_queue_get();
    pthread_mutex_lock(&queue->lock);
    queue->attached = 1;
    queue->looper_ident = ident;
    pthread_mutex_unlock(&queue->lock);
}

void AInputQueue_detachLooper(AInputQueue* queue) {
    if (!queue) queue = input_queue_get();
    pthread_mutex_lock(&queue->lock);
    queue->attached = 0;
    pthread_mutex_unlock(&queue->lock);
}

int32_t AInputQueue_getEvent(AInputQueue* queue, AInputEvent** outEvent) {
    if (!queue) queue = input_queue_get();
    pthread_mutex_lock(&queue->lock);
    while (queue->count == 0) {
        pthread_cond_wait(&queue->cond, &queue->lock);
    }
    AInputEvent* event = queue->events[queue->head];
    queue->head = (queue->head + 1) % NEXUS_MAX_QUEUED_EVENTS;
    queue->count--;
    pthread_mutex_unlock(&queue->lock);
    if (outEvent) *outEvent = event;
    return 1;
}

int32_t AInputQueue_preDispatchEvent(AInputQueue* queue, AInputEvent* event) {
    (void)queue;
    (void)event;
    return 0;
}

void AInputQueue_finishEvent(AInputQueue* queue, AInputEvent* event, int handled) {
    (void)queue;
    (void)handled;
    free(event);
}

int32_t AInputEvent_getType(const AInputEvent* event) {
    return event ? event->type : 0;
}

int32_t AInputEvent_getDeviceId(const AInputEvent* event) {
    return event ? event->device_id : 0;
}

int32_t AInputEvent_getSource(const AInputEvent* event) {
    return event ? event->source : 0;
}

int32_t AKeyEvent_getAction(const AInputEvent* event) {
    return (event && event->type == AINPUT_EVENT_TYPE_KEY) ? event->u.key.action : 0;
}

int32_t AKeyEvent_getKeyCode(const AInputEvent* event) {
    return (event && event->type == AINPUT_EVENT_TYPE_KEY) ? event->u.key.key_code : 0;
}

int32_t AKeyEvent_getMetaState(const AInputEvent* event) {
    return (event && event->type == AINPUT_EVENT_TYPE_KEY) ? event->u.key.meta_state : 0;
}

int32_t AKeyEvent_getRepeatCount(const AInputEvent* event) {
    return (event && event->type == AINPUT_EVENT_TYPE_KEY) ? event->u.key.repeat_count : 0;
}

int32_t AMotionEvent_getAction(const AInputEvent* event) {
    return (event && event->type == AINPUT_EVENT_TYPE_MOTION) ? event->u.motion.action : 0;
}

size_t AMotionEvent_getPointerCount(const AInputEvent* event) {
    return (event && event->type == AINPUT_EVENT_TYPE_MOTION) ? event->u.motion.pointer_count : 0;
}

int32_t AMotionEvent_getPointerId(const AInputEvent* event, size_t pointer_index) {
    if (!event || event->type != AINPUT_EVENT_TYPE_MOTION ||
        pointer_index >= event->u.motion.pointer_count) return 0;
    return event->u.motion.pointers[pointer_index].id;
}

float AMotionEvent_getX(const AInputEvent* event, size_t pointer_index) {
    if (!event || event->type != AINPUT_EVENT_TYPE_MOTION ||
        pointer_index >= event->u.motion.pointer_count) return 0.0f;
    return event->u.motion.pointers[pointer_index].x;
}

float AMotionEvent_getY(const AInputEvent* event, size_t pointer_index) {
    if (!event || event->type != AINPUT_EVENT_TYPE_MOTION ||
        pointer_index >= event->u.motion.pointer_count) return 0.0f;
    return event->u.motion.pointers[pointer_index].y;
}

float AMotionEvent_getRawX(const AInputEvent* event, size_t pointer_index) {
    if (!event || event->type != AINPUT_EVENT_TYPE_MOTION ||
        pointer_index >= event->u.motion.pointer_count) return 0.0f;
    return event->u.motion.pointers[pointer_index].raw_x;
}

float AMotionEvent_getRawY(const AInputEvent* event, size_t pointer_index) {
    if (!event || event->type != AINPUT_EVENT_TYPE_MOTION ||
        pointer_index >= event->u.motion.pointer_count) return 0.0f;
    return event->u.motion.pointers[pointer_index].raw_y;
}

float AMotionEvent_getAxisValue(const AInputEvent* event, int32_t axis, size_t pointer_index) {
    (void)axis;
    (void)pointer_index;
    return (event && event->type == AINPUT_EVENT_TYPE_MOTION) ? event->u.motion.axis_value : 0.0f;
}

/* ------------------------------------------------------------------------- */
/* ANativeActivity                                                           */
/* ------------------------------------------------------------------------- */

static int g_activity_finished = 0;

void ANativeActivity_finish(ANativeActivity* activity) {
    (void)activity;
    g_activity_finished = 1;
}

int nexus_activity_should_finish(void) {
    return g_activity_finished;
}

/* ------------------------------------------------------------------------- */
/* registration                                                              */
/* ------------------------------------------------------------------------- */

#define SHIM(name) { #name, (void*)(uintptr_t)&name }

static const nexus_symbol_entry g_android_shims[] = {
    SHIM(__android_log_print),
    SHIM(__android_log_vprint),
    SHIM(__android_log_write),
    SHIM(__android_log_assert),
    SHIM(bsd_signal),
    SHIM(_tolower_tab_),
    SHIM(ANativeWindow_setBuffersGeometry),
    SHIM(AConfiguration_new),
    SHIM(AConfiguration_delete),
    SHIM(AConfiguration_fromAssetManager),
    SHIM(AConfiguration_getLanguage),
    SHIM(AConfiguration_getCountry),
    SHIM(AAssetManager_open),
    SHIM(AAsset_getLength),
    SHIM(AAsset_getBuffer),
    SHIM(AAsset_close),
    SHIM(ALooper_prepare),
    SHIM(ALooper_pollAll),
    SHIM(ALooper_addFd),
    SHIM(ALooper_removeFd),
    SHIM(AInputQueue_attachLooper),
    SHIM(AInputQueue_detachLooper),
    SHIM(AInputQueue_getEvent),
    SHIM(AInputQueue_preDispatchEvent),
    SHIM(AInputQueue_finishEvent),
    SHIM(AInputEvent_getType),
    SHIM(AInputEvent_getDeviceId),
    SHIM(AInputEvent_getSource),
    SHIM(AKeyEvent_getAction),
    SHIM(AKeyEvent_getKeyCode),
    SHIM(AKeyEvent_getMetaState),
    SHIM(AKeyEvent_getRepeatCount),
    SHIM(AMotionEvent_getAction),
    SHIM(AMotionEvent_getPointerCount),
    SHIM(AMotionEvent_getPointerId),
    SHIM(AMotionEvent_getX),
    SHIM(AMotionEvent_getY),
    SHIM(AMotionEvent_getRawX),
    SHIM(AMotionEvent_getRawY),
    SHIM(AMotionEvent_getAxisValue),
    SHIM(ANativeActivity_finish),
};

void nexus_register_android_shims(void) {
    init_tolower_table();
    nexus_loader_add_symbols(g_android_shims,
                             sizeof(g_android_shims) / sizeof(g_android_shims[0]));
}
