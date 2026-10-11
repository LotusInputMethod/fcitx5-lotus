/*
 * SPDX-FileCopyrightText: 2026-2026 Nguyen Hoong Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */
package main

// Wire protocol between the fcitx5 addon (src/bamboo-ipc.cpp) and this helper:
// SOCK_STREAM over an abstract unix socket, length-framed so payloads (custom
// keymaps, macro tables) are not bound by SEQPACKET packet-size limits.
//
// Request:  [u32 len][u8 op][u64 handle][payload...]   (len covers op..payload)
// Response: [u32 len][u8 status][payload...]           (len covers status..payload)
//
// Integers are little-endian. Strings are [u32 len][utf-8 bytes]; lists are
// [u32 count][string...]; key/value pairs are [u32 count][key][value]...
// OP_NEW_DICTIONARY carries its fd out of band via one SCM_RIGHTS cmsg.
//
// Op numbers, option field order and the version MUST stay in sync with
// src/bamboo-ipc.cpp.
const (
	opHello         = 1  // handshake: payload u32 version -> u32 accepted version
	opPing          = 2  // liveness probe -> empty
	opNewEngine     = 3  // str imName, u64 dictHandle, u64 tableHandle -> u64 handle
	opNewCustom     = 4  // kv definitions, u64 dictHandle, u64 tableHandle -> u64 handle
	opNewMacroTable = 5  // kv definitions -> u64 handle
	opNewDictionary = 6  // (fd via SCM_RIGHTS) -> u64 handle
	opDeleteObject  = 7  // handle from request header -> empty
	opProcessKey    = 8  // u32 keyVal, u32 state -> u8 processed
	opPullPreedit   = 9  // -> str preedit
	opCommitPreedit = 10 // -> empty
	opPullCommit    = 11 // -> str commitText (and clears it)
	opSetMacro      = 12 // u8 enabled -> empty
	opSetOption     = 13 // option struct, see encodeOption -> empty
	opResetEngine   = 14 // -> empty
	opRebuildText   = 15 // str text -> empty
	opGetCharsets   = 16 // -> str list
	opGetIMNames    = 17 // -> str list
)

// statusOk means the op completed; any other value means failure, which callers
// map to the old in-process results: 0 handle, false, empty string.
const (
	statusOk    = 0
	statusError = 1
)

// protocolVersion is checked by the opHello handshake; a mismatch closes the
// connection so the client respawns a matching helper.
const protocolVersion = 1

// maxMessageSize caps one framed message: generous for user config (macro
// tables, custom keymaps), tiny for engine strings, a guard against runaways.
const maxMessageSize = 64 << 20
