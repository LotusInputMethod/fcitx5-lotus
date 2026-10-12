/*
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */
package main

import "testing"

func TestDictionaryLookup(t *testing.T) {
	d := newDictionary(map[string]bool{"đcu": true, "chào": true, "": true})
	if !d.hasWord("đcu") || !d.hasWord("chào") {
		t.Errorf("hasWord missed a dictionary word: %v", d.words)
	}
	if d.hasWord("đc") {
		t.Errorf("hasWord([đc]) got [true] expected [false] (prefix is not a word)")
	}
	for _, prefix := range []string{"đ", "đc", "c", "ch", "chà"} {
		if !d.hasWordOrPrefix(prefix) {
			t.Errorf("hasWordOrPrefix([%s]) got [false] expected [true]", prefix)
		}
	}
	for _, miss := range []string{"đct", "khoawjm", ""} {
		if d.hasWordOrPrefix(miss) {
			t.Errorf("hasWordOrPrefix([%s]) got [true] expected [false]", miss)
		}
	}
	var nilDict *dictionary
	if nilDict.hasWord("đcu") || nilDict.hasWordOrPrefix("đ") {
		t.Errorf("nil dictionary must not report a hit")
	}
}
