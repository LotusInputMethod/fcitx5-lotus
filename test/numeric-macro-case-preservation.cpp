// SPDX-License-Identifier: GPL-3.0-or-later
#include "bamboo-core.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>

#include <unistd.h>

namespace {

    void reportFailure(const std::string& step, const std::string& expected, const std::string& actual, const std::string& meaning) {
        std::cerr << "Step: " << step << '\n';
        std::cerr << "Expected: " << expected << '\n';
        std::cerr << "Actual: " << actual << '\n';
        std::cerr << "Meaning: " << meaning << '\n';
    }

    class Handle {
      public:
        explicit Handle(uintptr_t value = 0) : value_(value) {}
        ~Handle() {
            if (value_ != 0)
                DeleteObject(value_);
        }

        Handle(const Handle&)              = delete;
        Handle&   operator=(const Handle&) = delete;

        uintptr_t get() const {
            return value_;
        }

        explicit operator bool() const {
            return value_ != 0;
        }

      private:
        uintptr_t value_;
    };

    bool processKey(uintptr_t engine, uint32_t key, const char* name) {
        if (!EngineProcessKeyEvent(engine, key, 0)) {
            reportFailure("process key " + std::string(name), "key event accepted", "key event rejected", "the production Bamboo engine did not retain the numeric macro trigger");
            return false;
        }
        return true;
    }

} // namespace

int main() {
    char      dictionaryPath[] = "/tmp/fcitx5-lotus-numeric-macro-XXXXXX";
    const int dictionaryFd     = mkstemp(dictionaryPath);
    if (dictionaryFd < 0) {
        reportFailure("create empty temporary dictionary", "mkstemp succeeds", std::strerror(errno), "the test cannot construct a production dictionary handle");
        return 1;
    }

    Handle dictionary(NewDictionary(static_cast<uintptr_t>(dictionaryFd)));
    unlink(dictionaryPath);
    if (!dictionary) {
        reportFailure("create dictionary handle", "non-zero handle", "zero handle", "NewDictionary rejected the empty temporary dictionary");
        return 1;
    }

    char   key[]             = "123";
    char   value[]           = "Mixed Case";
    char*  macroDefinition[] = {key, value, nullptr};
    Handle macroTable(NewMacroTable(macroDefinition));
    if (!macroTable) {
        reportFailure("create macro table handle", "non-zero handle", "zero handle", "NewMacroTable rejected the numeric macro definition");
        return 1;
    }

    Handle engine(NewEngine("Telex", dictionary.get(), macroTable.get()));
    if (!engine) {
        reportFailure("create Telex engine handle", "non-zero handle", "zero handle", "NewEngine rejected valid production dependency handles");
        return 1;
    }

    FcitxBambooEngineOption option{};
    option.autoNonVnRestore = true;
    option.ddFreeStyle      = true;
    option.macroEnabled     = true;
    // Keep auto-capitalization enabled: regression #370 altered the expansion's
    // original letter case when the macro key consisted solely of digits.
    option.autoCapitalizeMacro = true;
    option.spellCheckWithDicts = true;
    option.outputCharset       = "Unicode";
    option.modernStyle         = false;
    option.freeMarking         = false;
    option.w2u                 = 0;
    option.bracketTransform    = 0;
    option.timeFormat          = "%H:%M";
    option.dateFormat          = "%d/%m/%Y";
    EngineSetOption(engine.get(), &option);

    // Tab terminates the numeric key sequence and triggers macro expansion in Bamboo core.
    if (!processKey(engine.get(), '1', "1") || !processKey(engine.get(), '2', "2") || !processKey(engine.get(), '3', "3") || !processKey(engine.get(), 0xff09, "Tab"))
        return 1;

    std::unique_ptr<char, decltype(&std::free)> commit(EnginePullCommit(engine.get()), &std::free);
    const std::string                           actual = commit ? commit.get() : "<null>";
    if (actual != "Mixed Case") {
        reportFailure("verify numeric macro case preservation", "Mixed Case", actual, "auto-capitalization must not change a numeric-only macro expansion's mixed case");
        return 1;
    }
    return 0;
}
