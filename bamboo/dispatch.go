/*
 * SPDX-FileCopyrightText: 2026-2026 Nguyen Hoong Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */
package main

import (
	"bufio"
	"bytes"
	"encoding/binary"
	"os"
	"sort"

	"bamboo-core"
)

// dispatch executes one decoded request. It is the wire-facing twin of the old
// bamboo-c.go CGo exports: same ops, same semantics, same handle conventions.
// The connection is served sequentially, one handler at a time.
func dispatch(req request) response {
	switch req.op {
	case opPing:
		return response{status: statusOk}
	case opNewEngine:
		return dispatchNewEngine(req)
	case opNewCustom:
		return dispatchNewCustomEngine(req)
	case opNewMacroTable:
		return dispatchNewMacroTable(req)
	case opNewDictionary:
		return dispatchNewDictionary(req)
	case opDeleteObject:
		deleteHandle(req.handle)
		return response{status: statusOk}
	case opProcessKey:
		return dispatchProcessKey(req)
	case opPullPreedit:
		engine, ok := engineFromHandle(req.handle)
		if !ok {
			return response{status: statusError}
		}
		return response{status: statusOk, payload: encodeString(engine.preeditText)}
	case opCommitPreedit:
		engine, ok := engineFromHandle(req.handle)
		if !ok {
			return response{status: statusError}
		}
		engine.commitPreeditAndReset(engine.getPreeditString())
		return response{status: statusOk}
	case opPullCommit:
		engine, ok := engineFromHandle(req.handle)
		if !ok {
			return response{status: statusError}
		}
		commitText := engine.commitText
		engine.commitText = ""
		return response{status: statusOk, payload: encodeString(bamboo.Encode(engine.outputCharset, commitText))}
	case opSetMacro:
		engine, ok := engineFromHandle(req.handle)
		if !ok || len(req.payload) < 1 {
			return response{status: statusError}
		}
		engine.macroEnabled = req.payload[0] != 0
		return response{status: statusOk}
	case opSetOption:
		return dispatchSetOption(req)
	case opResetEngine:
		engine, ok := engineFromHandle(req.handle)
		if !ok {
			return response{status: statusError}
		}
		engine.commitPreeditAndReset("")
		return response{status: statusOk}
	case opRebuildText:
		engine, ok := engineFromHandle(req.handle)
		if !ok {
			return response{status: statusError}
		}
		text, rest, err := decodeString(req.payload)
		if err != nil || len(rest) != 0 {
			return response{status: statusError}
		}
		engine.preeditor.RebuildEngineFromText(text)
		engine.preeditText = engine.getPreeditString()
		engine.commitText = ""
		return response{status: statusOk}
	case opGetCharsets:
		return response{status: statusOk, payload: encodeStringList(bamboo.GetCharsetNames())}
	case opGetIMNames:
		return response{status: statusOk, payload: encodeStringList(inputMethodNames())}
	default:
		return response{status: statusError}
	}
}

func dispatchNewEngine(req request) response {
	name, rest, err := decodeString(req.payload)
	if err != nil || len(rest) < 16 {
		return response{status: statusError}
	}
	dictHandle := binary.LittleEndian.Uint64(rest[0:8])
	tableHandle := binary.LittleEndian.Uint64(rest[8:16])
	if len(rest) != 16 {
		return response{status: statusError}
	}

	dict := newDictionary(nil)
	if d, ok := dictionaryFromHandle(dictHandle); ok {
		dict = d
	}
	table, ok := macroTableFromHandle(tableHandle)
	if !ok {
		return response{status: statusError}
	}

	engine := &FcitxBambooEngine{
		preeditor:               bamboo.NewEngine(bamboo.ParseInputMethod(bamboo.InputMethodDefinitions, name), bamboo.EstdFlags),
		macroTable:              table,
		dictionary:              dict,
		autoNonVnRestore:        true,
		ddFreeStyle:             true,
		macroEnabled:            false,
		autoCapitalizeMacro:     false,
		lastKeyWithShift:        false,
		spellCheckWithDicts:     true,
		preeditText:             "",
		commitText:              "",
		shouldRestoreKeyStrokes: false,
		outputCharset:           "Unicode",
		timeFormat:              "%H:%M",
		dateFormat:              "%d/%m/%Y",
	}
	return response{status: statusOk, payload: encodeUint64(newHandle(engine))}
}

func dispatchNewCustomEngine(req request) response {
	pairs, rest, err := decodePairs(req.payload)
	if err != nil || len(rest) != 16 {
		return response{status: statusError}
	}
	dictHandle := binary.LittleEndian.Uint64(rest[0:8])
	tableHandle := binary.LittleEndian.Uint64(rest[8:16])

	definitions := map[string]bamboo.InputMethodDefinition{
		"Custom": map[string]string{},
	}
	for i := 0; i+1 < len(pairs); i += 2 {
		definitions["Custom"][pairs[i]] = pairs[i+1]
	}

	dict := newDictionary(nil)
	if d, ok := dictionaryFromHandle(dictHandle); ok {
		dict = d
	}
	table, ok := macroTableFromHandle(tableHandle)
	if !ok {
		return response{status: statusError}
	}

	engine := &FcitxBambooEngine{
		preeditor:               bamboo.NewEngine(bamboo.ParseInputMethod(definitions, "Custom"), bamboo.EstdFlags),
		macroTable:              table,
		dictionary:              dict,
		autoNonVnRestore:        true,
		ddFreeStyle:             true,
		macroEnabled:            false,
		autoCapitalizeMacro:     false,
		lastKeyWithShift:        false,
		spellCheckWithDicts:     false,
		preeditText:             "",
		commitText:              "",
		shouldRestoreKeyStrokes: false,
		outputCharset:           "Unicode",
		timeFormat:              "%H:%M",
		dateFormat:              "%d/%m/%Y",
	}
	return response{status: statusOk, payload: encodeUint64(newHandle(engine))}
}

func dispatchNewMacroTable(req request) response {
	pairs, rest, err := decodePairs(req.payload)
	if err != nil || len(rest) != 0 {
		return response{status: statusError}
	}
	table := &MacroTable{mTable: map[string]string{}}
	for i := 0; i+1 < len(pairs); i += 2 {
		table.mTable[pairs[i]] = pairs[i+1]
	}
	return response{status: statusOk, payload: encodeUint64(newHandle(table))}
}

// dispatchNewDictionary consumes the fd received via SCM_RIGHTS; ownership
// moves to the helper here, mirroring the old in-process transfer.
func dispatchNewDictionary(req request) response {
	if len(req.fds) == 0 {
		return response{status: statusError}
	}
	fd := req.fds[0]
	req.fds = req.fds[1:]
	closeUnusedFds(req.fds)

	f := os.NewFile(uintptr(fd), "dict")
	if f == nil {
		return response{status: statusError}
	}
	defer f.Close()

	data := map[string]bool{}
	rd := bufio.NewReader(f)
	for {
		line, _, err := rd.ReadLine()
		if err != nil {
			break
		}
		if len(line) == 0 {
			continue
		}
		data[string(bytes.ToLower(line))] = true
	}
	return response{status: statusOk, payload: encodeUint64(newHandle(newDictionary(data)))}
}

func dispatchProcessKey(req request) response {
	engine, ok := engineFromHandle(req.handle)
	if !ok || len(req.payload) < 8 {
		return response{status: statusError}
	}
	keyVal := binary.LittleEndian.Uint32(req.payload[0:4])
	state := binary.LittleEndian.Uint32(req.payload[4:8])
	processed := engine.preeditProcessKeyEvent(keyVal, state)
	processedByte := byte(0)
	if processed {
		processedByte = 1
	}
	return response{status: statusOk, payload: []byte{processedByte}}
}

// engineOption mirrors FcitxBambooEngineOption; field order must stay in sync
// with encodeOption in src/bamboo-ipc.cpp.
type engineOption struct {
	autoNonVnRestore    bool
	ddFreeStyle         bool
	macroEnabled        bool
	autoCapitalizeMacro bool
	spellCheckWithDicts bool
	outputCharset       string
	modernStyle         bool
	freeMarking         bool
	w2u                 int32
	bracketTransform    int32
	timeFormat          string
	dateFormat          string
}

func decodeOption(payload []byte) (engineOption, error) {
	var opt engineOption
	p := payload
	flags, p, err := decodeBytes(5, p)
	if err != nil {
		return opt, err
	}
	opt.autoNonVnRestore = flags[0] != 0
	opt.ddFreeStyle = flags[1] != 0
	opt.macroEnabled = flags[2] != 0
	opt.autoCapitalizeMacro = flags[3] != 0
	opt.spellCheckWithDicts = flags[4] != 0
	if opt.outputCharset, p, err = decodeString(p); err != nil {
		return opt, err
	}
	flags2, p, err := decodeBytes(2, p)
	if err != nil {
		return opt, err
	}
	opt.modernStyle = flags2[0] != 0
	opt.freeMarking = flags2[1] != 0
	if len(p) < 8 {
		return opt, errTooShort
	}
	opt.w2u = int32(binary.LittleEndian.Uint32(p[0:4]))
	opt.bracketTransform = int32(binary.LittleEndian.Uint32(p[4:8]))
	p = p[8:]
	if opt.timeFormat, p, err = decodeString(p); err != nil {
		return opt, err
	}
	if opt.dateFormat, p, err = decodeString(p); err != nil {
		return opt, err
	}
	if len(p) != 0 {
		return opt, errTrailing
	}
	return opt, nil
}

func dispatchSetOption(req request) response {
	engine, ok := engineFromHandle(req.handle)
	if !ok {
		return response{status: statusError}
	}
	opt, err := decodeOption(req.payload)
	if err != nil {
		return response{status: statusError}
	}

	engine.autoNonVnRestore = opt.autoNonVnRestore
	engine.ddFreeStyle = opt.ddFreeStyle
	engine.macroEnabled = opt.macroEnabled
	engine.autoCapitalizeMacro = opt.autoCapitalizeMacro
	engine.spellCheckWithDicts = opt.spellCheckWithDicts
	engine.outputCharset = opt.outputCharset

	flags := bamboo.EstdFlags
	if opt.modernStyle {
		flags &= ^bamboo.EstdToneStyle
	} else {
		flags |= bamboo.EstdToneStyle
	}
	if opt.freeMarking {
		flags |= bamboo.EfreeToneMarking
	} else {
		flags &= ^bamboo.EfreeToneMarking
	}
	if opt.w2u != 0 {
		flags |= bamboo.Ew2uEnabled
	} else {
		flags &= ^bamboo.Ew2uEnabled
	}
	if opt.bracketTransform != 0 {
		flags |= bamboo.EbracketTransformEnabled
	} else {
		flags &= ^bamboo.EbracketTransformEnabled
	}
	engine.preeditor.SetFlag(flags)
	engine.preeditor.SetW2UMode(int(opt.w2u))
	engine.preeditor.SetBracketTransformMode(int(opt.bracketTransform))
	engine.timeFormat = opt.timeFormat
	engine.dateFormat = opt.dateFormat
	return response{status: statusOk}
}

// inputMethodNames preserves the exact order the old CGo export produced,
// so the config UI list does not reshuffle.
func inputMethodNames() []string {
	order := map[string]int{
		"Telex":                   0,
		"VNI":                     1,
		"Telex + VNI":             2,
		"Telex + VNI + VIQR":      3,
		"VIQR":                    4,
		"Microsoft layout":        5,
		"VNI Bàn phím tiếng Pháp": 6,
	}
	names := make([]string, len(bamboo.InputMethodDefinitions))
	i := 0
	for imName := range bamboo.InputMethodDefinitions {
		names[i] = imName
		i++
	}
	sort.Slice(names, func(i, j int) bool {
		oi, oki := order[names[i]]
		oj, okj := order[names[j]]
		if oki && okj {
			return oi < oj
		}
		if oki {
			return true
		}
		if okj {
			return false
		}
		return names[i] < names[j]
	})
	return names
}

// ---- wire codec helpers ----

var (
	errTooShort = &wireError{"lotus-engine-helper: truncated payload"}
	errTrailing = &wireError{"lotus-engine-helper: trailing payload bytes"}
)

type wireError struct{ msg string }

func (e *wireError) Error() string { return e.msg }

func encodeUint64(v uint64) []byte {
	var b [8]byte
	binary.LittleEndian.PutUint64(b[:], v)
	return b[:]
}

func encodeString(s string) []byte {
	buf := make([]byte, 4+len(s))
	binary.LittleEndian.PutUint32(buf[0:4], uint32(len(s)))
	copy(buf[4:], s)
	return buf
}

func encodeStringList(list []string) []byte {
	size := 4
	for _, s := range list {
		size += 4 + len(s)
	}
	buf := make([]byte, size)
	binary.LittleEndian.PutUint32(buf[0:4], uint32(len(list)))
	pos := 4
	for _, s := range list {
		binary.LittleEndian.PutUint32(buf[pos:pos+4], uint32(len(s)))
		copy(buf[pos+4:], s)
		pos += 4 + len(s)
	}
	return buf
}

func decodeBytes(n int, p []byte) ([]byte, []byte, error) {
	if len(p) < n {
		return nil, nil, errTooShort
	}
	return p[:n], p[n:], nil
}

func decodeString(p []byte) (string, []byte, error) {
	if len(p) < 4 {
		return "", nil, errTooShort
	}
	n := binary.LittleEndian.Uint32(p[0:4])
	if uint64(len(p)-4) < uint64(n) {
		return "", nil, errTooShort
	}
	return string(p[4 : 4+n]), p[4+n:], nil
}

// decodePairs reads [u32 count]([u32 klen] key [u32 vlen] value)... and
// returns the flattened key,value,key,value sequence the handlers consume.
func decodePairs(p []byte) ([]string, []byte, error) {
	if len(p) < 4 {
		return nil, nil, errTooShort
	}
	count := binary.LittleEndian.Uint32(p[0:4])
	p = p[4:]
	pairs := make([]string, 0, count*2)
	for i := uint32(0); i < count; i++ {
		var k, v string
		var err error
		if k, p, err = decodeString(p); err != nil {
			return nil, nil, err
		}
		if v, p, err = decodeString(p); err != nil {
			return nil, nil, err
		}
		pairs = append(pairs, k, v)
	}
	return pairs, p, nil
}
