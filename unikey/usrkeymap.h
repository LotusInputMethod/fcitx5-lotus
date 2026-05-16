/*
 * SPDX-FileCopyrightText: 2000-2005 Pham Kim Long <unikey@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef __UNIKEY_USER_KEY_MAP_H
#define __UNIKEY_USER_KEY_MAP_H

#include "inputproc.h"
#include <cstdio>
#include <vector>

DllInterface void UkLoadKeyMap(int fd, int keyMap[256]);
DllInterface std::vector<UkKeyMapping> UkLoadKeyOrderMap(int fd);
DllInterface void UkStoreKeyOrderMap(FILE *f,
                                     const std::vector<UkKeyMapping> &pMap);

#endif
