/*
 * SPDX-FileCopyrightText: 2026-2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */
package main

import (
	"sync"
)

// handleTable replaces runtime/cgo handles (unavailable with CGO_ENABLED=0)
// with process-unique uint64 ids in a mutex-protected map; the connection is
// served sequentially, so the mutex only guards future concurrent use.
//
// Zero is the C++ side's "nothing here" (empty CGoObject, no macro table,
// failed New*), so every wire handle is checked before use, as before.
type handleTable struct {
	mu   sync.Mutex
	next uint64
	objs map[uint64]interface{}
}

func newHandleTable() *handleTable {
	return &handleTable{next: 1, objs: map[uint64]interface{}{}}
}

var table = newHandleTable()

func newHandle(v interface{}) uint64 {
	table.mu.Lock()
	defer table.mu.Unlock()
	id := table.next
	table.next++
	table.objs[id] = v
	return id
}

func objectFromHandle(h uint64) (interface{}, bool) {
	if h == 0 {
		return nil, false
	}
	table.mu.Lock()
	defer table.mu.Unlock()
	v, ok := table.objs[h]
	return v, ok
}

func engineFromHandle(h uint64) (*FcitxBambooEngine, bool) {
	v, ok := objectFromHandle(h)
	if !ok {
		return nil, false
	}
	engine, ok := v.(*FcitxBambooEngine)
	return engine, ok
}

func macroTableFromHandle(h uint64) (*MacroTable, bool) {
	v, ok := objectFromHandle(h)
	if !ok {
		return nil, false
	}
	t, ok := v.(*MacroTable)
	return t, ok
}

func dictionaryFromHandle(h uint64) (*dictionary, bool) {
	v, ok := objectFromHandle(h)
	if !ok {
		return nil, false
	}
	d, ok := v.(*dictionary)
	return d, ok
}

func deleteHandle(h uint64) {
	if h == 0 {
		return
	}
	table.mu.Lock()
	defer table.mu.Unlock()
	delete(table.objs, h)
}
