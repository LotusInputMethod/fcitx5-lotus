/*
 * SPDX-FileCopyrightText: 2000-2005 Pham Kim Long <unikey@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "usrkeymap.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcitx-utils/fdstreambuf.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/misc.h>
#include <fcitx-utils/stringutils.h>
#include <istream>
#include <string>
#include <vector>

namespace {

constexpr char OPT_COMMENT_CHAR = ';';

struct UkEventLabelPair {
    char label[32];
    int ev;
};

const char *UkKeyMapHeader = "; This is UniKey user-defined key mapping file, "
                             "generated from UniKey (Fcitx 5)\n\n";

constexpr UkEventLabelPair UkEvLabelList[] = {
    {"Tone0", vneTone0},       {"Tone1", vneTone1},
    {"Tone2", vneTone2},       {"Tone3", vneTone3},
    {"Tone4", vneTone4},       {"Tone5", vneTone5},
    {"Roof-All", vneRoofAll},  {"Roof-A", vneRoof_a},
    {"Roof-E", vneRoof_e},     {"Roof-O", vneRoof_o},
    {"Hook-Bowl", vneHookAll}, {"Hook-UO", vneHook_uo},
    {"Hook-U", vneHook_u},     {"Hook-O", vneHook_o},
    {"Bowl", vneBowl},         {"D-Mark", vneDd},
    {"Telex-W", vne_telex_w},  {"Escape", vneEscChar},
    {"DD", vneCount + vnl_DD}, {"dd", vneCount + vnl_dd},
    {"A^", vneCount + vnl_Ar}, {"a^", vneCount + vnl_ar},
    {"A(", vneCount + vnl_Ab}, {"a(", vneCount + vnl_ab},
    {"E^", vneCount + vnl_Er}, {"e^", vneCount + vnl_er},
    {"O^", vneCount + vnl_Or}, {"o^", vneCount + vnl_or},
    {"O+", vneCount + vnl_Oh}, {"o+", vneCount + vnl_oh},
    {"U+", vneCount + vnl_Uh}, {"u+", vneCount + vnl_uh}};

constexpr auto UkEvLabelCount = FCITX_ARRAY_SIZE(UkEvLabelList);

//-------------------------------------------
void initKeyMap(int keyMap[256]) {
    unsigned int c;
    for (c = 0; c < 256; c++)
        keyMap[c] = vneNormal;
}

int getLabelIndex(int event) {
    for (size_t i = 0; i < UkEvLabelCount; i++) {
        if (UkEvLabelList[i].ev == event)
            return i;
    }
    return -1;
}

} // namespace

//--------------------------------------------------
static bool parseNameValue(std::string_view line, std::string_view *name,
                           std::string_view *value) {
    if (line.empty()) {
        return false;
    }

    // get rid of comment
    auto pos = line.find(OPT_COMMENT_CHAR);
    if (pos != std::string::npos) {
        line = line.substr(0, pos);
    }
    if (line.empty()) {
        return false;
    }

    pos = line.find('=');
    if (pos == std::string::npos) {
        return false;
    }
    auto k = fcitx::stringutils::trimView(line.substr(0, pos));
    auto v = fcitx::stringutils::trimView(line.substr(pos + 1));
    if (k.empty() || v.empty()) {
        return false;
    }

    *name = k;
    *value = v;
    return true;
}

//-----------------------------------------------------
DllExport void UkLoadKeyMap(int fd, int keyMap[256]) {
    std::vector<UkKeyMapping> orderMap = UkLoadKeyOrderMap(fd);
    initKeyMap(keyMap);
    for (const auto &item : orderMap) {
        keyMap[item.key] = item.action;
        if (item.action < vneCount) {
            keyMap[tolower(item.key)] = item.action;
        }
    }
}

//------------------------------------------------------------------
DllExport std::vector<UkKeyMapping> UkLoadKeyOrderMap(int fd) {
    size_t lineCount = 0;
    int keyMap[256];

    initKeyMap(keyMap);

    std::vector<UkKeyMapping> pMap;
    fcitx::IFDStreamBuf buf(fd);
    std::istream in(&buf);
    std::string line;
    while (std::getline(in, line)) {
        lineCount++;
        auto text = fcitx::stringutils::trimView(line);
        if (text.empty()) {
            continue;
        }
        std::string_view name, value;
        if (parseNameValue(text, &name, &value)) {
            if (name.size() != 1) {
                FCITX_ERROR() << "Error in user key layout, line " << lineCount
                              << ": key name is not a single character";
                continue;
            }
            size_t i = 0;
            for (; i < UkEvLabelCount; i++) {
                if (UkEvLabelList[i].label == value) {
                    break;
                }
            }
            if (i == UkEvLabelCount) {
                FCITX_ERROR() << "Error in user key layout, line " << lineCount
                              << ": command not found";
                continue;
            }

            auto c = static_cast<uint8_t>(name[0]);
            if (keyMap[c] != vneNormal) {
                // already assigned, don't accept this map
                break;
            }
            // cout << "key: " << c << " value: " <<
            // UkEvLabelList[i].ev << endl; //DEBUG
            keyMap[c] = UkEvLabelList[i].ev;
            UkKeyMapping newPair;
            newPair.action = UkEvLabelList[i].ev;
            if (keyMap[c] < vneCount) {
                newPair.key = toupper(c);
                keyMap[toupper(c)] = UkEvLabelList[i].ev;
            } else {
                newPair.key = c;
            }
            pMap.push_back(newPair);
        }
    }
    return pMap;
}

DllExport void UkStoreKeyOrderMap(FILE *f,
                                  const std::vector<UkKeyMapping> &pMap) {
    int labelIndex;

    fputs(UkKeyMapHeader, f);
    for (const auto &item : pMap) {
        labelIndex = getLabelIndex(item.action);
        if (labelIndex != -1) {
            fprintf(f, "%c = %s\n", item.key, UkEvLabelList[labelIndex].label);
        }
    }
}
