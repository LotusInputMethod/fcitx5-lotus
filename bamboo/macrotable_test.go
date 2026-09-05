/*
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
package main

import "testing"

func TestMacroTableEmpty(t *testing.T) {
	var nilTable *MacroTable
	if !nilTable.Empty() {
		t.Errorf("nil MacroTable: Empty() got [false] expected [true]")
	}
	if !(&MacroTable{}).Empty() {
		t.Errorf("empty MacroTable: Empty() got [false] expected [true]")
	}
	nonEmpty := &MacroTable{mTable: map[string]string{"vn": "Việt Nam"}}
	if nonEmpty.Empty() {
		t.Errorf("non-empty MacroTable: Empty() got [true] expected [false]")
	}
}

func TestMacroTableGet(t *testing.T) {
	table := &MacroTable{mTable: map[string]string{"vn": "Việt Nam"}}

	if val, ok := table.Get("VN"); !ok || val != "Việt Nam" {
		t.Errorf("Get(VN) got [%s,%v] expected [Việt Nam,true] (case-insensitive)", val, ok)
	}
	if _, ok := table.Get("vn"); !ok {
		t.Errorf("Get(vn) got [missing] expected [found]")
	}
	if _, ok := table.Get(""); ok {
		t.Errorf("Get(empty key) got [found] expected [missing]")
	}
	if _, ok := table.Get("xyz"); ok {
		t.Errorf("Get(xyz) got [found] expected [missing]")
	}
	if _, ok := (&MacroTable{}).Get("vn"); ok {
		t.Errorf("Get on empty table got [found] expected [missing]")
	}
}

func TestMacroTableDeprecatedHelpers(t *testing.T) {
	table := &MacroTable{mTable: map[string]string{"vn": "Việt Nam"}}

	if !table.HasKey("VN") {
		t.Errorf("HasKey(VN) got [false] expected [true]")
	}
	if table.HasKey("xyz") {
		t.Errorf("HasKey(xyz) got [true] expected [false]")
	}
	if got := table.GetText("VN"); got != "Việt Nam" {
		t.Errorf("GetText(VN) got [%s] expected [Việt Nam]", got)
	}
}

func TestMacroTableSetIsCaseInsensitive(t *testing.T) {
	table := &MacroTable{}
	table.Set("VN", "Việt Nam")

	// A macro defined with an upper-case key must still be reachable, both by
	// the exact spelling and by the lower-case one. Get() lower-cases the
	// lookup, so Set() has to normalize the key the same way.
	for _, key := range []string{"VN", "vn", "Vn"} {
		if val, ok := table.Get(key); !ok || val != "Việt Nam" {
			t.Errorf("Set(VN) then Get(%s) got [%s,%v] expected [Việt Nam,true]", key, val, ok)
		}
	}
}

func TestMacroTableSetGuards(t *testing.T) {
	var nilTable *MacroTable
	nilTable.Set("vn", "Việt Nam") // must not panic

	table := &MacroTable{}
	table.Set("", "Việt Nam")
	if !table.Empty() {
		t.Errorf("Set(empty key) got [stored] expected [ignored]")
	}
}
