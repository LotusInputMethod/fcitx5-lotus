/*
 * SPDX-FileCopyrightText: 2022-2022 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
package main

import "strings"

type MacroTable struct {
	mTable map[string]string
}

func (e *MacroTable) Empty() bool {
	return e == nil || len(e.mTable) == 0
}

func (e *MacroTable) Get(key string) (string, bool) {
	if len(key) == 0 || e.Empty() {
		return "", false
	}
	val, ok := e.mTable[strings.ToLower(key)]
	return val, ok
}
