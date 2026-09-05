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

// Set stores a macro entry, normalizing the key to lower case so that it can
// be found again by Get(), which looks the key up case-insensitively.
func (e *MacroTable) Set(key, value string) {
	if e == nil || len(key) == 0 {
		return
	}
	if e.mTable == nil {
		e.mTable = map[string]string{}
	}
	e.mTable[strings.ToLower(key)] = value
}

func (e *MacroTable) Get(key string) (string,bool) {
	if len(key) == 0 || e.Empty() {
		return "", false
	}
	val, ok := e.mTable[strings.ToLower(key)]
	return val, ok
}

//Deprecated: Use Get() instead or wrap with Get() function
func (e *MacroTable) HasKey(key string) bool {
	return e.mTable[strings.ToLower(key)] != ""
}

//Deprecated: Use Get() instead or wrap with Get() function
func (e *MacroTable) GetText(key string) string {
	return e.mTable[strings.ToLower(key)]
}
