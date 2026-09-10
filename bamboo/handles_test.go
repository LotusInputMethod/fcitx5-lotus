/*
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
package main

import (
	"runtime/cgo"
	"testing"
)

// A zero handle is how the C++ side reports "there is nothing here":
// CGoObject::handle() returns 0 when empty, LotusEngine::macroTable() returns 0
// when no input method is configured, and NewDictionary/NewEngine return 0 on
// failure. cgo.Handle(0).Value() panics, so the helpers must reject 0 before
// touching the handle.
func TestHandleHelpersRejectZeroHandle(t *testing.T) {
	if engine, ok := engineFromHandle(0); ok || engine != nil {
		t.Errorf("engineFromHandle(0) got [%v,%v] expected [nil,false]", engine, ok)
	}
	if table, ok := macroTableFromHandle(0); ok || table != nil {
		t.Errorf("macroTableFromHandle(0) got [%v,%v] expected [nil,false]", table, ok)
	}
	if dict, ok := dictionaryFromHandle(0); ok || dict != nil {
		t.Errorf("dictionaryFromHandle(0) got [%v,%v] expected [nil,false]", dict, ok)
	}
	deleteHandle(0) // must not panic
}

func TestHandleHelpersRoundTrip(t *testing.T) {
	engine := &FcitxBambooEngine{}
	handle := uintptr(cgo.NewHandle(engine))
	defer deleteHandle(handle)

	got, ok := engineFromHandle(handle)
	if !ok || got != engine {
		t.Errorf("engineFromHandle(valid) got [%v,%v] expected [%v,true]", got, ok, engine)
	}
	// A handle holding another type must be reported as a miss, not returned.
	if table, ok := macroTableFromHandle(handle); ok || table != nil {
		t.Errorf("macroTableFromHandle(engine handle) got [%v,%v] expected [nil,false]", table, ok)
	}
}
