// NEXUS-MCPE in-tree ARM32 ELF loader
//
// This is the guest-side loader that will eventually load the genuine
// Android ARM32 libminecraftpe.so without any Android linker.  It is plain C
// so the same source can be:
//   * compiled for the ARM32 guest with the Android NDK (static bionic), and
//   * compiled for the host as an inspection tool.
//
// Two modes:
//   --inspect FILE   parse the ELF and report architecture, dependencies,
//                    relocations and undefined symbols.  No mapping, no
//                    execution.  Safe on any host.
//   --load FILE      reserve and map PT_LOAD segments, apply relocations in
//                    the current process and report what is still unresolved.
//                    Meaningful only for a 32-bit ARM process (the guest);
//                    on other hosts it reports why it refused.
//
// The loader never fakes success.  If a relocation or symbol cannot be
// resolved it is counted and named.  The game is never executed here.

#define _GNU_SOURCE
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "loader_symbols.h"

#if defined(__arm__)
#include "bionic_shims.h"
#include "gfx_client.h"
#include "jni_env.h"
#endif

#ifndef R_ARM_COPY
#define R_ARM_COPY 20
#endif
#ifndef R_ARM_GLOB_DAT
#define R_ARM_GLOB_DAT 21
#endif
#ifndef R_ARM_JUMP_SLOT
#define R_ARM_JUMP_SLOT 22
#endif
#ifndef R_ARM_RELATIVE
#define R_ARM_RELATIVE 23
#endif

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

#define NEXUS_MAX_REPORTED_SYMBOLS 256
#define NEXUS_PAGE 4096u
#define NEXUS_MAX_SYMBOL_TABLES 8

static const nexus_symbol_entry* g_symbol_tables[NEXUS_MAX_SYMBOL_TABLES];
static size_t g_symbol_counts[NEXUS_MAX_SYMBOL_TABLES];
static size_t g_symbol_table_count = 0;

// The guest shim layer registers its exported ABI symbols here.  The loader
// stays independent of every specific shim.
void nexus_loader_add_symbols(const nexus_symbol_entry* entries, size_t count) {
    if (!entries || count == 0) return;
    if (g_symbol_table_count >= NEXUS_MAX_SYMBOL_TABLES) return;
    g_symbol_tables[g_symbol_table_count] = entries;
    g_symbol_counts[g_symbol_table_count] = count;
    g_symbol_table_count++;
}

typedef struct {
    uint32_t offset;
    uint32_t info;
    int32_t addend;
    int has_addend;
} nexus_reloc;

typedef struct {
    int64_t total;
    int64_t applied;
    int64_t unresolved;
    int64_t by_type[64];
    int64_t relative;
    char reported[NEXUS_MAX_REPORTED_SYMBOLS][160];
    size_t reported_count;
} nexus_reloc_stats;

#if defined(__arm__)
static void record_unresolved(nexus_reloc_stats* stats, const char* name) {
    stats->unresolved++;
    if (stats->reported_count >= NEXUS_MAX_REPORTED_SYMBOLS) return;
    for (size_t i = 0; i < stats->reported_count; ++i) {
        if (strcmp(stats->reported[i], name) == 0) return;
    }
    snprintf(stats->reported[stats->reported_count], sizeof(stats->reported[0]), "%s",
             name ? name : "(no name)");
    stats->reported_count++;
}
#endif

static const char* arm_reloc_name(uint32_t type) {
    switch (type) {
        case R_ARM_NONE: return "R_ARM_NONE";
        case R_ARM_PC24: return "R_ARM_PC24";
        case R_ARM_ABS32: return "R_ARM_ABS32";
        case R_ARM_REL32: return "R_ARM_REL32";
        case R_ARM_COPY: return "R_ARM_COPY";
        case R_ARM_GLOB_DAT: return "R_ARM_GLOB_DAT";
        case R_ARM_JUMP_SLOT: return "R_ARM_JUMP_SLOT";
        case R_ARM_RELATIVE: return "R_ARM_RELATIVE";
#ifdef R_ARM_TLS_DTPMOD32
        case R_ARM_TLS_DTPMOD32: return "R_ARM_TLS_DTPMOD32";
#endif
#ifdef R_ARM_TLS_DTPOFF32
        case R_ARM_TLS_DTPOFF32: return "R_ARM_TLS_DTPOFF32";
#endif
#ifdef R_ARM_TLS_TPOFF32
        case R_ARM_TLS_TPOFF32: return "R_ARM_TLS_TPOFF32";
#endif
        default: return "R_ARM_<other>";
    }
}

static uint8_t* read_file(const char* path, size_t* out_size) {
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return NULL;
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size <= 0) {
        close(fd);
        return NULL;
    }
    uint8_t* data = (uint8_t*)malloc((size_t)st.st_size);
    if (!data) {
        close(fd);
        return NULL;
    }
    size_t done = 0;
    while (done < (size_t)st.st_size) {
        const ssize_t n = read(fd, data + done, (size_t)st.st_size - done);
        if (n <= 0) {
            free(data);
            close(fd);
            return NULL;
        }
        done += (size_t)n;
    }
    close(fd);
    *out_size = (size_t)st.st_size;
    return data;
}

static int validate_elf32_arm(const uint8_t* data, size_t size) {
    if (size < sizeof(Elf32_Ehdr)) return 0;
    const Elf32_Ehdr* eh = (const Elf32_Ehdr*)data;
    if (memcmp(eh->e_ident, ELFMAG, SELFMAG) != 0) return 0;
    if (eh->e_ident[EI_CLASS] != ELFCLASS32) return 0;
    if (eh->e_ident[EI_DATA] != ELFDATA2LSB) return 0;
    if (eh->e_machine != EM_ARM) return 0;
    if (eh->e_type != ET_DYN && eh->e_type != ET_EXEC) return 0;
    if (eh->e_phoff == 0 || eh->e_phnum == 0) return 0;
    return 1;
}

// Translate a virtual address from the file image into a file offset using the
// program headers.  Returns 0 on failure.
static int vaddr_to_offset(const Elf32_Ehdr* eh, uint32_t vaddr, uint32_t* out_offset) {
    const Elf32_Phdr* ph = (const Elf32_Phdr*)((const uint8_t*)eh + eh->e_phoff);
    for (int i = 0; i < eh->e_phnum; ++i) {
        if (ph[i].p_type != PT_LOAD) continue;
        if (vaddr < ph[i].p_vaddr) continue;
        if (vaddr >= ph[i].p_vaddr + ph[i].p_filesz) continue;
        *out_offset = ph[i].p_offset + (vaddr - ph[i].p_vaddr);
        return 1;
    }
    return 0;
}

static const uint8_t* vaddr_ptr(const Elf32_Ehdr* eh, size_t size, uint32_t vaddr) {
    uint32_t offset = 0;
    if (!vaddr_to_offset(eh, vaddr, &offset)) return NULL;
    if (offset > size) return NULL;
    return (const uint8_t*)eh + offset;
}

typedef struct {
    const Elf32_Dyn* dyn;
    size_t dyn_count;
    uint32_t needed[64];
    size_t needed_count;
    uint32_t symtab, strtab, syment;
    uint32_t rel, relsz, rela, relasz;
    uint32_t jmprel, pltrelsz, pltrel;
    uint32_t init, init_array, init_array_sz;
    uint32_t hash, gnu_hash;
} nexus_dynamic;

static int parse_dynamic(const Elf32_Ehdr* eh, size_t size, nexus_dynamic* out) {
    memset(out, 0, sizeof(*out));
    const Elf32_Phdr* ph = (const Elf32_Phdr*)((const uint8_t*)eh + eh->e_phoff);
    for (int i = 0; i < eh->e_phnum; ++i) {
        if (ph[i].p_type != PT_DYNAMIC) continue;
        const uint8_t* p = vaddr_ptr(eh, size, ph[i].p_vaddr);
        if (!p) return 0;
        out->dyn = (const Elf32_Dyn*)p;
        out->dyn_count = ph[i].p_memsz / sizeof(Elf32_Dyn);
        out->syment = sizeof(Elf32_Sym);
        for (size_t d = 0; d < out->dyn_count; ++d) {
            const Elf32_Dyn* e = &out->dyn[d];
            switch (e->d_tag) {
                case DT_NEEDED:
                    if (out->needed_count < 64) out->needed[out->needed_count++] = e->d_un.d_val;
                    break;
                case DT_SYMTAB: out->symtab = e->d_un.d_ptr; break;
                case DT_STRTAB: out->strtab = e->d_un.d_ptr; break;
                case DT_SYMENT: out->syment = e->d_un.d_val; break;
                case DT_REL: out->rel = e->d_un.d_ptr; break;
                case DT_RELSZ: out->relsz = e->d_un.d_val; break;
                case DT_RELA: out->rela = e->d_un.d_ptr; break;
                case DT_RELASZ: out->relasz = e->d_un.d_val; break;
                case DT_JMPREL: out->jmprel = e->d_un.d_ptr; break;
                case DT_PLTRELSZ: out->pltrelsz = e->d_un.d_val; break;
                case DT_PLTREL: out->pltrel = e->d_un.d_val; break;
                case DT_INIT: out->init = e->d_un.d_ptr; break;
                case DT_INIT_ARRAY: out->init_array = e->d_un.d_ptr; break;
                case DT_INIT_ARRAYSZ: out->init_array_sz = e->d_un.d_val; break;
                case DT_HASH: out->hash = e->d_un.d_ptr; break;
                case DT_GNU_HASH: out->gnu_hash = e->d_un.d_ptr; break;
                default: break;
            }
        }
        return 1;
    }
    return 0;
}

static const char* dynstr(const Elf32_Ehdr* eh, size_t size, const nexus_dynamic* dyn,
                          uint32_t offset) {
    if (!dyn->strtab) return NULL;
    const uint8_t* p = vaddr_ptr(eh, size, dyn->strtab + offset);
    return (const char*)p;
}

// Number of entries in the dynamic symbol table.  DT_HASH carries it directly;
// otherwise fall back to the SHT_DYNSYM section size.  Never scan the whole
// file: that produces meaningless "undefined" symbols.
static size_t dynamic_symbol_count(const Elf32_Ehdr* eh, size_t size,
                                   const nexus_dynamic* dyn) {
    if (dyn->hash) {
        const uint8_t* p = vaddr_ptr(eh, size, dyn->hash);
        if (p) {
            uint32_t values[2];
            memcpy(values, p, sizeof(values));
            return values[1];  // nchain == number of symbols
        }
    }
    if (eh->e_shoff && eh->e_shnum) {
        const Elf32_Shdr* sh = (const Elf32_Shdr*)((const uint8_t*)eh + eh->e_shoff);
        if (eh->e_shoff + (size_t)eh->e_shnum * sizeof(Elf32_Shdr) <= size) {
            for (int i = 0; i < eh->e_shnum; ++i) {
                if (sh[i].sh_type == SHT_DYNSYM && sh[i].sh_entsize) {
                    return sh[i].sh_size / sh[i].sh_entsize;
                }
            }
        }
    }
    return 0;
}

static void count_relocs(const Elf32_Ehdr* eh, size_t size, const nexus_dynamic* dyn,
                         nexus_reloc_stats* stats, int include_plt) {
    const uint32_t rel_addr = include_plt ? dyn->jmprel : dyn->rel;
    const uint32_t rel_size = include_plt ? dyn->pltrelsz : dyn->relsz;
    if (!rel_addr || !rel_size) return;
    const uint8_t* base = vaddr_ptr(eh, size, rel_addr);
    if (!base) return;
    const size_t count = rel_size / sizeof(Elf32_Rel);
    for (size_t i = 0; i < count; ++i) {
        const Elf32_Rel* rel = (const Elf32_Rel*)(base + i * sizeof(Elf32_Rel));
        const uint32_t type = ELF32_R_TYPE(rel->r_info);
        stats->total++;
        if (type < 64) stats->by_type[type]++;
        if (type == R_ARM_RELATIVE) stats->relative++;
        (void)eh;
    }
}

static void print_reloc_table(const nexus_reloc_stats* stats) {
    printf("  relocations: %" PRId64 " total, %" PRId64 " R_ARM_RELATIVE\n", stats->total,
           stats->relative);
    for (int type = 0; type < 64; ++type) {
        if (stats->by_type[type] == 0) continue;
        if (type == R_ARM_NONE) continue;
        printf("    %-24s %" PRId64 "\n", arm_reloc_name((uint32_t)type), stats->by_type[type]);
    }
}

static int inspect(const char* path) {
    size_t size = 0;
    uint8_t* data = read_file(path, &size);
    if (!data) {
        fprintf(stderr, "nexus-loader: cannot read %s: %s\n", path, strerror(errno));
        return 1;
    }
    if (!validate_elf32_arm(data, size)) {
        fprintf(stderr, "nexus-loader: %s is not a little-endian ARM32 ELF\n", path);
        free(data);
        return 1;
    }
    const Elf32_Ehdr* eh = (const Elf32_Ehdr*)data;
    printf("ARM32 ELF inspection: %s\n", path);
    printf("  type: %s, entry: 0x%08" PRIx32 "\n",
           eh->e_type == ET_DYN ? "ET_DYN (shared object)" : "ET_EXEC",
           eh->e_entry);

    uint64_t filesz = 0, memsz = 0;
    const Elf32_Phdr* ph = (const Elf32_Phdr*)(data + eh->e_phoff);
    for (int i = 0; i < eh->e_phnum; ++i) {
        if (ph[i].p_type != PT_LOAD) continue;
        filesz += ph[i].p_filesz;
        memsz += ph[i].p_memsz;
    }
    printf("  PT_LOAD bytes: file=%" PRIu64 ", memory=%" PRIu64 ", bias=%s\n", filesz,
           memsz, eh->e_type == ET_DYN ? "required" : "none");

    nexus_dynamic dyn;
    if (!parse_dynamic(eh, size, &dyn)) {
        printf("  dynamic section: none\n");
        free(data);
        return 0;
    }
    printf("  needed libraries: %zu\n", dyn.needed_count);
    for (size_t i = 0; i < dyn.needed_count; ++i) {
        const char* name = dynstr(eh, size, &dyn, dyn.needed[i]);
        printf("    %s\n", name ? name : "(invalid)");
    }

    nexus_reloc_stats stats;
    memset(&stats, 0, sizeof(stats));
    count_relocs(eh, size, &dyn, &stats, 0);
    count_relocs(eh, size, &dyn, &stats, 1);
    print_reloc_table(&stats);

    // Undefined symbols (st_shndx == SHN_UNDEF) that the shim layer must
    // provide.  Report a bounded, readable sample.
    if (dyn.symtab && dyn.strtab) {
        const uint8_t* symtab = vaddr_ptr(eh, size, dyn.symtab);
        const size_t symbol_count = dynamic_symbol_count(eh, size, &dyn);
        if (symtab && symbol_count > 0) {
            size_t undefined = 0;
            size_t shown = 0;
            for (size_t i = 0; i < symbol_count; ++i) {
                const Elf32_Sym* sym = (const Elf32_Sym*)(symtab + i * dyn.syment);
                if ((const uint8_t*)sym + sizeof(*sym) > data + size) break;
                if (sym->st_name == 0) continue;
                if (sym->st_shndx != SHN_UNDEF) continue;
                if (ELF32_ST_BIND(sym->st_info) == STB_LOCAL) continue;
                const char* name = dynstr(eh, size, &dyn, sym->st_name);
                if (!name || !*name) continue;
                undefined++;
                if (shown < 40) {
                    printf("    undefined: %s\n", name);
                    shown++;
                }
            }
            printf("  undefined symbols: %zu%s\n", undefined,
                   undefined > shown ? " (sample shown)" : "");
        }
    }

    // Report whether the JNI and NativeActivity entry points are exported.
    int has_jni_onload = 0;
    if (dyn.symtab && dyn.strtab) {
        const uint8_t* symtab = vaddr_ptr(eh, size, dyn.symtab);
        const size_t symbol_count = dynamic_symbol_count(eh, size, &dyn);
        for (size_t i = 0; symtab && i < symbol_count; ++i) {
            const Elf32_Sym* sym = (const Elf32_Sym*)(symtab + i * dyn.syment);
            if (sym->st_shndx == SHN_UNDEF || sym->st_name == 0) continue;
            const char* name = dynstr(eh, size, &dyn, sym->st_name);
            if (name && strcmp(name, "JNI_OnLoad") == 0) has_jni_onload = 1;
        }
    }
    printf("  exports JNI_OnLoad: %s\n", has_jni_onload ? "yes" : "no");
    printf("  init array: %s\n", dyn.init_array ? "present" : "none");
    free(data);
    return 0;
}

#if defined(__arm__)
// The following mapping/relocation functions are only meaningful inside a
// 32-bit ARM process; guard them so a host build produces no dead code.
static void* resolve_in_table(const char* name) {
    if (!name) return NULL;
    for (size_t t = 0; t < g_symbol_table_count; ++t) {
        const nexus_symbol_entry* table = g_symbol_tables[t];
        for (size_t i = 0; i < g_symbol_counts[t]; ++i) {
            if (table[i].name && strcmp(table[i].name, name) == 0) {
                return table[i].address;
            }
        }
    }
    return NULL;
}

// Translate a module virtual address into the mapped image.  Relocations and
// dynamic tables must be read from the loaded copy, never from the file image.
static uint8_t* mapped_addr(uint8_t* base, uint32_t min_vaddr, uint32_t max_vaddr,
                            uint32_t vaddr) {
    if (vaddr < min_vaddr || vaddr >= max_vaddr) return NULL;
    return base + (vaddr - min_vaddr);
}

// Trim an ELF symbol version suffix ("foo@LIBC") for matching.
static void strip_version(char* name) {
    char* at = strchr(name, '@');
    if (at) *at = '\0';
}

static int load_and_relocate(const char* path) {
    size_t size = 0;
    uint8_t* data = read_file(path, &size);
    if (!data) {
        fprintf(stderr, "nexus-loader: cannot read %s: %s\n", path, strerror(errno));
        return 1;
    }
    if (!validate_elf32_arm(data, size)) {
        fprintf(stderr, "nexus-loader: %s is not a little-endian ARM32 ELF\n", path);
        free(data);
        return 1;
    }
    const Elf32_Ehdr* eh = (const Elf32_Ehdr*)data;
    const Elf32_Phdr* ph = (const Elf32_Phdr*)(data + eh->e_phoff);

    uint32_t min_vaddr = UINT32_MAX, max_vaddr = 0;
    for (int i = 0; i < eh->e_phnum; ++i) {
        if (ph[i].p_type != PT_LOAD) continue;
        const uint32_t start = ph[i].p_vaddr & ~(NEXUS_PAGE - 1);
        const uint32_t end = (ph[i].p_vaddr + ph[i].p_memsz + NEXUS_PAGE - 1) &
                             ~(NEXUS_PAGE - 1);
        if (start < min_vaddr) min_vaddr = start;
        if (end > max_vaddr) max_vaddr = end;
    }
    if (min_vaddr == UINT32_MAX || max_vaddr <= min_vaddr) {
        fprintf(stderr, "nexus-loader: %s has no loadable segments\n", path);
        free(data);
        return 1;
    }
    const size_t span = max_vaddr - min_vaddr;

    // Reserve the whole span first so segments land contiguously and cannot be
    // placed over unrelated mappings.
    void* reserve = mmap(NULL, span, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (reserve == MAP_FAILED) {
        fprintf(stderr, "nexus-loader: cannot reserve %zu bytes: %s\n", span,
                strerror(errno));
        free(data);
        return 1;
    }
    uint8_t* base = (uint8_t*)reserve;
    const intptr_t bias = (intptr_t)base - (intptr_t)min_vaddr;

    for (int i = 0; i < eh->e_phnum; ++i) {
        if (ph[i].p_type != PT_LOAD) continue;
        const uint32_t seg_start = ph[i].p_vaddr & ~(NEXUS_PAGE - 1);
        const uint32_t page_delta = ph[i].p_vaddr - seg_start;
        const size_t seg_len = (size_t)page_delta + ph[i].p_memsz;
        void* target = base + (seg_start - min_vaddr);
        int prot = PROT_READ;
        if (ph[i].p_flags & PF_W) prot |= PROT_WRITE;
        if (ph[i].p_flags & PF_X) prot |= PROT_EXEC;
        if (mmap(target, seg_len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS |
                                                             MAP_FIXED, -1, 0) == MAP_FAILED) {
            fprintf(stderr, "nexus-loader: cannot map segment: %s\n", strerror(errno));
            munmap(reserve, span);
            free(data);
            return 1;
        }
        if (ph[i].p_filesz) {
            memcpy((uint8_t*)target + page_delta, data + ph[i].p_offset, ph[i].p_filesz);
        }
        if (mprotect(target, seg_len, prot) != 0) {
            fprintf(stderr, "nexus-loader: cannot protect segment: %s\n", strerror(errno));
            munmap(reserve, span);
            free(data);
            return 1;
        }
    }

    nexus_dynamic dyn;
    if (!parse_dynamic(eh, size, &dyn)) {
        printf("nexus-loader: loaded at %p (bias %" PRIdPTR "), no dynamic section\n",
               (void*)base, bias);
        free(data);
        return 0;
    }

    const Elf32_Sym* symtab = NULL;
    const char* strtab = NULL;
    if (dyn.symtab >= min_vaddr && dyn.symtab < max_vaddr) {
        symtab = (const Elf32_Sym*)(base + (dyn.symtab - min_vaddr));
    }
    if (dyn.strtab >= min_vaddr && dyn.strtab < max_vaddr) {
        strtab = (const char*)(base + (dyn.strtab - min_vaddr));
    }

    nexus_reloc_stats stats;
    memset(&stats, 0, sizeof(stats));

    // Apply DT_REL and (if REL-format) DT_JMPREL.
    for (int pass = 0; pass < 2; ++pass) {
        const uint32_t addr = pass ? dyn.jmprel : dyn.rel;
        const uint32_t bytes = pass ? dyn.pltrelsz : dyn.relsz;
        if (!addr || !bytes) continue;
        if (pass && dyn.pltrel && dyn.pltrel != DT_REL) {
            printf("  note: PLT relocations are not REL-format; skipping\n");
            continue;
        }
        Elf32_Rel* rel = (Elf32_Rel*)mapped_addr(base, min_vaddr, max_vaddr, addr);
        if (!rel) {
            printf("  note: relocation table at 0x%" PRIx32 " is outside the mapped image; skipping\n",
                   addr);
            continue;
        }
        const size_t count = bytes / sizeof(Elf32_Rel);
        for (size_t i = 0; i < count; ++i) {
            const uint32_t type = ELF32_R_TYPE(rel[i].r_info);
            const uint32_t sym_index = ELF32_R_SYM(rel[i].r_info);
            if (rel[i].r_offset < min_vaddr || rel[i].r_offset + 4 > max_vaddr) {
                record_unresolved(&stats, "(relocation outside mapped range)");
                continue;
            }
            uint32_t* where = (uint32_t*)(base + (rel[i].r_offset - min_vaddr));
            stats.total++;
            if (type < 64) stats.by_type[type]++;
            switch (type) {
                case R_ARM_NONE:
                    stats.applied++;
                    break;
                case R_ARM_RELATIVE: {
                    const uint32_t addend = *where;
                    *where = (uint32_t)(addend + (uint32_t)bias);
                    stats.relative++;
                    stats.applied++;
                    break;
                }
                case R_ARM_ABS32:
                case R_ARM_GLOB_DAT:
                case R_ARM_JUMP_SLOT: {
                    char namebuf[128] = {0};
                    const char* name = NULL;
                    if (symtab && strtab && sym_index != 0) {
                        const Elf32_Sym* sym = &symtab[sym_index];
                        name = strtab + sym->st_name;
                        if (name) {
                            strncpy(namebuf, name, sizeof(namebuf) - 1);
                            strip_version(namebuf);
                            name = namebuf;
                        }
                    }
                    void* target = resolve_in_table(name);
                    if (target) {
                        *where = (uint32_t)(uintptr_t)target;
                        stats.applied++;
                    } else {
                        record_unresolved(&stats, name);
                    }
                    break;
                }
                case R_ARM_COPY:
                    // A copy relocation needs a definition from another
                    // module; without one there is nothing safe to copy.
                    record_unresolved(&stats, "(R_ARM_COPY definition)");
                    break;
                default:
                    record_unresolved(&stats, arm_reloc_name(type));
                    break;
            }
        }
    }

    printf("nexus-loader: loaded %s at %p (bias %" PRIdPTR ")\n", path, (void*)base, bias);
    printf("  relocation summary: total=%" PRId64 " applied=%" PRId64
           " unresolved=%" PRId64 "\n",
           stats.total, stats.applied, stats.unresolved);
    print_reloc_table(&stats);
    if (stats.reported_count) {
        printf("  unresolved symbols (sample):\n");
        for (size_t i = 0; i < stats.reported_count; ++i) {
            printf("    %s\n", stats.reported[i]);
        }
        printf("  -> %zu distinct symbols still need a bionic/GPU/audio shim\n",
               stats.reported_count);
    }
    if (dyn.init_array) {
        printf("  init array present at +0x%" PRIx32 " (%u bytes); not executed yet\n",
               dyn.init_array, dyn.init_array_sz);
    }
    munmap(reserve, span);
    free(data);
    // Unresolved symbols are expected until the shims exist; report them but do
    // not claim the module is runnable.
    return 0;
}

// ---------------------------------------------------------------------------
// Dependency-tree loading (guest only)
//
// The genuine libminecraftpe.so needs libfmod.so, libgnustl_shared.so and
// Android system libraries.  A real loader maps every module first, then
// resolves each relocation against the exports of all modules.
// ---------------------------------------------------------------------------

#define NEXUS_MAX_MODULES 24

typedef struct {
    char name[128];
    char path[512];
    uint8_t* base;
    size_t span;
    uint32_t min_vaddr, max_vaddr;
    intptr_t bias;
    nexus_dynamic dyn;
    const Elf32_Sym* symtab;
    const char* strtab;
    size_t symcount;
} nexus_module;

static nexus_module g_modules[NEXUS_MAX_MODULES];
static size_t g_module_count = 0;

static const char* const g_system_libs[] = {
    "libc.so",   "libm.so",   "libdl.so",      "liblog.so",  "libandroid.so",
    "libEGL.so", "libGLESv2.so", "libOpenSLES.so", "libstdc++.so",
    "libz.so",   "libgcc.so", NULL
};

static int is_system_lib(const char* name) {
    for (size_t i = 0; g_system_libs[i]; ++i) {
        if (strcmp(name, g_system_libs[i]) == 0) return 1;
    }
    return 0;
}

static const char* path_basename(const char* path) {
    const char* slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static void path_dirname(const char* path, char* out, size_t out_size) {
    const char* slash = strrchr(path, '/');
    if (!slash) {
        snprintf(out, out_size, ".");
        return;
    }
    const size_t n = (size_t)(slash - path);
    if (n >= out_size) {
        snprintf(out, out_size, ".");
        return;
    }
    memcpy(out, path, n);
    out[n] = '\0';
}

static int map_image(const uint8_t* data, size_t size, uint8_t** out_base,
                     size_t* out_span, uint32_t* out_min, uint32_t* out_max) {
    const Elf32_Ehdr* eh = (const Elf32_Ehdr*)data;
    const Elf32_Phdr* ph = (const Elf32_Phdr*)(data + eh->e_phoff);
    uint32_t min_vaddr = UINT32_MAX, max_vaddr = 0;
    for (int i = 0; i < eh->e_phnum; ++i) {
        if (ph[i].p_type != PT_LOAD) continue;
        const uint32_t start = ph[i].p_vaddr & ~(NEXUS_PAGE - 1);
        const uint32_t end = (ph[i].p_vaddr + ph[i].p_memsz + NEXUS_PAGE - 1) &
                             ~(NEXUS_PAGE - 1);
        if (start < min_vaddr) min_vaddr = start;
        if (end > max_vaddr) max_vaddr = end;
    }
    if (min_vaddr == UINT32_MAX || max_vaddr <= min_vaddr) return -1;
    const size_t span = max_vaddr - min_vaddr;
    void* reserve = mmap(NULL, span, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (reserve == MAP_FAILED) return -1;
    uint8_t* base = (uint8_t*)reserve;
    for (int i = 0; i < eh->e_phnum; ++i) {
        if (ph[i].p_type != PT_LOAD) continue;
        const uint32_t seg_start = ph[i].p_vaddr & ~(NEXUS_PAGE - 1);
        const uint32_t page_delta = ph[i].p_vaddr - seg_start;
        const size_t seg_len = (size_t)page_delta + ph[i].p_memsz;
        void* target = base + (seg_start - min_vaddr);
        int prot = PROT_READ;
        if (ph[i].p_flags & PF_W) prot |= PROT_WRITE;
        if (ph[i].p_flags & PF_X) prot |= PROT_EXEC;
        if (mmap(target, seg_len, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == MAP_FAILED) {
            munmap(reserve, span);
            return -1;
        }
        if (ph[i].p_filesz) {
            memcpy((uint8_t*)target + page_delta, data + ph[i].p_offset, ph[i].p_filesz);
        }
        if (mprotect(target, seg_len, prot) != 0) {
            munmap(reserve, span);
            return -1;
        }
    }
    *out_base = base;
    *out_span = span;
    *out_min = min_vaddr;
    *out_max = max_vaddr;
    return 0;
}

static int load_module(const char* path, int depth) {
    if (depth > 8) {
        printf("  dependency depth exceeded for %s\n", path);
        return -1;
    }
    const char* name = path_basename(path);
    if (is_system_lib(name)) return 0;  // provided by the guest/bionic layer
    for (size_t i = 0; i < g_module_count; ++i) {
        if (strcmp(g_modules[i].name, name) == 0) return 0;
    }
    if (g_module_count >= NEXUS_MAX_MODULES) {
        printf("  too many modules; cannot load %s\n", path);
        return -1;
    }

    size_t size = 0;
    uint8_t* data = read_file(path, &size);
    if (!data) {
        printf("  missing dependency: %s\n", name);
        return -1;
    }
    if (!validate_elf32_arm(data, size)) {
        printf("  not a little-endian ARM32 ELF: %s\n", path);
        free(data);
        return -1;
    }

    nexus_module* m = &g_modules[g_module_count];
    memset(m, 0, sizeof(*m));
    snprintf(m->name, sizeof(m->name), "%s", name);
    snprintf(m->path, sizeof(m->path), "%s", path);
    if (map_image(data, size, &m->base, &m->span, &m->min_vaddr, &m->max_vaddr) != 0) {
        printf("  cannot map %s\n", path);
        free(data);
        return -1;
    }
    m->bias = (intptr_t)m->base - (intptr_t)m->min_vaddr;
    g_module_count++;

    const Elf32_Ehdr* eh = (const Elf32_Ehdr*)data;
    if (!parse_dynamic(eh, size, &m->dyn)) {
        printf("  %s: no dynamic section\n", m->name);
        free(data);
        return 0;
    }
    m->symcount = dynamic_symbol_count(eh, size, &m->dyn);
    if (m->dyn.symtab >= m->min_vaddr && m->dyn.symtab < m->max_vaddr) {
        m->symtab = (const Elf32_Sym*)mapped_addr(m->base, m->min_vaddr, m->max_vaddr,
                                                  m->dyn.symtab);
    }
    if (m->dyn.strtab >= m->min_vaddr && m->dyn.strtab < m->max_vaddr) {
        m->strtab = (const char*)mapped_addr(m->base, m->min_vaddr, m->max_vaddr,
                                            m->dyn.strtab);
    }
    printf("  loaded %-22s base=%p bias=%" PRIdPTR " symbols=%zu\n", m->name,
           (void*)m->base, m->bias, m->symcount);

    char dir[512];
    path_dirname(path, dir, sizeof(dir));
    for (size_t i = 0; i < m->dyn.needed_count; ++i) {
        const char* needed = dynstr(eh, size, &m->dyn, m->dyn.needed[i]);
        if (!needed || !*needed) continue;
        if (is_system_lib(needed)) {
            printf("    needs system library %s (guest/bionic)\n", needed);
            continue;
        }
        char candidate[768];
        snprintf(candidate, sizeof(candidate), "%s/%s", dir, needed);
        load_module(candidate, depth + 1);
    }
    free(data);
    return 0;
}

// Resolve a symbol name against the shim table, then the exports of every
// loaded module (main module first, so it wins interposition).
static void* find_exported_symbol(const char* name) {
    void* shim = resolve_in_table(name);
    if (shim) return shim;
    for (size_t mi = 0; mi < g_module_count; ++mi) {
        const nexus_module* m = &g_modules[mi];
        if (!m->symtab || !m->strtab) continue;
        for (size_t i = 0; i < m->symcount; ++i) {
            const Elf32_Sym* sym = &m->symtab[i];
            if (sym->st_name == 0 || sym->st_shndx == SHN_UNDEF) continue;
            if (ELF32_ST_BIND(sym->st_info) == STB_LOCAL) continue;
            if (strcmp(m->strtab + sym->st_name, name) == 0) {
                return (void*)(uintptr_t)(sym->st_value + (uint32_t)m->bias);
            }
        }
    }
    return NULL;
}

static void relocate_module_tree(nexus_module* m, nexus_reloc_stats* total) {
    nexus_reloc_stats stats;
    memset(&stats, 0, sizeof(stats));
    uint32_t* cache = m->symcount ? (uint32_t*)calloc(m->symcount, sizeof(uint32_t)) : NULL;
    uint8_t* done = m->symcount ? (uint8_t*)calloc(m->symcount, 1) : NULL;

    for (int pass = 0; pass < 2; ++pass) {
        const uint32_t addr = pass ? m->dyn.jmprel : m->dyn.rel;
        const uint32_t bytes = pass ? m->dyn.pltrelsz : m->dyn.relsz;
        if (!addr || !bytes) continue;
        if (pass && m->dyn.pltrel && m->dyn.pltrel != DT_REL) continue;
        const Elf32_Rel* rel = (const Elf32_Rel*)mapped_addr(m->base, m->min_vaddr,
                                                             m->max_vaddr, addr);
        if (!rel) continue;
        const size_t count = bytes / sizeof(Elf32_Rel);
        for (size_t i = 0; i < count; ++i) {
            const uint32_t type = ELF32_R_TYPE(rel[i].r_info);
            const uint32_t sym_index = ELF32_R_SYM(rel[i].r_info);
            uint32_t* where = (uint32_t*)mapped_addr(m->base, m->min_vaddr, m->max_vaddr,
                                                     rel[i].r_offset);
            if (!where) {
                record_unresolved(&stats, "(relocation outside mapped image)");
                continue;
            }
            stats.total++;
            if (type < 64) stats.by_type[type]++;
            switch (type) {
                case R_ARM_NONE:
                    stats.applied++;
                    break;
                case R_ARM_RELATIVE: {
                    const uint32_t addend = *where;
                    *where = addend + (uint32_t)m->bias;
                    stats.relative++;
                    stats.applied++;
                    break;
                }
                case R_ARM_ABS32:
                case R_ARM_GLOB_DAT:
                case R_ARM_JUMP_SLOT: {
                    const char* name = NULL;
                    void* target = NULL;
                    if (m->symtab && m->strtab && sym_index != 0 &&
                        sym_index < m->symcount) {
                        const Elf32_Sym* sym = &m->symtab[sym_index];
                        name = m->strtab + sym->st_name;
                    }
                    if (name && cache && done) {
                        if (done[sym_index]) {
                            if (cache[sym_index]) target = (void*)(uintptr_t)cache[sym_index];
                        } else {
                            target = find_exported_symbol(name);
                            cache[sym_index] = (uint32_t)(uintptr_t)target;
                            done[sym_index] = 1;
                        }
                    }
                    if (target) {
                        // R_ARM_ABS32 uses the value stored at the site as the
                        // addend; GLOB_DAT/JUMP_SLOT are S.
                        const uint32_t addend = (type == R_ARM_ABS32) ? *where : 0;
                        *where = (uint32_t)(uintptr_t)target + addend;
                        stats.applied++;
                    } else {
                        record_unresolved(&stats, name ? name : arm_reloc_name(type));
                    }
                    break;
                }
                case R_ARM_COPY:
                    record_unresolved(&stats, "(R_ARM_COPY definition)");
                    break;
                default:
                    record_unresolved(&stats, arm_reloc_name(type));
                    break;
            }
        }
    }
    free(cache);
    free(done);

    printf("  relocated %-22s total=%" PRId64 " applied=%" PRId64
           " unresolved=%" PRId64 "\n",
           m->name, stats.total, stats.applied, stats.unresolved);
    if (stats.reported_count) {
        for (size_t i = 0; i < stats.reported_count; ++i) {
            printf("    unresolved: %s\n", stats.reported[i]);
        }
        if (stats.reported_count >= NEXUS_MAX_REPORTED_SYMBOLS) {
            printf("    ... (report capped at %d distinct names)\n",
                   NEXUS_MAX_REPORTED_SYMBOLS);
        }
    }
    total->total += stats.total;
    total->applied += stats.applied;
    total->unresolved += stats.unresolved;
    total->relative += stats.relative;
}

static int load_tree(const char* path) {
    printf("NEXUS ARM32 dependency-tree load: %s\n", path);
    // Register the guest bionic/Android shim symbols before resolving.
    extern void nexus_register_bionic_symbols(void);
    nexus_register_bionic_symbols();
    nexus_register_android_shims();
    nexus_register_gfx_symbols();
    if (load_module(path, 0) != 0 || g_module_count == 0) {
        printf("nexus-loader: could not load %s\n", path);
        return 1;
    }
    printf("  modules loaded: %zu\n", g_module_count);
    nexus_reloc_stats total;
    memset(&total, 0, sizeof(total));
    for (size_t i = 0; i < g_module_count; ++i) {
        relocate_module_tree(&g_modules[i], &total);
    }
    printf("  TOTAL: modules=%zu relocations=%" PRId64 " applied=%" PRId64
           " unresolved=%" PRId64 "\n",
           g_module_count, total.total, total.applied, total.unresolved);
    return 0;
}

/*
 * Run the DT_INIT/DT_INIT_ARRAY constructors of every loaded module in
 * dependency order (dependencies were appended after the main module, so the
 * reverse order runs libgnustl_shared/libfmod before libminecraftpe).
 *
 * Constructors are real game code.  If one crashes, the process crashes; there
 * is no "pretend it succeeded" path.
 */
static int run_init_arrays(void) {
    for (size_t mi = g_module_count; mi-- > 0;) {
        nexus_module* m = &g_modules[mi];
        if (m->dyn.init) {
            void (*init)(void) = (void (*)(void))(uintptr_t)(m->dyn.init + (uint32_t)m->bias);
            printf("  init %-22s DT_INIT %p\n", m->name, (void*)init);
            fflush(stdout);
            init();
        }
        if (m->dyn.init_array && m->dyn.init_array_sz) {
            uint32_t* entries = (uint32_t*)mapped_addr(m->base, m->min_vaddr, m->max_vaddr,
                                                       m->dyn.init_array);
            if (!entries) {
                printf("  init %-22s init_array outside image\n", m->name);
                return 1;
            }
            const size_t count = m->dyn.init_array_sz / sizeof(uint32_t);
            printf("  init %-22s %zu constructor(s)\n", m->name, count);
            fflush(stdout);
            for (size_t i = 0; i < count; ++i) {
                uint32_t addr = entries[i];
                if (!addr) continue;
                void (*ctor)(void) = (void (*)(void))(uintptr_t)addr;
                ctor();
            }
        }
    }
    return 0;
}

static int run_jni_probe(const char* path, int log_calls) {
    if (load_tree(path) != 0) return 1;
    if (run_init_arrays() != 0) return 1;

    void* fn = find_exported_symbol("JNI_OnLoad");
    if (!fn) {
        printf("  JNI_OnLoad: not exported\n");
        return 1;
    }
    printf("  JNI_OnLoad: %p\n", fn);
    fflush(stdout);
    const jint version = nexus_run_jni_onload(fn, log_calls);
    printf("  JNI_OnLoad result: 0x%x (%d)\n", (unsigned)version, (int)version);
    if (version <= 0) {
        printf("  FAIL: JNI_OnLoad did not return a JNI version\n");
        return 1;
    }
    printf("  OK: genuine JNI_OnLoad executed in the in-tree ARM32 runtime\n");
    return 0;
}

#endif // defined(__arm__)

static void usage(const char* argv0) {
    printf("NEXUS in-tree ARM32 ELF loader\n\n");
    printf("Usage:\n");
    printf("  %s --inspect FILE   parse an ARM32 ELF and report its needs\n", argv0);
    printf("  %s --load FILE      map and relocate in this process (ARM32 only)\n", argv0);
    printf("  %s --load-tree FILE map a module and its dependencies (ARM32 only)\n", argv0);
    printf("  %s --jni-onload FILE [--quiet]\n"
           "                      load, run constructors and call JNI_OnLoad (ARM32 only)\n", argv0);
    printf("  %s --help\n", argv0);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        usage(argv[0]);
        return 2;
    }
    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        usage(argv[0]);
        return 0;
    }
    if (argc < 3) {
        fprintf(stderr, "nexus-loader: missing file argument\n");
        return 2;
    }
    if (strcmp(argv[1], "--inspect") == 0) return inspect(argv[2]);
    if (strcmp(argv[1], "--load") == 0) {
#if defined(__arm__)
        return load_and_relocate(argv[2]);
#else
        // On a non-ARM host, printing a relocation report is still useful, but
        // executing or applying 32-bit ARM relocations into a 64-bit address
        // space would be wrong.  Fall back to inspection.
        printf("nexus-loader: host is not ARM32; running inspection instead\n");
        return inspect(argv[2]);
#endif
    }
    if (strcmp(argv[1], "--load-tree") == 0) {
#if defined(__arm__)
        return load_tree(argv[2]);
#else
        printf("nexus-loader: host is not ARM32; running inspection instead\n");
        return inspect(argv[2]);
#endif
    }
    if (strcmp(argv[1], "--jni-onload") == 0) {
#if defined(__arm__)
        const int log_calls = !(argc > 3 && strcmp(argv[3], "--quiet") == 0);
        return run_jni_probe(argv[2], log_calls);
#else
        printf("nexus-loader: host is not ARM32; running inspection instead\n");
        return inspect(argv[2]);
#endif
    }
    fprintf(stderr, "nexus-loader: unknown mode '%s'\n", argv[1]);
    usage(argv[0]);
    return 2;
}
