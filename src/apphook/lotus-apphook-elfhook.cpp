/*
 * SPDX-FileCopyrightText: 2026 Kazoku <k4zoku@disr.it>
 *
 * SPDX-License-Identifier: MIT
 *
 */

/**
 * @file lotus-apphook-elfhook.cpp
 * @brief Patches imported ELF relocation slots for keyboard event interception.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "lotus-apphook-elfhook.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <elf.h>
#include <link.h>
#include <memory>
#include <mutex>
#include <sys/mman.h>
#include <unistd.h>

namespace lotus::apphook {
    namespace {

        std::mutex hookMutex;

        struct WalkContext {
            std::span<const HookSpec> hooks;
            int                       patched = 0;
        };

        /** @brief Returns the native ELF class's relocation symbol index. */
        [[nodiscard]] size_t symbolIndex(ElfW(Xword) info) {
#if __SIZEOF_POINTER__ == 8
            return ELF64_R_SYM(info);
#else
            return ELF32_R_SYM(info);
#endif
        }

        /**
         * @brief Writes one ELF relocation slot with its original page protection restored.
         */
        [[nodiscard]] bool writeSlot(void** slot, void* replacement) {
            const long systemPageSize = sysconf(_SC_PAGESIZE);
            if (systemPageSize <= 0 || reinterpret_cast<uintptr_t>(slot) % alignof(void*) != 0)
                return false;
            const auto      pageSize  = static_cast<uintptr_t>(systemPageSize);
            const uintptr_t page      = reinterpret_cast<uintptr_t>(slot) - (reinterpret_cast<uintptr_t>(slot) % pageSize);
            const auto      closeFile = [](FILE* file) {
                if (file)
                    std::fclose(file);
            };
            std::unique_ptr<FILE, decltype(closeFile)> maps(std::fopen("/proc/self/maps", "r"), closeFile);
            if (!maps)
                return false;

            char          line[512]{};
            char          permissions[8]{};
            unsigned long first      = 0;
            unsigned long last       = 0;
            int           protection = 0;
            bool          found      = false;
            while (std::fgets(line, sizeof(line), maps.get())) {
                if (std::sscanf(line, "%lx-%lx %7s", &first, &last, permissions) != 3 || reinterpret_cast<uintptr_t>(slot) < first ||
                    reinterpret_cast<uintptr_t>(slot) > last - sizeof(void*)) {
                    continue;
                }
                protection = (permissions[0] == 'r' ? PROT_READ : 0) | (permissions[1] == 'w' ? PROT_WRITE : 0) | (permissions[2] == 'x' ? PROT_EXEC : 0);
                found      = true;
                break;
            }
            if (!found || !(protection & PROT_READ))
                return false;

            const bool readonly = (protection & PROT_WRITE) == 0;
            if (readonly && mprotect(reinterpret_cast<void*>(page), pageSize, protection | PROT_WRITE) != 0)
                return false;
            __atomic_store_n(slot, replacement, __ATOMIC_RELEASE);
            if (readonly && mprotect(reinterpret_cast<void*>(page), pageSize, protection) != 0)
                std::fprintf(stderr, "lotus-apphook: failed to restore ELF relocation page protection\n");
            return true;
        }

        /** @brief Resolves an ELF dynamic table pointer against its load base. */
        [[nodiscard]] uintptr_t dynamicPointer(uintptr_t base, uintptr_t value) {
            return value < base ? base + value : value;
        }

        /**
         * @brief Patches imported function-pointer slots from ELF32/ELF64 REL or RELA tables.
         *
         * DT_JMPREL contains the ABI's PLT relocations. For non-PLT relocations,
         * require the existing slot to resolve to the requested exported function;
         * this excludes PC-relative, TLS and non-pointer relocation types without
         * hard-coding architecture-specific relocation numbers.
         */
        template <typename Relocation>
        void patchRelocations(uintptr_t base, const Relocation* relocations, size_t count, bool plt, const ElfW(Sym)* symbols, const char* strings, WalkContext& context) {
            for (size_t i = 0; i < count; ++i) {
                const auto& relocation = relocations[i];
                const size_t index      = symbolIndex(relocation.r_info);
                if (index == 0)
                    continue;
                const auto& symbol = symbols[index];
                const unsigned type = ELF64_ST_TYPE(symbol.st_info);
                if (symbol.st_shndx != SHN_UNDEF || (type != STT_FUNC && type != STT_NOTYPE))
                    continue;
                const char* name = strings + symbol.st_name;
                for (const HookSpec& hook : context.hooks) {
                    if (std::strcmp(name, hook.symbol) != 0)
                        continue;
                    auto** slot = reinterpret_cast<void**>(base + relocation.r_offset);
                    if (reinterpret_cast<uintptr_t>(slot) % alignof(void*) != 0)
                        break;
                    void* current = __atomic_load_n(slot, __ATOMIC_ACQUIRE);
                    if (current == hook.replacement)
                        break;
                    if (!plt) {
                        Dl_info currentInfo{};
                        if (!dladdr(current, &currentInfo) || !currentInfo.dli_sname || !currentInfo.dli_saddr || currentInfo.dli_saddr != current ||
                            std::strcmp(currentInfo.dli_sname, hook.symbol) != 0)
                            break;
                    }
                    if (writeSlot(slot, hook.replacement))
                        ++context.patched;
                    break;
                }
            }
        }

        /** @brief Visits one loaded ELF object to replace imported hook symbols. */
        int visitObject(dl_phdr_info* info, size_t, void* userData) {
            if (info->dlpi_name && std::strstr(info->dlpi_name, "liblotus-apphook"))
                return 0;
            auto&             context = *static_cast<WalkContext*>(userData);
            const uintptr_t   base    = static_cast<uintptr_t>(info->dlpi_addr);
            const ElfW(Dyn)* dynamic = nullptr;
            for (size_t i = 0; i < info->dlpi_phnum; ++i) {
                if (info->dlpi_phdr[i].p_type == PT_DYNAMIC) {
                    dynamic = reinterpret_cast<const ElfW(Dyn)*>(base + info->dlpi_phdr[i].p_vaddr);
                    break;
                }
            }
            if (!dynamic)
                return 0;

            const ElfW(Sym)* symbols = nullptr;
            const char* strings      = nullptr;
            uintptr_t jumpRel        = 0;
            uintptr_t rel            = 0;
            uintptr_t rela           = 0;
            size_t jumpSize          = 0;
            size_t relSize           = 0;
            size_t relaSize          = 0;
            int    pltType           = 0;
            for (const ElfW(Dyn)* entry = dynamic; entry->d_tag != DT_NULL; ++entry) {
                switch (entry->d_tag) {
                    case DT_SYMTAB: symbols = reinterpret_cast<const ElfW(Sym)*>(dynamicPointer(base, entry->d_un.d_ptr)); break;
                    case DT_STRTAB: strings = reinterpret_cast<const char*>(dynamicPointer(base, entry->d_un.d_ptr)); break;
                    case DT_JMPREL: jumpRel = dynamicPointer(base, entry->d_un.d_ptr); break;
                    case DT_PLTRELSZ: jumpSize = entry->d_un.d_val; break;
                    case DT_PLTREL: pltType = entry->d_un.d_val; break;
                    case DT_REL: rel = dynamicPointer(base, entry->d_un.d_ptr); break;
                    case DT_RELSZ: relSize = entry->d_un.d_val; break;
                    case DT_RELA: rela = dynamicPointer(base, entry->d_un.d_ptr); break;
                    case DT_RELASZ: relaSize = entry->d_un.d_val; break;
                    default: break;
                }
            }
            if (!symbols || !strings)
                return 0;
            // The dynamic section can list DT_JMPREL inside DT_REL(A)'s range.
            // Already-patched slots are ignored, so either order is safe.
            if (jumpRel && pltType == DT_REL)
                patchRelocations(base, reinterpret_cast<const ElfW(Rel)*>(jumpRel), jumpSize / sizeof(ElfW(Rel)), true, symbols, strings, context);
            if (jumpRel && pltType == DT_RELA)
                patchRelocations(base, reinterpret_cast<const ElfW(Rela)*>(jumpRel), jumpSize / sizeof(ElfW(Rela)), true, symbols, strings, context);
            if (rel)
                patchRelocations(base, reinterpret_cast<const ElfW(Rel)*>(rel), relSize / sizeof(ElfW(Rel)), false, symbols, strings, context);
            if (rela)
                patchRelocations(base, reinterpret_cast<const ElfW(Rela)*>(rela), relaSize / sizeof(ElfW(Rela)), false, symbols, strings, context);
            return 0;
        }

    } // namespace

    int installHooks(std::span<const HookSpec> hooks) {
        std::scoped_lock lock(hookMutex);
        WalkContext      context{.hooks = hooks};
        dl_iterate_phdr(&visitObject, &context);
        return context.patched;
    }

} // namespace lotus::apphook
