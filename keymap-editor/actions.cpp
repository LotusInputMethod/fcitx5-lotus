#include "actions.h"
#include "inputproc.h"
#include <fcitx-utils/i18n.h>
#include <fcitx-utils/macros.h>
#include <unordered_map>

namespace fcitx::unikey {

const std::vector<std::tuple<std::string, int, int>> &actionNames() {
    static const auto names = []() {
        std::vector<std::tuple<std::string, int, int>> result;
        const std::tuple<std::string, int, int> UkEvNameList[] = {
            {N_("Remove existing tone"), vneTone0, AC_Tone},
            {N_("Tone ' (acute)"), vneTone1, AC_Tone},
            {N_("Tone ` (grave)"), vneTone2, AC_Tone},
            {N_("Tone ◌̉ (hook above)"), vneTone3, AC_Tone},
            {N_("Tone ~ (tilde)"), vneTone4, AC_Tone},
            {N_("Tone . (dot below)"), vneTone5, AC_Tone},
            {N_("Escape key"), vneEscChar, AC_Tone},
            {N_("Circumflex for all applicable characters"), vneRoofAll,
             AC_ChrComp},
            {N_("Circumflex: A becomes A^"), vneRoof_a, AC_ChrComp},
            {N_("Circumflex: E becomes E^"), vneRoof_e, AC_ChrComp},
            {N_("Circumflex: O becomes O^"), vneRoof_o, AC_ChrComp},
            {N_("Horn-Breve: U, O, A, become U+, O+, A("), vneHookAll,
             AC_ChrComp},
            {N_("Horn: U, O become U+, O+"), vneHook_uo, AC_ChrComp},
            {N_("Horn: U becomes U+"), vneHook_u, AC_ChrComp},
            {N_("Horn: O becomes O+"), vneHook_o, AC_ChrComp},
            {N_("Breve: A becomes A("), vneBowl, AC_ChrComp},
            {N_("Stroke: D becomes -D"), vneDd, AC_ChrComp},
            {N_("Horn-Breve: U, O, A, become U+, O+, A(, or create U+"),
             vne_telex_w, AC_ChrComp},
            {N_("D with stroke [-D]"), vneCount + vnl_DD, AC_Viet},
            {N_("d with stroke [-d]"), vneCount + vnl_dd, AC_Viet},
            {N_("A with circumflex [A^]"), vneCount + vnl_Ar, AC_Viet},
            {N_("a with circumflex [a^]"), vneCount + vnl_ar, AC_Viet},
            {N_("A with breve [A(]"), vneCount + vnl_Ab, AC_Viet},
            {N_("a with breve [a(]"), vneCount + vnl_ab, AC_Viet},
            {N_("E with circumflex [E^]"), vneCount + vnl_Er, AC_Viet},
            {N_("e with circumflex [e^]"), vneCount + vnl_er, AC_Viet},
            {N_("O with circumflex [O^]"), vneCount + vnl_Or, AC_Viet},
            {N_("o with circumflex [o^]"), vneCount + vnl_or, AC_Viet},
            {N_("O with horn [O+]"), vneCount + vnl_Oh, AC_Viet},
            {N_("o with horn [o+]"), vneCount + vnl_oh, AC_Viet},
            {N_("U with horn [U+]"), vneCount + vnl_Uh, AC_Viet},
            {N_("u with horn [u+]"), vneCount + vnl_uh, AC_Viet}};
        result.reserve(FCITX_ARRAY_SIZE(UkEvNameList));
        for (const auto &item : UkEvNameList) {
            result.push_back(item);
        }
        return result;
    }();
    return names;
}

static const std::string emptyString;
const std::string &actionName(int action) {
    static const auto actionToNameMap = []() {
        std::unordered_map<int, std::string> result;
        for (const auto &[name, action, _] : actionNames()) {
            result[action] = name;
        }
        return result;
    }();

    if (auto iter = actionToNameMap.find(action);
        iter != actionToNameMap.end()) {
        return iter->second;
    }
    return emptyString;
}

int actionCategory(int action) {
    switch (action) {
    case vneTone0:
    case vneTone1:
    case vneTone2:
    case vneTone3:
    case vneTone4:
    case vneTone5:
    case vneEscChar:
        return AC_Tone;
    case vneRoofAll:
    case vneRoof_a:
    case vneRoof_e:
    case vneRoof_o:
    case vneHookAll:
    case vneHook_uo:
    case vneHook_u:
    case vneHook_o:
    case vneBowl:
    case vneDd:
    case vne_telex_w:
        return AC_ChrComp;
    case vneCount + vnl_DD:
    case vneCount + vnl_dd:
    case vneCount + vnl_Ar:
    case vneCount + vnl_ar:
    case vneCount + vnl_Ab:
    case vneCount + vnl_ab:
    case vneCount + vnl_Er:
    case vneCount + vnl_er:
    case vneCount + vnl_Or:
    case vneCount + vnl_or:
    case vneCount + vnl_Oh:
    case vneCount + vnl_oh:
    case vneCount + vnl_Uh:
    case vneCount + vnl_uh:
        return AC_Viet;
    }
    return -1;
}

} // namespace fcitx::unikey
