/*
 * SPDX-FileCopyrightText: 2026 fcitx5-lotus contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef LOTUS_ENGINE_UNIKEY

#include "lotus-input-backend.hpp"
#include "lotus-config.h"
#include "lotus-engine.h"
#include "lotus.h"

#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>

namespace fcitx {

namespace {

class LotusBambooInputBackend final : public LotusInputBackend {
public:
    void recreateEngine(LotusEngine* engine) override {
        engine_.reset();
        if (engine->config().inputMethod.value() == "Custom") {
            const auto&        keymaps = *engine->customKeymap().customKeymap;
            std::vector<char*> charArray;
            charArray.reserve((keymaps.size() * 2) + 1);
            for (const auto& keymap : keymaps) {
                charArray.push_back(const_cast<char*>(keymap.key->data()));   // NOLINT
                charArray.push_back(const_cast<char*>(keymap.value->data())); // NOLINT
            }
            charArray.push_back(nullptr);
            engine_.reset(NewCustomEngine(charArray.data(), engine->dictionary(), engine->macroTable()));
        } else {
            engine_.reset(NewEngine(engine->config().inputMethod->data(), engine->dictionary(), engine->macroTable()));
        }
    }

    void setOptions(LotusEngine* engine) override {
        if (!engine_)
            return;
        FcitxBambooEngineOption option = {
            .autoNonVnRestore    = *engine->config().autoNonVnRestore,
            .ddFreeStyle         = *engine->config().ddFreeStyle,
            .macroEnabled        = *engine->config().enableMacro,
            .autoCapitalizeMacro = *engine->config().capitalizeMacro,
            .spellCheckWithDicts = *engine->config().spellCheck,
            .outputCharset       = engine->config().outputCharset->data(),
            .modernStyle         = *engine->config().modernStyle,
            .freeMarking         = *engine->config().freeMarking,
            .w2u                 = *engine->config().w2u,
            .timeFormat          = engine->config().timeFormat->data(),
            .dateFormat          = engine->config().dateFormat->data(),
        };
        EngineSetOption(engine_.handle(), &option);
    }

    void resetEngine() override {
        if (engine_)
            ResetEngine(engine_.handle());
    }

    void rebuildFromText(const char* utf8) override {
        if (engine_)
            EngineRebuildFromText(engine_.handle(), utf8);
    }

    bool processKeyEventAndPull(uint32_t sym, uint32_t state, std::string* commit, std::string* preedit) override {
        if (!engine_)
            return false;
        char *cr = nullptr, *pr = nullptr;
        bool  ok = EngineProcessKeyEventAndPull(engine_.handle(), sym, state, &cr, &pr);
        if (commit) {
            commit->assign(cr ? cr : "");
        }
        if (preedit) {
            preedit->assign(pr ? pr : "");
        }
        std::free(cr); // NOLINT
        std::free(pr); // NOLINT
        return ok;
    }

    bool processKeyEvent(uint32_t sym, uint32_t state) override {
        if (!engine_)
            return false;
        return EngineProcessKeyEvent(engine_.handle(), sym, state);
    }

    void pullCommitAndPreedit(std::string* commit, std::string* preedit) override {
        if (!engine_)
            return;
        char *cp = nullptr, *pp = nullptr;
        EnginePullCommitAndPreedit(engine_.handle(), &cp, &pp);
        if (commit)
            commit->assign(cp ? cp : "");
        if (preedit)
            preedit->assign(pp ? pp : "");
        std::free(cp); // NOLINT
        std::free(pp); // NOLINT
    }

    void pullCommit(std::string* out) override {
        if (!engine_ || !out)
            return;
        char* p = EnginePullCommit(engine_.handle());
        out->assign(p ? p : "");
        std::free(p); // NOLINT
    }

    void pullPreedit(std::string* out) override {
        if (!engine_ || !out)
            return;
        char* p = EnginePullPreedit(engine_.handle());
        out->assign(p ? p : "");
        std::free(p); // NOLINT
    }

    void commitPreedit() override {
        if (engine_)
            EngineCommitPreedit(engine_.handle());
    }

private:
    CGoObject engine_;
};

} // namespace

std::unique_ptr<LotusInputBackend> makeLotusInputBackend() {
    return std::make_unique<LotusBambooInputBackend>();
}

} // namespace fcitx

#endif // !LOTUS_ENGINE_UNIKEY
