#pragma once

/*
 * NEXUS-MCPE native mod ABI.
 *
 * This is a stable C interface.  Native mods are shared objects exporting
 * nexus_mod_descriptor(), nexus_mod_init() and nexus_mod_shutdown().  They are
 * loaded with dlopen()/dlsym() by the NEXUS mod host.
 *
 * The host exposes only explicit capabilities.  It never hands the mod a raw
 * pointer into the proprietary Minecraft PE runtime: the runtime is not linked
 * into the host in this repository.  Events and registrations flow through the
 * callbacks below so that ABI mismatches are contained and reported.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NEXUS_MOD_ABI_VERSION 1u
#define NEXUS_MOD_API_VERSION "1.0"

#if defined(_WIN32)
#define NEXUS_MOD_EXPORT __declspec(dllexport)
#else
#define NEXUS_MOD_EXPORT __attribute__((visibility("default")))
#endif

typedef enum NexusLogLevel {
    NEXUS_LOG_TRACE = 0,
    NEXUS_LOG_DEBUG = 1,
    NEXUS_LOG_INFO = 2,
    NEXUS_LOG_WARN = 3,
    NEXUS_LOG_ERROR = 4,
    NEXUS_LOG_FATAL = 5
} NexusLogLevel;

/* Event callback: payload is a UTF-8 JSON object.  The callback may return a
 * UTF-8 JSON object with modified fields; return NULL to leave the event
 * unchanged.  The returned string is copied by the host immediately. */
typedef const char* (*NexusEventCallback)(void* userdata, const char* event_name,
                                          const char* json_payload);

typedef struct NexusModHost {
    uint32_t abi_version;
    const char* nexus_version;
    void* userdata;

    void (*log)(void* userdata, int level, const char* message);

    /* Emit an event into the host event bus.  Returns 1 when not cancelled. */
    int (*emit)(void* userdata, const char* event_name, const char* json_payload);

    /* Subscribe to an event.  Returns a positive subscription id or 0. */
    uint64_t (*subscribe)(void* userdata, const char* event_name, int priority,
                          NexusEventCallback callback, void* callback_userdata);
    int (*unsubscribe)(void* userdata, uint64_t subscription_id);

    /* Resolve an exported symbol from a loaded library.  Returns NULL when the
     * symbol is not found.  Used for compatibility checks and optional
     * integration points, not for patching arbitrary process memory. */
    void* (*resolve_symbol)(void* userdata, const char* library, const char* symbol);

    /* Register a block.  Returns the assigned block id, or -1 on error. */
    int (*register_block)(void* userdata, const char* json_definition);
    int (*register_item)(void* userdata, const char* json_definition);
    int (*register_entity)(void* userdata, const char* json_definition);

    /* Cooperative hook registration.  Hooks run at documented NEXUS pipeline
     * points (worldgen, render, tick), never by modifying game machine code. */
    uint64_t (*register_hook)(void* userdata, const char* hook_name, int priority,
                              NexusEventCallback callback, void* callback_userdata);
} NexusModHost;

typedef struct NexusModDescriptor {
    uint32_t abi_version;
    const char* id;
    const char* name;
    const char* version;
    const char* author;
    const char* description;
    const char* api_version;
} NexusModDescriptor;

/* Mandatory exports. */
NEXUS_MOD_EXPORT const NexusModDescriptor* nexus_mod_descriptor(void);
NEXUS_MOD_EXPORT int nexus_mod_init(const NexusModHost* host);
NEXUS_MOD_EXPORT void nexus_mod_shutdown(void);

/* Optional export: called once per frame by runtimes that support ticking. */
typedef void (*NexusModTickFn)(double delta_seconds);
NEXUS_MOD_EXPORT void nexus_mod_tick(double delta_seconds);

#ifdef __cplusplus
}
#endif
