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
