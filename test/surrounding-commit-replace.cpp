// SPDX-License-Identifier: GPL-3.0-or-later
//
// In Surrounding Text mode, commitOrReplace() must delete exactly the
// deletedPart it was given: the offset and size of deleteSurroundingText()
// follow diffPart.deletedPart.size(). A hardcoded deleteSurroundingText(-1, 1)
// would leave all but the last character of a multi-character deletedPart in
// the application, corrupting the replaced word. Coverage:
//   - a 3-character deletedPart through commitOrReplace() itself,
//   - an empty deletedPart (no delete request) and an empty addedPart (no commit),
//   - an astral-plane deletedPart counted in code points,
//   - the double-space and double-hyphen shortcuts driven by real key events,
//     which must keep deleting exactly one character before the cursor.
#include "lotus-engine.h"
#include "lotus-state.h"
#include "test-input-context.h"

#include <iostream>
#include <string>
#include <string_view>

namespace fcitx {
    // Test seam: friend of LotusState, declared in lotus-state.h.
    class SurroundingCommitReplacePeer {
      public:
        static void replace(LotusState& state, DiffPart diffPart) {
            state.commitOrReplace(diffPart, "test: commitOrReplace");
        }
    };
} // namespace fcitx

namespace {

    void reportFailure(const std::string& step, const std::string& expected, const std::string& actual, const std::string& meaning) {
        std::cerr << "Step: " << step << '\n';
        std::cerr << "Expected: " << expected << '\n';
        std::cerr << "Actual: " << actual << '\n';
        std::cerr << "Meaning: " << meaning << '\n';
    }

    std::string describeDeletes(const TestInputContext& context) {
        std::string actual = "deletes=";
        for (const auto& [offset, size] : context.deletes()) {
            actual += "(" + std::to_string(offset) + "," + std::to_string(size) + ")";
        }
        return actual;
    }

    std::string describeCommits(const TestInputContext& context) {
        std::string actual = "commits=";
        for (const auto& commit : context.commits()) {
            actual += "['" + commit + "']";
        }
        return actual;
    }

    bool expectDelete(const TestInputContext& context, const char* step, int offset, unsigned int size) {
        if (context.deletes().size() != 1 || context.deletes()[0].first != offset || context.deletes()[0].second != size) {
            reportFailure(step, "deletes=(" + std::to_string(offset) + "," + std::to_string(size) + ")", describeDeletes(context),
                          "commitOrReplace() must delete exactly the deletedPart: offset = -deletedPart.size(), size = deletedPart.size()");
            return false;
        }
        return true;
    }

    bool sendKey(fcitx::LotusEngine& engine, const fcitx::InputMethodEntry& entry, TestInputContext& context, fcitx::KeySym symbol, bool requireAccepted) {
        fcitx::KeyEvent event(&context, fcitx::Key(symbol), false);
        engine.keyEvent(entry, event);
        if (event.accepted() != requireAccepted) {
            reportFailure("process key " + std::to_string(symbol), "accepted=" + std::to_string(static_cast<int>(requireAccepted)),
                          "accepted=" + std::to_string(static_cast<int>(event.accepted())), "the shortcut handling accepted or rejected the key unexpectedly");
            return false;
        }
        return true;
    }

} // namespace

int main() {
    configureTestPaths("fcitx5-lotus-surrounding-commit-replace");
    TestInstance       testInstance;
    fcitx::LotusEngine engine(&testInstance.instance);
    fcitx::RawConfig   config;
    config.setValueByPath("Mode", "Surrounding Text");
    config.setValueByPath("InputMethod", "Telex");
    config.setValueByPath("DoubleSpaceToPeriod", "True");
    config.setValueByPath("DoubleHyphenToEmDash", "True");
    engine.setConfig(config);
    if (engine.config().mode.value() != fcitx::LotusMode::SurroundingText || engine.config().inputMethod.value() != fcitx::InputMethodId::Telex ||
        !engine.config().doubleSpaceToPeriod.value() || !engine.config().doubleHyphenToEmDash.value()) {
        reportFailure("configure Surrounding Text/Telex and the replacement shortcuts", "mode=SurroundingText, telex, shortcuts on",
                      "configured mode, input method or shortcut differs", "the raw Fcitx configuration values did not serialize to the tested behavior");
        return 1;
    }
    fcitx::InputMethodEntry entry("lotus", "Lotus", "vi", "lotus");

    // Each scenario gets its own focused context so recorded requests stay isolated.
    const auto newContext = [&] {
        auto context = std::make_unique<TestInputContext>(&testInstance.instance);
        context->setCapabilityFlags(fcitx::CapabilityFlag::SurroundingText);
        context->focusIn();
        fcitx::InputContextEvent focus(context.get(), fcitx::EventType::InputContextFocusIn);
        engine.activate(entry, focus);
        if (currentMode() != fcitx::LotusMode::SurroundingText) {
            reportFailure("activate resolves Surrounding Text", "currentMode()=SurroundingText", "currentMode() differs",
                          "commitOrReplace() routes on the active mode, so the test needs Surrounding Text active");
            context = nullptr;
        }
        return context;
    };

    // A multi-character replacement: "abc" -> "xyz" must delete the whole
    // three-character deletedPart preceding the cursor, not just one character.
    {
        auto context = newContext();
        if (context == nullptr) {
            return 1;
        }
        fcitx::LotusState state(&engine, context.get());
        fcitx::SurroundingCommitReplacePeer::replace(state, {.deletedPart = U"abc", .addedPart = U"xyz"});
        if (!expectDelete(*context, "delete the whole 3-character deletedPart", -3, 3U)) {
            return 1;
        }
        if (context->commits().size() != 1 || context->commits()[0] != "xyz") {
            reportFailure("commit the addedPart of the 3-character replacement", "commits=[xyz]", describeCommits(*context), "the replacement text must be committed exactly once");
            return 1;
        }
    }

    // An empty deletedPart must not ask the application to delete anything.
    {
        auto context = newContext();
        if (context == nullptr) {
            return 1;
        }
        fcitx::LotusState state(&engine, context.get());
        fcitx::SurroundingCommitReplacePeer::replace(state, {.deletedPart = U"", .addedPart = U"xyz"});
        if (!context->deletes().empty()) {
            reportFailure("no delete request for an empty deletedPart", "deletes=[]", describeDeletes(*context),
                          "an empty deletedPart has nothing to remove, so no deleteSurroundingText() request may be sent");
            return 1;
        }
        if (context->commits().size() != 1 || context->commits()[0] != "xyz") {
            reportFailure("commit the addedPart despite the empty deletedPart", "commits=[xyz]", describeCommits(*context),
                          "the replacement text must still be committed exactly once");
            return 1;
        }
    }

    // An empty addedPart must delete the deletedPart but commit nothing.
    {
        auto context = newContext();
        if (context == nullptr) {
            return 1;
        }
        fcitx::LotusState state(&engine, context.get());
        fcitx::SurroundingCommitReplacePeer::replace(state, {.deletedPart = U"ab", .addedPart = U""});
        if (!expectDelete(*context, "delete the 2-character deletedPart of an empty replacement", -2, 2U)) {
            return 1;
        }
        if (!context->commits().empty()) {
            reportFailure("no commit for an empty addedPart", "commits=[]", describeCommits(*context), "an empty addedPart has nothing to commit, so no commitString may be sent");
            return 1;
        }
    }

    // Length is counted in Unicode code points: a single astral-plane emoji is one.
    {
        auto context = newContext();
        if (context == nullptr) {
            return 1;
        }
        fcitx::LotusState state(&engine, context.get());
        fcitx::SurroundingCommitReplacePeer::replace(state, {.deletedPart = U"😀", .addedPart = U"😄"});
        if (!expectDelete(*context, "delete one astral-plane code point", -1, 1U)) {
            return 1;
        }
        if (context->commits().size() != 1 || context->commits()[0] != "😄") {
            reportFailure("commit the emoji replacement", "commits=[😄]", describeCommits(*context), "the replacement text must be committed exactly once");
            return 1;
        }
    }

    // The double-space shortcut through real key events: typing a second space
    // replaces exactly the previous space with ". ".
    {
        auto context = newContext();
        if (context == nullptr) {
            return 1;
        }
        context->surroundingText().setText("  ", 2, 2);
        context->updateSurroundingText();
        if (!context->surroundingText().isValid()) {
            reportFailure("set valid surrounding text for double space", "surroundingText().isValid()", "invalid surrounding text",
                          "the shortcut needs a valid cursor/anchor position to define the replacement word");
            return 1;
        }
        // The first space only arms the shortcut; its own acceptance depends on
        // how the surrounding-text mode handles a space, so record the counts
        // before the second space and then require exactly one more delete and
        // one more commit, both from the replacement itself.
        fcitx::KeyEvent first(context.get(), fcitx::Key(FcitxKey_space), false);
        engine.keyEvent(entry, first);
        const auto deletesBefore = context->deletes().size();
        const auto commitsBefore = context->commits().size();
        if (!sendKey(engine, entry, *context, FcitxKey_space, true)) {
            return 1;
        }
        if (context->deletes().size() != deletesBefore + 1 || context->deletes().back().first != -1 || context->deletes().back().second != 1U) {
            reportFailure("double space deletes exactly the previous space", "exactly one new delete (-1,1)", describeDeletes(*context),
                          "the shortcut must delete exactly one character before the cursor and nothing else");
            return 1;
        }
        if (context->commits().size() != commitsBefore + 1 || context->commits().back() != ". ") {
            reportFailure("double space commits the period replacement", "exactly one new commit '. '", describeCommits(*context),
                          "the shortcut must commit '. ' exactly once and nothing else");
            return 1;
        }
    }

    // The double-hyphen shortcut through real key events: typing a second
    // hyphen replaces exactly the previous hyphen with an em dash.
    {
        auto context = newContext();
        if (context == nullptr) {
            return 1;
        }
        context->surroundingText().setText(" -", 2, 2);
        context->updateSurroundingText();
        if (!context->surroundingText().isValid()) {
            reportFailure("set valid surrounding text for double hyphen", "surroundingText().isValid()", "invalid surrounding text",
                          "the shortcut needs a valid cursor/anchor position to define the replacement word");
            return 1;
        }
        // As with double space, the first hyphen only arms the shortcut; require
        // exactly one more delete and one more commit from the second hyphen.
        fcitx::KeyEvent first(context.get(), fcitx::Key(FcitxKey_minus), false);
        engine.keyEvent(entry, first);
        const auto deletesBefore = context->deletes().size();
        const auto commitsBefore = context->commits().size();
        if (!sendKey(engine, entry, *context, FcitxKey_minus, true)) {
            return 1;
        }
        if (context->deletes().size() != deletesBefore + 1 || context->deletes().back().first != -1 || context->deletes().back().second != 1U) {
            reportFailure("double hyphen deletes exactly the previous hyphen", "exactly one new delete (-1,1)", describeDeletes(*context),
                          "the shortcut must delete exactly one character before the cursor and nothing else");
            return 1;
        }
        if (context->commits().size() != commitsBefore + 1 || context->commits().back() != "—") {
            reportFailure("double hyphen commits the em dash", "exactly one new commit '—'", describeCommits(*context),
                          "the shortcut must commit the em dash exactly once and nothing else");
            return 1;
        }
    }

    return 0;
}
