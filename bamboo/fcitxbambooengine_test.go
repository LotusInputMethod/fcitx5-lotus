/*
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
package main

import (
	"regexp"
	"strings"
	"testing"

	"bamboo-core"
)

func newTestEngine(dict map[string]bool, spellCheckWithDicts bool) *FcitxBambooEngine {
	return &FcitxBambooEngine{
		preeditor:           bamboo.NewEngine(bamboo.ParseInputMethod(bamboo.InputMethodDefinitions, "Telex"), bamboo.EstdFlags),
		macroTable:          &MacroTable{},
		dictionary:          dict,
		autoNonVnRestore:    true,
		ddFreeStyle:         true,
		spellCheckWithDicts: spellCheckWithDicts,
		outputCharset:       "Unicode",
		timeFormat:          "%H:%M",
		dateFormat:          "%d/%m/%Y",
	}
}

func typeKeys(e *FcitxBambooEngine, keys string) {
	for _, r := range keys {
		e.preeditProcessKeyEvent(uint32(r), 0)
	}
}

func TestDetermineMacroCase(t *testing.T) {
	cases := []struct {
		in   string
		want uint8
	}{
		{"abc", VnCaseAllSmall},
		{"ABC", VnCaseAllCapital},
		{"aBc", VnCaseNoChange},
		{"123", VnCaseNoChange},
		{"", VnCaseNoChange},
		{"áộ", VnCaseAllSmall},
		{"ÁỘ", VnCaseAllCapital},
	}
	for _, c := range cases {
		if got := determineMacroCase(c.in); got != c.want {
			t.Errorf("determineMacroCase(%q) got [%d] expected [%d]", c.in, got, c.want)
		}
	}
}

func TestGetLastRune(t *testing.T) {
	if got := getLastRune(""); got != 0 {
		t.Errorf("getLastRune(\"\") got [%c] expected [0]", got)
	}
	if got := getLastRune("abc"); got != 'c' {
		t.Errorf("getLastRune(abc) got [%c] expected [c]", got)
	}
	if got := getLastRune("tắk"); got != 'k' {
		t.Errorf("getLastRune(tắk) got [%c] expected [k]", got)
	}
}

func TestInKeyList(t *testing.T) {
	list := []rune{'a', 'w', 's'}
	if !inKeyList(list, 'a') {
		t.Errorf("inKeyList([a w s], a) got [false] expected [true]")
	}
	if inKeyList(list, 'z') {
		t.Errorf("inKeyList([a w s], z) got [true] expected [false]")
	}
	if inKeyList(nil, 'a') {
		t.Errorf("inKeyList(nil, a) got [true] expected [false]")
	}
}

func TestFormatTime(t *testing.T) {
	e := &FcitxBambooEngine{}
	if got := e.formatTime(""); got != "" {
		t.Errorf("formatTime(\"\") got [%s] expected [empty]", got)
	}
	if got := e.formatTime("no placeholders"); got != "no placeholders" {
		t.Errorf("formatTime(no placeholders) got [%s] expected [same]", got)
	}
	if got := e.formatTime("%H:%M"); !regexp.MustCompile(`^\d{2}:\d{2}$`).MatchString(got) {
		t.Errorf("formatTime(%%H:%%M) got [%s] expected shape HH:MM", got)
	}
	if got := e.formatTime("%d/%m/%Y"); !regexp.MustCompile(`^\d{2}/\d{2}/\d{4}$`).MatchString(got) {
		t.Errorf("formatTime(%%d/%%m/%%Y) got [%s] expected shape DD/MM/YYYY", got)
	}
	if got := e.formatTime("%Q"); got == "" || got == "%Q" {
		t.Errorf("formatTime(%%Q) got [%s] expected fallback different from input", got)
	}
}

func TestExpandMacro(t *testing.T) {
	e := &FcitxBambooEngine{}
	// autoCapitalize off: macro returned as-is regardless of trigger case
	if got := e.expandMacro("VN", "Việt Nam"); got != "Việt Nam" {
		t.Errorf("expandMacro no-cap got [%s] expected [Việt Nam]", got)
	}
	// autoCapitalize on, trigger all-lowercase -> lowercase macro
	e.autoCapitalizeMacro = true
	if got := e.expandMacro("vn", "Việt Nam"); got != "việt nam" {
		t.Errorf("expandMacro all-small got [%s] expected [việt nam]", got)
	}
	if got := e.expandMacro("VN", "việt nam"); got != "VIỆT NAM" {
		t.Errorf("expandMacro all-capital got [%s] expected [VIỆT NAM]", got)
	}
	if got := e.expandMacro("Vn", "Việt Nam"); got != "Việt Nam" {
		t.Errorf("expandMacro mixed got [%s] expected [Việt Nam] (unchanged)", got)
	}
	e.autoCapitalizeMacro = false
	// $TIME substitution
	e.timeFormat = "%H:%M"
	if got := e.expandMacro("now", "giờ $TIME"); !regexp.MustCompile(`\d`).MatchString(got) {
		t.Errorf("expandMacro $TIME got [%s] expected to contain digits", got)
	}
	// $TIME untouched when timeFormat is empty
	e.timeFormat = ""
	if got := e.expandMacro("now", "giờ $TIME"); got != "giờ $TIME" {
		t.Errorf("expandMacro $TIME empty-format got [%s] expected literal [$TIME]", got)
	}
	// $DATE substitution
	e.dateFormat = "%d/%m/%Y"
	if got := e.expandMacro("now", "$DATE"); !strings.Contains(got, "/") {
		t.Errorf("expandMacro $DATE got [%s] expected to contain [/]", got)
	}
}

func TestUpdatePreeditClearsBoth(t *testing.T) {
	e := &FcitxBambooEngine{preeditText: "x", commitText: "y"}
	e.updatePreedit("")
	if e.preeditText != "" || e.commitText != "" {
		t.Errorf("updatePreedit(\"\") left preedit=[%s] commit=[%s], expected both empty", e.preeditText, e.commitText)
	}
}

func TestEncodeCharsets(t *testing.T) {
	// Identity across charsets (current behavior).
	// Update expected values if charset conversion is ever added or changed.
	for _, cs := range []string{"Unicode", "VNI", "TCVN3"} {
		if got := bamboo.Encode(cs, "tôi"); got != "tôi" {
			t.Errorf("Encode(%s, tôi) got [%s] expected [tôi]", cs, got)
		}
	}
}

func TestPreeditTelexComposition(t *testing.T) {
	cases := []struct {
		keys string
		want string
	}{
		{"aw", "ă"},
		{"dd", "đ"},
		{"tooi", "tôi"},
		{"chaof", "chào"},
	}
	for _, c := range cases {
		e := newTestEngine(nil, false)
		typeKeys(e, c.keys)
		if got := e.preeditText; got != c.want {
			t.Errorf("type [%s] preedit got [%s] expected [%s]", c.keys, got, c.want)
		}
	}
}

func TestWordBreakCommit(t *testing.T) {
	e := newTestEngine(nil, false)
	typeKeys(e, "chao")
	ok := e.preeditProcessKeyEvent(FcitxSpace, 0)
	if !ok {
		t.Errorf("space on [chao] returned [false] expected [true]")
	}
	if e.commitText != "chao " {
		t.Errorf("commit got [%s] expected [chao ]", e.commitText)
	}
	if e.preeditText != "" {
		t.Errorf("preedit after commit got [%s] expected [empty]", e.preeditText)
	}
}

func TestAutoRestoreDictMode(t *testing.T) {
	// In dict -> kept Vietnamese
	e := newTestEngine(map[string]bool{"chào": true}, true)
	typeKeys(e, "chaof")
	if got := e.preeditText; got != "chào" {
		t.Fatalf("preedit got [%s] expected [chào]", got)
	}
	e.preeditProcessKeyEvent(FcitxSpace, 0)
	if e.commitText != "chào " {
		t.Errorf("dict-hit commit got [%s] expected [chào ]", e.commitText)
	}

	// Not in dict -> restored to raw keystrokes at word break
	e = newTestEngine(map[string]bool{"chào": true}, true)
	typeKeys(e, "khoawjm")
	if got := e.preeditText; got != "khoặm" {
		t.Fatalf("preedit got [%s] expected [khoặm]", got)
	}
	e.preeditProcessKeyEvent(FcitxSpace, 0)
	if e.commitText != "khoawjm " {
		t.Errorf("dict-miss commit got [%s] expected [khoawjm ] (raw restore)", e.commitText)
	}
}

func TestAutoRestoreRulesMode(t *testing.T) {
	e := newTestEngine(nil, false)
	// rules-invalid sequence -> must fall back
	typeKeys(e, "tak")
	if !e.mustFallbackToEnglish() {
		t.Errorf("mustFallbackToEnglish for [tak] got [false] expected [true] (rules-invalid)")
	}
	// rules-valid word -> keep
	e = newTestEngine(nil, false)
	typeKeys(e, "tooi")
	if e.mustFallbackToEnglish() {
		t.Errorf("mustFallbackToEnglish for [tooi] got [true] expected [false]")
	}
	// full flow: rules-valid rime survives word break (cv-free bamboo-core)
	e = newTestEngine(nil, false)
	typeKeys(e, "boawjm")
	if got := e.preeditText; got != "boặm" {
		t.Fatalf("preedit got [%s] expected [boặm]", got)
	}
	e.preeditProcessKeyEvent(FcitxSpace, 0)
	if e.commitText != "boặm " {
		t.Errorf("rules-valid commit got [%s] expected [boặm ]", e.commitText)
	}
}

func TestDDFreeStyle(t *testing.T) {
	e := newTestEngine(map[string]bool{}, true)
	typeKeys(e, "dd")
	if e.mustFallbackToEnglish() {
		t.Errorf("ddFreeStyle on: mustFallbackToEnglish for [dd] got [true] expected [false]")
	}
	e.ddFreeStyle = false
	if !e.mustFallbackToEnglish() {
		t.Errorf("ddFreeStyle off: mustFallbackToEnglish for [dd] got [false] expected [true]")
	}
}

func TestAutoNonVnRestoreDisabled(t *testing.T) {
	e := newTestEngine(map[string]bool{}, true)
	e.autoNonVnRestore = false
	typeKeys(e, "boawjm")
	e.preeditProcessKeyEvent(FcitxSpace, 0)
	if e.commitText != "boặm " {
		t.Errorf("autoNonVnRestore disabled commit got [%s] expected [boặm ] (never restore)", e.commitText)
	}
}

func TestToUpper(t *testing.T) {
	e := newTestEngine(nil, false)
	// letters untouched; brackets inactive by default
	if got := e.toUpper('b'); got != 'b' {
		t.Errorf("toUpper(b) got [%c] expected [b]", got)
	}
	if got := e.toUpper('['); got != '[' {
		t.Errorf("toUpper([) without bracket mode got [%c] expected [[]", got)
	}
	// brackets active: [ -> {, ] -> }, { -> [, } -> ]
	e.preeditor.SetBracketTransformMode(bamboo.BracketTransformEverywhere)
	cases := []struct {
		in, want rune
	}{
		{'[', '{'}, {']', '}'}, {'{', '['}, {'}', ']'},
	}
	for _, c := range cases {
		if got := e.toUpper(c.in); got != c.want {
			t.Errorf("toUpper(%c) with bracket mode got [%c] expected [%c]", c.in, got, c.want)
		}
	}
}

func TestShiftSpaceRestoreKeyStrokes(t *testing.T) {
	e := newTestEngine(nil, false)
	typeKeys(e, "aw")
	if got := e.preeditText; got != "ă" {
		t.Fatalf("preedit got [%s] expected [ă]", got)
	}
	e.shouldRestoreKeyStrokes = true
	e.preeditProcessKeyEvent(FcitxSpace, 0)
	if e.shouldRestoreKeyStrokes {
		t.Errorf("shouldRestoreKeyStrokes not consumed")
	}
	if got := e.preeditText; got != "aw" {
		t.Errorf("preedit after shift+space got [%s] expected [aw] (raw keystrokes)", got)
	}
}

func TestBackspace(t *testing.T) {
	// single rune -> commit empty and reset
	e := newTestEngine(nil, false)
	typeKeys(e, "aw")
	ok := e.preeditProcessKeyEvent(FcitxBackSpace, 0)
	if !ok {
		t.Errorf("backspace on single rune returned [false] expected [true]")
	}
	if e.commitText != "" || e.preeditText != "" {
		t.Errorf("backspace single rune left commit=[%s] preedit=[%s], expected both empty", e.commitText, e.preeditText)
	}

	// multi rune -> remove last char
	e = newTestEngine(nil, false)
	typeKeys(e, "chao")
	e.preeditProcessKeyEvent(FcitxBackSpace, 0)
	if got := e.preeditText; got != "cha" {
		t.Errorf("backspace on [chao] preedit got [%s] expected [cha]", got)
	}

	// empty engine -> rejected
	e = newTestEngine(nil, false)
	if ok := e.preeditProcessKeyEvent(FcitxBackSpace, 0); ok {
		t.Errorf("backspace on empty engine returned [true] expected [false]")
	}
}

func TestTabMacroExpansion(t *testing.T) {
	e := newTestEngine(nil, false)
	e.macroEnabled = true
	e.macroTable = &MacroTable{mTable: map[string]string{"vn": "Việt Nam"}}
	typeKeys(e, "vn")
	ok := e.preeditProcessKeyEvent(FcitxTab, 0)
	if !ok {
		t.Errorf("tab with macro match returned [false] expected [true]")
	}
	if e.commitText != "Việt Nam" {
		t.Errorf("tab macro commit got [%s] expected [Việt Nam]", e.commitText)
	}

	// no macro match -> commit composed text, reject
	e = newTestEngine(nil, false)
	e.macroEnabled = true
	e.macroTable = &MacroTable{mTable: map[string]string{"vn": "Việt Nam"}}
	typeKeys(e, "xx")
	if ok := e.preeditProcessKeyEvent(FcitxTab, 0); ok {
		t.Errorf("tab without macro match returned [true] expected [false]")
	}
	if e.commitText != "xx" {
		t.Errorf("tab no-match commit got [%s] expected [xx]", e.commitText)
	}
}

func TestTabTimeMacro(t *testing.T) {
	e := newTestEngine(nil, false)
	e.macroEnabled = true
	// Macro keys match the composed text: "now" composes to "nơ" (o+w -> ơ).
	e.macroTable = &MacroTable{mTable: map[string]string{"nơ": "$TIME giờ"}}
	e.timeFormat = "%H:%M"
	typeKeys(e, "now")
	e.preeditProcessKeyEvent(FcitxTab, 0)
	if !regexp.MustCompile(`^\d{2}:\d{2} giờ$`).MatchString(e.commitText) {
		t.Errorf("tab time macro commit got [%s] expected shape [HH:MM giờ]", e.commitText)
	}
}

func TestCanProcessKey(t *testing.T) {
	e := newTestEngine(nil, false)
	for _, kv := range []uint32{FcitxSpace, FcitxBackSpace, ','} {
		if !e.canProcessKey(kv) {
			t.Errorf("canProcessKey(%d) got [false] expected [true]", kv)
		}
	}
	for _, r := range "aw" {
		if !e.canProcessKey(uint32(r)) {
			t.Errorf("canProcessKey(%c) got [false] expected [true]", r)
		}
	}
	// Non-ASCII non-letter: not alpha, not punctuation, not a Vietnamese rune.
	if e.canProcessKey('€') {
		t.Errorf("canProcessKey(€) got [true] expected [false]")
	}
}

func TestIsValidState(t *testing.T) {
	e := newTestEngine(nil, false)
	if !e.isValidState(0) {
		t.Errorf("isValidState(0) got [false] expected [true]")
	}
	if !e.isValidState(FcitxShiftMask) {
		t.Errorf("isValidState(ShiftMask) got [false] expected [true] (shift is allowed)")
	}
	for _, mask := range []uint32{FcitxControlMask, FcitxMod1Mask, FcitxIgnoredMask, FcitxSuperMask, FcitxHyperMask, FcitxMetaMask} {
		if e.isValidState(mask) {
			t.Errorf("isValidState(%d) got [true] expected [false]", mask)
		}
	}
}

func TestMacroDefinedWithUpperCaseKey(t *testing.T) {
	// Macro keys are looked up case-insensitively, so a key entered in capitals
	// in the settings UI must trigger just like a lower-case one.
	e := newTestEngine(nil, false)
	e.macroEnabled = true
	e.autoCapitalizeMacro = true
	e.macroTable = &MacroTable{}
	e.macroTable.Set("VN", "Việt Nam")

	typeKeys(e, "VN")
	e.preeditProcessKeyEvent(FcitxSpace, 0)
	if e.commitText != "VIỆT NAM " {
		t.Errorf("upper-case macro key commit got [%s] expected [VIỆT NAM ]", e.commitText)
	}

	// The same definition must also fire when typed in lower case.
	e = newTestEngine(nil, false)
	e.macroEnabled = true
	e.macroTable = &MacroTable{}
	e.macroTable.Set("VN", "Việt Nam")

	typeKeys(e, "vn")
	e.preeditProcessKeyEvent(FcitxSpace, 0)
	if e.commitText != "Việt Nam " {
		t.Errorf("lower-case typing of upper-case macro key commit got [%s] expected [Việt Nam ]", e.commitText)
	}
}
