#pragma once

// Shared symbol-registration contract between the in-tree ELF loader and the
// guest shim modules.  The loader resolves imports against every registered
// table (shims first), then against the exports of the loaded modules.

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char* name;
    void* address;
} nexus_symbol_entry;

// Register an additional symbol table.  Tables are consulted in registration
// order and are not copied, so they must stay alive for the process lifetime.
void nexus_loader_add_symbols(const nexus_symbol_entry* entries, size_t count);

#ifdef __cplusplus
}
#endif
