/*
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */
package main

import (
	"sort"
	"strings"
)

// dictionary holds the custom dictionary words sorted in byte order. A sorted
// slice answers both questions the engine needs: an exact word match at a word
// break and a prefix match while a word is still being typed.
type dictionary struct {
	words []string
}

// newDictionary builds a sorted, duplicate-free dictionary from the words read
// from the dictionary file.
func newDictionary(words map[string]bool) *dictionary {
	var list = make([]string, 0, len(words))
	for word := range words {
		if len(word) > 0 {
			list = append(list, word)
		}
	}
	sort.Strings(list)
	var unique = list[:0]
	for i, word := range list {
		if i == 0 || word != list[i-1] {
			unique = append(unique, word)
		}
	}
	return &dictionary{words: unique}
}

// hasWord reports whether word is in the dictionary.
func (d *dictionary) hasWord(word string) bool {
	if d == nil || len(word) == 0 {
		return false
	}
	var i = sort.SearchStrings(d.words, word)
	return i < len(d.words) && d.words[i] == word
}

// hasWordOrPrefix reports whether word is in the dictionary or is the beginning
// of a longer dictionary word, so a word that is still being typed is not
// treated as a miss.
func (d *dictionary) hasWordOrPrefix(word string) bool {
	if d == nil || len(word) == 0 {
		return false
	}
	var i = sort.SearchStrings(d.words, word)
	return i < len(d.words) && strings.HasPrefix(d.words[i], word)
}
