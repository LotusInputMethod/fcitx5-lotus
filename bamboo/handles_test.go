/*
 * SPDX-FileCopyrightText: 2026-2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */
package main

import (
	"testing"
)

// Zero is the C++ side's "nothing here" (empty CGoObject, no macro table,
// failed New*); the helpers must reject it without panicking.
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
	handle := newHandle(engine)
	defer deleteHandle(handle)

	got, ok := engineFromHandle(handle)
	if !ok || got != engine {
		t.Errorf("engineFromHandle(valid) got [%v,%v] expected [%v,true]", got, ok, engine)
	}
	// A handle holding another type must be reported as a miss, not returned.
	if table, ok := macroTableFromHandle(handle); ok || table != nil {
		t.Errorf("macroTableFromHandle(engine handle) got [%v,%v] expected [nil,false]", table, ok)
	}
	// Delete must remove the object...
	deleteHandle(handle)
	if _, ok := engineFromHandle(handle); ok {
		t.Errorf("engineFromHandle(deleted) got ok=true expected false")
	}
	// ...and deleting twice must be safe.
	deleteHandle(handle)
}

func TestHandleTableIdsAreUniqueAndNonZero(t *testing.T) {
	tbl := newHandleTable()
	first := tbl.next
	h1 := newHandle(&MacroTable{mTable: map[string]string{}})
	h2 := newHandle(&MacroTable{mTable: map[string]string{}})
	if h1 == 0 || h2 == 0 {
		t.Fatalf("newHandle returned zero id (h1=%d h2=%d)", h1, h2)
	}
	if h1 == h2 {
		t.Errorf("newHandle returned duplicate id %d", h1)
	}
	if first != 1 {
		t.Errorf("fresh table should start at id 1, got %d", first)
	}
	// The global table is shared; ids never collide with a fresh local table.
	if h1 == table.next || h2 == table.next {
		t.Errorf("local table ids overlap the global sequence")
	}
}
