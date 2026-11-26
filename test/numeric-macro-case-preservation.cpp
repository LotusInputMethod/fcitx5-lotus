// SPDX-License-Identifier: GPL-3.0-or-later
//
// Regression #370: a macro whose key is made of digits only used to lose the letter case of its
// value when auto-capitalization is enabled. Driven through the C++ engine (the Go C ABI is gone).
#include "key_processor.h"

#include "input_method.h"
#include "macro_table.h"
#include "test-utils.h"
#include "utf8.h"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>

namespace {

    using lotus::KeyProcessor;
    using lotus::MacroTable;
    using lotus::Options;

    /** @brief Builds a processor with the option set the regression was reported with. */
    KeyProcessor newProcessor(const std::shared_ptr<const MacroTable>& macroTable) {
        KeyProcessor processor(lotus::cachedInputMethod(lotus::InputMethodId::Telex), true);
        Options      options;
        options.autoNonVnRestore = true;
        options.ddFreeStyle      = true;
        options.macroEnabled     = true;
        // Keep auto-capitalization enabled: regression #370 altered the expansion's
        // original letter case when the macro key consisted solely of digits.
        options.autoCapitalizeMacro = true;
        options.spellCheckWithDicts = true;
        options.outputCharset       = lotus::Charset::Unicode;
        options.modernStyle         = false;
        options.freeMarking         = false;
        options.w2u                 = lotus::W2uMode::Disabled;
        options.bracketTransform    = lotus::BracketTransformMode::Disabled;
        options.timeFormat          = "%H:%M";
        options.dateFormat          = "%d/%m/%Y";
        processor.setOptions(options);
        processor.setMacroTable(macroTable);
        return processor;
    }

} // namespace

int run_numeric_macro_case() {
    auto macroTable = std::make_shared<MacroTable>();
    macroTable->set("123", "Mixed Case");

    KeyProcessor processor = newProcessor(macroTable);

    // Tab terminates the numeric key sequence and triggers macro expansion.
    bool accepted = true;
    for (const uint32_t key : {static_cast<uint32_t>('1'), static_cast<uint32_t>('2'), static_cast<uint32_t>('3'), 0xFF09U}) {
        accepted = processor.processKey(key, {}) && accepted;
    }

    const std::string actual = lotus::utf8::encode(processor.takeCommit());
    if (!accepted || actual != "Mixed Case") {
        reportFailure("numeric macro expansion", "accepted=1, commit=[Mixed Case]", std::string("accepted=") + (accepted ? "1" : "0") + ", commit=[" + actual + "]",
                      "a numeric macro key must not lose the letter case of its expansion (regression #370)");
        return 1;
    }
    return checkExitCode("numeric macro case preservation: all checks passed");
}
