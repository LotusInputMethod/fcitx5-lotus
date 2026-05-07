/*
 * SPDX-FileCopyrightText: 2026 fcitx5-lotus contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Abstract input engine: Unikey
 */
#ifndef FCITX5_LOTUS_INPUT_BACKEND_HPP
#define FCITX5_LOTUS_INPUT_BACKEND_HPP

#include <cstdint>
#include <memory>
#include <string>

namespace fcitx {

    class LotusEngine;

    /**
 * Per-context Vietnamese engine (Bamboo or Unikey implementation).
 */
    class LotusInputBackend {
      public:
        virtual ~LotusInputBackend() = default;

        virtual void recreateEngine(LotusEngine* engine)                                                             = 0;
        virtual void setOptions(LotusEngine* engine)                                                                 = 0;
        virtual void resetEngine()                                                                                   = 0;
        virtual void rebuildFromText(const char* utf8)                                                               = 0;
        virtual bool processKeyEventAndPull(uint32_t sym, uint32_t state, std::string* commit, std::string* preedit) = 0;
        virtual bool processKeyEvent(uint32_t sym, uint32_t state)                                                   = 0;
        virtual void pullCommitAndPreedit(std::string* commit, std::string* preedit)                                 = 0;
        virtual void pullCommit(std::string* out)                                                                    = 0;
        virtual void pullPreedit(std::string* out)                                                                   = 0;
        virtual void commitPreedit()                                                                                 = 0;
    };

    std::unique_ptr<LotusInputBackend> makeLotusInputBackend();

} // namespace fcitx

#endif
