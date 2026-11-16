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
#include <stdint.h>
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
    uintptr_t addr  = (uintptr_t)slot;
    uintptr_t start = 0, end = 0;
    char      perms[5] = {0};
    char      line[512];
    int       protection = 0;
    while (fgets(line, sizeof(line), maps)) {
        unsigned long a = 0, b = 0;
        char          p[5] = {0};
        if (sscanf(line, "%lx-%lx %4s", &a, &b, p) != 3)
            continue;
        if (addr < (uintptr_t)a || addr > (uintptr_t)b || (uintptr_t)b - addr < sizeof(void*))
            continue;
        start = (uintptr_t)a;
        end   = (uintptr_t)b;
        memcpy(perms, p, sizeof(perms));
        break;
    }
    fclose(maps);
    if (!start || !end || perms[0] != 'r' || perms[2] == 'x')
        return 0;
    protection        = PROT_READ | (perms[1] == 'w' ? PROT_WRITE : 0);
    uintptr_t page    = (addr / page_size) * page_size;
    int       changed = !(protection & PROT_WRITE);
    if (changed && mprotect((void*)page, page_size, protection | PROT_WRITE) != 0)
        return 0;
    /* No clobber when another thread or interposer changed the binding. */
    int replaced = __atomic_compare_exchange_n(slot, &old, replacement, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    if (changed && mprotect((void*)page, page_size, protection) != 0) {
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
        ElfW(Xword) rinfo;
        ElfW(Addr) offset;
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
            if (!hook->native || strcmp(name, hook->name))
                continue;
            uintptr_t addr = base + offset;
            void**    slot = (void**)addr;
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
            dyn = (const ElfW(Dyn)*)(base + info->dlpi_phdr[i].p_vaddr);
            break;
        }
    if (!dyn)
        return 0;
    const ElfW(Sym)* syms = NULL;
    const char* strs      = NULL;
    size_t      strsz     = 0;
    uintptr_t   plt = 0, rel = 0, rela = 0;
    size_t      pltsz = 0, relsz = 0, relasz = 0;
    int         pltrel = 0;
    for (const ElfW(Dyn)* d = dyn; d->d_tag != DT_NULL; ++d) {
        switch (d->d_tag) {
            case DT_SYMTAB: syms = (const ElfW(Sym)*)dyn_addr(base, d->d_un.d_ptr); break;
            case DT_STRTAB: strs = (const char*)dyn_addr(base, d->d_un.d_ptr); break;
            case DT_STRSZ: strsz = d->d_un.d_val; break;
            case DT_JMPREL: plt = dyn_addr(base, d->d_un.d_ptr); break;
            case DT_PLTRELSZ: pltsz = d->d_un.d_val; break;
            case DT_PLTREL: pltrel = d->d_un.d_val; break;
            case DT_REL: rel = dyn_addr(base, d->d_un.d_ptr); break;
            case DT_RELSZ: relsz = d->d_un.d_val; break;
            case DT_RELA: rela = dyn_addr(base, d->d_un.d_ptr); break;
            case DT_RELASZ: relasz = d->d_un.d_val; break;
            default: break;
        }
    }
    if (!syms || !strs || !strsz)
        return 0;
    if (plt && pltrel == DT_RELA)
        scan_relocs(info, scan, base, (void*)plt, pltsz, 1, 1, syms, strs, strsz);
    else if (plt && pltrel == DT_REL)
        scan_relocs(info, scan, base, (void*)plt, pltsz, 0, 1, syms, strs, strsz);
    if (rel)
        scan_relocs(info, scan, base, (void*)rel, relsz, 0, 0, syms, strs, strsz);
    if (rela)
        scan_relocs(info, scan, base, (void*)rela, relasz, 1, 0, syms, strs, strsz);
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
