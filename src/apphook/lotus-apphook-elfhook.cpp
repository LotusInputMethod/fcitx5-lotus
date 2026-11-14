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

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <elf.h>
#include <link.h>
#include <mutex>
#include <memory>
#include <sys/mman.h>
#include <unistd.h>

#if !defined(__x86_64__)
#error "This GOT/PLT relocation walker targets Linux x86-64"
#endif

namespace lotus::apphook {
    namespace {

        std::mutex hookMutex;

        struct WalkContext {
            std::span<const HookSpec> hooks;
            int                       patched = 0;
        };

        /**
         * @brief Writes one ELF relocation slot with its original page protection restored.
         * @param slot Relocation slot to patch.
         * @param replacement Replacement function address.
         * @return True on successful write.
         */
        [[nodiscard]] bool writeSlot(void** slot, void* replacement) {
            const auto      pageSize  = static_cast<uintptr_t>(sysconf(_SC_PAGESIZE));
            const uintptr_t page      = reinterpret_cast<uintptr_t>(slot) & ~(pageSize - 1);
            const auto      closeFile = [](FILE* file) {
                if (file)
                    std::fclose(file);
            };
            std::unique_ptr<FILE, decltype(closeFile)> maps(std::fopen("/proc/self/maps", "r"), closeFile);
            if (!maps) {
                return false;
            }

            char          line[512]{};
            char          permissions[8]{};
            unsigned long first      = 0;
            unsigned long last       = 0;
            int           protection = 0;
            bool          found      = false;
            while (std::fgets(line, sizeof(line), maps.get())) {
                if (std::sscanf(line, "%lx-%lx %7s", &first, &last, permissions) != 3 || reinterpret_cast<uintptr_t>(slot) < first || reinterpret_cast<uintptr_t>(slot) >= last) {
                    continue;
                }
                protection = (permissions[0] == 'r' ? PROT_READ : 0) | (permissions[1] == 'w' ? PROT_WRITE : 0) | (permissions[2] == 'x' ? PROT_EXEC : 0);
                found      = true;
                break;
            }
            if (!found) {
                return false;
            }

            const bool readonly = (protection & PROT_WRITE) == 0;
            if (readonly && mprotect(reinterpret_cast<void*>(page), pageSize, protection | PROT_WRITE) != 0) {
                return false;
            }
            __atomic_store_n(slot, replacement, __ATOMIC_RELEASE);
            if (readonly) {
                mprotect(reinterpret_cast<void*>(page), pageSize, protection);
            }
            return true;
        }

        /**
         * @brief Resolves an ELF dynamic table pointer against its load base.
         * @param base ELF object load address.
         * @param value Raw dynamic entry pointer.
         * @return Resolved process address.
         */
        [[nodiscard]] uintptr_t dynamicPointer(uintptr_t base, uintptr_t value) {
            return value < base ? base + value : value;
        }

        /**
         * @brief Patches matching PLT or dynamic relocation entries.
         * @param base ELF object load address.
         * @param relocations Relocation array.
         * @param count Number of relocation entries.
         * @param symbols ELF dynamic symbol table.
         * @param strings ELF dynamic string table.
         * @param context Hook specifications and patch count.
         */
        void patchRelocations(uintptr_t base, const ElfW(Rela) * relocations, size_t count, const ElfW(Sym) * symbols, const char* strings, WalkContext& context) {
            for (size_t i = 0; i < count; ++i) {
                const auto& relocation = relocations[i];
                const auto  type       = ELF64_R_TYPE(relocation.r_info);
                if (type != R_X86_64_JUMP_SLOT && type != R_X86_64_GLOB_DAT) {
                    continue;
                }
                const char* symbol = strings + symbols[ELF64_R_SYM(relocation.r_info)].st_name;
                for (const HookSpec& hook : context.hooks) {
                    if (std::strcmp(symbol, hook.symbol) != 0) {
                        continue;
                    }
                    auto** slot = reinterpret_cast<void**>(base + relocation.r_offset);
                    if (__atomic_load_n(slot, __ATOMIC_ACQUIRE) != hook.replacement && writeSlot(slot, hook.replacement)) {
                        ++context.patched;
                    }
                    break;
                }
            }
        }

        /**
         * @brief Visits one loaded ELF object to replace imported hook symbols.
         * @param info Object loader metadata.
         * @param userData Hook walker context.
         * @return Zero to continue iterating ELF objects.
         */
        int visitObject(dl_phdr_info* info, size_t, void* userData) {
            if (info->dlpi_name && std::strstr(info->dlpi_name, "liblotus-apphook")) {
                return 0;
            }
            auto&           context  = *static_cast<WalkContext*>(userData);
            const uintptr_t base     = static_cast<uintptr_t>(info->dlpi_addr);
            const ElfW(Dyn)* dynamic = nullptr;
            for (size_t i = 0; i < info->dlpi_phnum; ++i) {
                if (info->dlpi_phdr[i].p_type == PT_DYNAMIC) {
                    dynamic = reinterpret_cast<const ElfW(Dyn)*>(base + info->dlpi_phdr[i].p_vaddr);
                    break;
                }
            }
            if (!dynamic) {
                return 0;
            }

            const ElfW(Sym)* symbols      = nullptr;
            const char* strings           = nullptr;
            const ElfW(Rela)* jumpRel     = nullptr;
            const ElfW(Rela)* relocations = nullptr;
            size_t jumpSize               = 0;
            size_t relocationSize         = 0;
            int    pltType                = DT_RELA;
            for (const ElfW(Dyn)* entry = dynamic; entry->d_tag != DT_NULL; ++entry) {
                switch (entry->d_tag) {
                    case DT_SYMTAB: symbols = reinterpret_cast<const ElfW(Sym)*>(dynamicPointer(base, entry->d_un.d_ptr)); break;
                    case DT_STRTAB: strings = reinterpret_cast<const char*>(dynamicPointer(base, entry->d_un.d_ptr)); break;
                    case DT_JMPREL: jumpRel = reinterpret_cast<const ElfW(Rela)*>(dynamicPointer(base, entry->d_un.d_ptr)); break;
                    case DT_PLTRELSZ: jumpSize = entry->d_un.d_val; break;
                    case DT_PLTREL: pltType = entry->d_un.d_val; break;
                    case DT_RELA: relocations = reinterpret_cast<const ElfW(Rela)*>(dynamicPointer(base, entry->d_un.d_ptr)); break;
                    case DT_RELASZ: relocationSize = entry->d_un.d_val; break;
                    default: break;
                }
            }
            if (!symbols || !strings) {
                return 0;
            }
            if (jumpRel && pltType == DT_RELA) {
                patchRelocations(base, jumpRel, jumpSize / sizeof(ElfW(Rela)), symbols, strings, context);
            }
            if (relocations) {
                patchRelocations(base, relocations, relocationSize / sizeof(ElfW(Rela)), symbols, strings, context);
            }
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
