/*
 * SPDX-FileCopyrightText: 2026-2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
package main

import "runtime/cgo"

// The C++ side reports "there is nothing here" by passing a zero handle:
// CGoObject::handle() returns 0 when empty, LotusEngine::macroTable() returns 0
// when no input method is configured, and NewDictionary/NewEngine return 0 when
// they fail. cgo.Handle(0).Value() and .Delete() both panic on such a handle,
// and a panic here takes the whole fcitx5 process down, so every handle coming
// from C has to be checked before it is used.

func engineFromHandle(h uintptr) (*FcitxBambooEngine, bool) {
	if h == 0 {
		return nil, false
	}
	engine, ok := cgo.Handle(h).Value().(*FcitxBambooEngine)
	return engine, ok
}

func macroTableFromHandle(h uintptr) (*MacroTable, bool) {
	if h == 0 {
		return nil, false
	}
	table, ok := cgo.Handle(h).Value().(*MacroTable)
	return table, ok
}

func dictionaryFromHandle(h uintptr) (*map[string]bool, bool) {
	if h == 0 {
		return nil, false
	}
	dict, ok := cgo.Handle(h).Value().(*map[string]bool)
	return dict, ok
}

func deleteHandle(h uintptr) {
	if h == 0 {
		return
	}
	cgo.Handle(h).Delete()
}
