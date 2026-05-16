/*
 * SPDX-FileCopyrightText: 2012-2018 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */
#ifndef _KEYMAP_EDITOR_ACTIONS_H_
#define _KEYMAP_EDITOR_ACTIONS_H_

#include <string>
#include <tuple>
#include <vector>

namespace fcitx::unikey {

enum ActionCategory { AC_Tone, AC_ChrComp, AC_Viet };

const std::vector<std::tuple<std::string, int, int>> &actionNames();
const std::string &actionName(int action);
int actionCategory(int action);

} // namespace fcitx::unikey

#endif
