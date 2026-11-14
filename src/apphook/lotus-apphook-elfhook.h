/*
 * SPDX-FileCopyrightText: 2026 Kazoku <k4zoku@disr.it>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

/**
 * @file lotus-apphook-elfhook.h
 * @brief Declares the in-process ELF hook installer.
 */

#ifndef _SRC_LOTUS_APPHOOK_ELFHOOK_H_
#define _SRC_LOTUS_APPHOOK_ELFHOOK_H_

#include <cstddef>
#include <span>

namespace lotus::apphook {

    /**
     * @brief ELF symbol and replacement address for an in-process relocation patch.
     */
    struct HookSpec {
        const char* symbol;
        void*       replacement;
    };

    /**
     * @brief Replaces matching imported ELF function pointers in the current process.
     * @param hooks Symbol names and their hook addresses.
     * @return Number of relocation slots patched.
     */
    [[nodiscard]] int installHooks(std::span<const HookSpec> hooks);

} // namespace lotus::apphook

#endif // _SRC_LOTUS_APPHOOK_ELFHOOK_H_
