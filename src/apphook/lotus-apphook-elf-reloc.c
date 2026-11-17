/*
 * SPDX-FileCopyrightText: 2026 Kazoku <k4zoku@disr.it>
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Optional, architecture-neutral ELF relocation interception.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dlfcn.h>
#include <elf.h>
#include <link.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

extern void* lotus_apphook_native_dlsym(void*, const char*) __attribute__((visibility("hidden")));
extern int   wl_proxy_add_listener(void*, void*, void*);
extern int   wl_proxy_add_dispatcher(void*, void*, const void*, void*);

struct Hook {
    const char* name;
    void*       native;
    void*       replacement;
};
struct Scan {
    struct Hook hooks[2];
    size_t      page_size;
    int         patched;
};

static uintptr_t dyn_addr(uintptr_t base, uintptr_t value) {
    return value < base ? base + value : value;
}

/* Loader data (dl_iterate_phdr bases, dynamic entries, relocation offsets)
 * is integer-typed by the ELF ABI; this is the one conversion point. */
static void* loader_pointer(uintptr_t address) {
    return (void*)address; /* NOLINT(performance-no-int-to-ptr) */
}

/* Parses "start-end perms" from a /proc/self/maps line. */
static int parse_mapping(const char* line, uintptr_t* start, uintptr_t* end, char perms[5]) {
    char* cursor      = NULL;
    errno             = 0;
    unsigned long low = strtoul(line, &cursor, 16);
    if (cursor == line || *cursor != '-' || errno)
        return 0;
    const char*   high_text = cursor + 1;
    unsigned long high      = strtoul(high_text, &cursor, 16);
    if (cursor == high_text || errno)
        return 0;
    while (*cursor == ' ' || *cursor == '\t')
        ++cursor;
    size_t length = 0;
    while (length < 4 && cursor[length] && cursor[length] != ' ' && cursor[length] != '\t' && cursor[length] != '\n') {
        perms[length] = cursor[length];
        ++length;
    }
    if (!length)
        return 0;
    perms[length] = '\0';
    *start        = (uintptr_t)low;
    *end          = (uintptr_t)high;
    return 1;
}

static size_t reloc_sym(ElfW(Xword) info) {
#if __SIZEOF_POINTER__ == 8
    return ELF64_R_SYM(info);
#else
    return ELF32_R_SYM(info);
#endif
}

/* A pointer-valued import is required. Avoid modifying arbitrary relocations
 * or a slot already owned by another interposer. */
static int patch_slot(void** slot, void* old, void* replacement, size_t page_size) {
    if ((uintptr_t)slot % sizeof(void*))
        return 0;
    FILE* maps = fopen("/proc/self/maps", "r");
    if (!maps)
        return 0;
    uintptr_t addr     = (uintptr_t)slot;
    uintptr_t start    = 0;
    uintptr_t end      = 0;
    char      perms[5] = {0};
    char      line[512];
    while (fgets(line, sizeof(line), maps)) {
        uintptr_t a    = 0;
        uintptr_t b    = 0;
        char      p[5] = {0};
        if (!parse_mapping(line, &a, &b, p))
            continue;
        if (addr < a || addr > b || b - addr < sizeof(void*))
            continue;
        start = a;
        end   = b;
        memcpy(perms, p, sizeof(perms));
        break;
    }
    fclose(maps);
    if (!start || !end || perms[0] != 'r' || perms[2] == 'x')
        return 0;
    const int writable   = PROT_READ | PROT_WRITE;
    const int protection = perms[1] == 'w' ? writable : PROT_READ;
    /* Page start derived from the slot pointer itself. */
    char*     page    = (char*)slot - (addr % page_size);
    const int changed = perms[1] != 'w';
    if (changed && mprotect(page, page_size, writable) != 0)
        return 0;
    /* No clobber when another thread or interposer changed the binding. */
    int replaced = (int)__atomic_compare_exchange_n(slot, &old, replacement, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    if (changed && mprotect(page, page_size, protection) != 0) {
        /* Failure to restore protection is not an acceptable silent success. */
        fprintf(stderr, "lotus-apphook: cannot restore relocation page protection\n");
    }
    return replaced;
}

/* PLT entries can initially point to a lazy resolver stub inside the
 * importing module. Other relocation kinds must already contain the exact
 * native function address. */
static int is_lazy_plt(void* current, const struct dl_phdr_info* info) {
    uintptr_t a    = (uintptr_t)current;
    uintptr_t base = (uintptr_t)info->dlpi_addr;
    for (size_t i = 0; i < info->dlpi_phnum; ++i) {
        const ElfW(Phdr)* ph = &info->dlpi_phdr[i];
        if (ph->p_type != PT_LOAD || !(ph->p_flags & PF_X))
            continue;
        uintptr_t from = base + ph->p_vaddr;
        if (a >= from && a - from < ph->p_memsz)
            return 1;
    }
    return 0;
}

static void scan_relocs(const struct dl_phdr_info* info, struct Scan* scan, uintptr_t base, const void* rels, size_t bytes, int is_rela, int is_plt, const ElfW(Sym) * syms,
                        const char* strs, size_t strsz) {
    size_t stride = is_rela ? sizeof(ElfW(Rela)) : sizeof(ElfW(Rel));
    if (!rels || !stride || bytes % stride)
        return;
    for (size_t i = 0; i < bytes / stride; ++i) {
        ElfW(Xword) rinfo = 0;
        ElfW(Addr) offset = 0;
        if (is_rela) {
            const ElfW(Rela)* r = (const ElfW(Rela)*)rels + i;
            rinfo               = r->r_info;
            offset              = r->r_offset;
        } else {
            const ElfW(Rel)* r = (const ElfW(Rel)*)rels + i;
            rinfo              = r->r_info;
            offset             = r->r_offset;
        }
        size_t si = reloc_sym(rinfo);
        if (!si)
            continue;
        const ElfW(Sym)* symbol = syms + si;
        if (symbol->st_shndx != SHN_UNDEF || symbol->st_name >= strsz)
            continue;
        unsigned char st_type = ELF64_ST_TYPE(symbol->st_info);
        if (st_type != STT_FUNC && st_type != STT_NOTYPE)
            continue;
        const char* name = strs + symbol->st_name;
        for (size_t h = 0; h < 2; ++h) {
            const struct Hook* hook = &scan->hooks[h];
            if (!hook->native || strcmp(name, hook->name) != 0)
                continue;
            uintptr_t addr = base + offset;
            void**    slot = (void**)loader_pointer(addr);
            if (addr % sizeof(void*))
                break;
            /* Only read the slot after confirming it is an address-bearing
             * relocation in a mapped load segment. */
            int mapped = 0;
            for (size_t p = 0; p < info->dlpi_phnum; ++p) {
                const ElfW(Phdr)* ph = &info->dlpi_phdr[p];
                if (ph->p_type != PT_LOAD || !(ph->p_flags & PF_R))
                    continue;
                uintptr_t low = base + ph->p_vaddr;
                if (addr >= low && addr - low <= ph->p_memsz && ph->p_memsz - (addr - low) >= sizeof(void*)) {
                    mapped = 1;
                    break;
                }
            }
            if (!mapped)
                break;
            void* current = __atomic_load_n(slot, __ATOMIC_ACQUIRE);
            if (current == hook->replacement)
                break;
            if (current != hook->native && !(is_plt && is_lazy_plt(current, info)))
                break;
            scan->patched += patch_slot(slot, current, hook->replacement, scan->page_size);
            break;
        }
    }
}

static int scan_object(struct dl_phdr_info* info, size_t size, void* opaque) {
    (void)size;
    struct Scan* scan = opaque;
    const char*  obj  = info->dlpi_name ? info->dlpi_name : "";
    if (strstr(obj, "liblotus-apphook") || strstr(obj, "libwayland-client"))
        return 0;
    uintptr_t base       = (uintptr_t)info->dlpi_addr;
    const ElfW(Dyn)* dyn = NULL;
    for (size_t i = 0; i < info->dlpi_phnum; ++i)
        if (info->dlpi_phdr[i].p_type == PT_DYNAMIC) {
            dyn = loader_pointer(base + info->dlpi_phdr[i].p_vaddr);
            break;
        }
    if (!dyn)
        return 0;
    const ElfW(Sym)* syms = NULL;
    const char* strs      = NULL;
    size_t      strsz     = 0;
    const void* plt       = NULL;
    const void* rel       = NULL;
    const void* rela      = NULL;
    size_t      pltsz     = 0;
    size_t      relsz     = 0;
    size_t      relasz    = 0;
    ElfW(Xword) pltrel    = 0;
    for (const ElfW(Dyn)* d = dyn; d->d_tag != DT_NULL; ++d) {
        switch (d->d_tag) {
            case DT_SYMTAB: syms = loader_pointer(dyn_addr(base, d->d_un.d_ptr)); break;
            case DT_STRTAB: strs = loader_pointer(dyn_addr(base, d->d_un.d_ptr)); break;
            case DT_STRSZ: strsz = d->d_un.d_val; break;
            case DT_JMPREL: plt = loader_pointer(dyn_addr(base, d->d_un.d_ptr)); break;
            case DT_PLTRELSZ: pltsz = d->d_un.d_val; break;
            case DT_PLTREL: pltrel = d->d_un.d_val; break;
            case DT_REL: rel = loader_pointer(dyn_addr(base, d->d_un.d_ptr)); break;
            case DT_RELSZ: relsz = d->d_un.d_val; break;
            case DT_RELA: rela = loader_pointer(dyn_addr(base, d->d_un.d_ptr)); break;
            case DT_RELASZ: relasz = d->d_un.d_val; break;
            default: break;
        }
    }
    if (!syms || !strs || !strsz)
        return 0;
    if (plt && pltrel == DT_RELA)
        scan_relocs(info, scan, base, plt, pltsz, 1, 1, syms, strs, strsz);
    else if (plt && pltrel == DT_REL)
        scan_relocs(info, scan, base, plt, pltsz, 0, 1, syms, strs, strsz);
    if (rel)
        scan_relocs(info, scan, base, rel, relsz, 0, 0, syms, strs, strsz);
    if (rela)
        scan_relocs(info, scan, base, rela, relasz, 1, 0, syms, strs, strsz);
    return 0;
}

/* Called only once libwayland-client has been observed by the preload shim.
 * Never initializes the runtime or activates the Wayland backend. */
__attribute__((visibility("hidden"))) int lotus_apphook_patch_wayland_imports(void) {
    void* wayland = dlopen("libwayland-client.so.0", RTLD_LAZY | RTLD_NOLOAD | RTLD_LOCAL);
    if (!wayland)
        return 0;
    struct Scan scan = {.page_size = (size_t)sysconf(_SC_PAGESIZE)};
    if (!scan.page_size) {
        dlclose(wayland);
        return 0;
    }
    scan.hooks[0] = (struct Hook){"wl_proxy_add_listener", lotus_apphook_native_dlsym(wayland, "wl_proxy_add_listener"), (void*)wl_proxy_add_listener};
    scan.hooks[1] = (struct Hook){"wl_proxy_add_dispatcher", lotus_apphook_native_dlsym(wayland, "wl_proxy_add_dispatcher"), (void*)wl_proxy_add_dispatcher};
    if (scan.hooks[0].native || scan.hooks[1].native)
        dl_iterate_phdr(scan_object, &scan);
    dlclose(wayland);
    return scan.patched;
}
