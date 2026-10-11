/*
 * SPDX-FileCopyrightText: 2022-2022 CSSlayer <wengxt@gmail.com>
 * SPDX-FileCopyrightText: 2026-2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

/**
 * @file bamboo-core.h
 * @brief C API of the bamboo Vietnamese engine.
 *
 * Drop-in replacement for the cgo-generated bamboo-core.h: same signatures,
 * same FcitxBambooEngineOption layout, same malloc/free ownership rules, so
 * lotus-state.cpp, lotus-engine.cpp and the tests compile unmodified. Calls
 * are forwarded over a private abstract unix socket to lotus-engine-helper, a
 * fully static pure-Go binary built with CGO_ENABLED=0 (see bamboo/ and
 * src/bamboo-ipc.cpp), which is what makes the addon buildable on musl libc.
 */

#ifndef _FCITX5_LOTUS_BAMBOO_CORE_H_
#define _FCITX5_LOTUS_BAMBOO_CORE_H_

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Engine options mirrored on the Go side.
 *
 * Field order must stay in sync with the option codec in bamboo/dispatch.go.
 */
typedef struct {
    bool        autoNonVnRestore;    ///< restore invalid words on commit
    bool        ddFreeStyle;         ///< free style for the dd sequence
    bool        macroEnabled;        ///< macro expansion enabled
    bool        autoCapitalizeMacro; ///< capitalize expanded macros
    bool        spellCheckWithDicts; ///< dictionaries participate in spell check
    const char* outputCharset;       ///< output charset name, e.g. "Unicode"
    bool        modernStyle;         ///< modern tone style
    bool        freeMarking;         ///< free tone marking
    int         w2u;                 ///< word-to-unicode conversion mode
    int         bracketTransform;    ///< bracket transform mode
    const char* timeFormat;          ///< strftime-like format for $TIME macros
    const char* dateFormat;          ///< strftime-like format for $DATE macros
} FcitxBambooEngineOption;

/** @brief Connects (and if needed spawns) the engine helper. */
void Init(void);

/**
 * @brief Creates an engine for a built-in input method.
 * @param name input method name, e.g. "Telex".
 * @param dictHandle NewDictionary() handle or 0; tableHandle NewMacroTable() handle, must be valid.
 * @return engine handle or 0 on failure.
 */
uintptr_t NewEngine(const char* name, uintptr_t dictHandle, uintptr_t tableHandle);

/**
 * @brief Creates an engine from a custom keymap definition.
 * @param definition NULL-terminated key/value array: key, value, ..., NULL.
 * @param dictHandle NewDictionary() handle or 0; tableHandle NewMacroTable() handle, must be valid.
 * @return engine handle or 0 on failure.
 */
uintptr_t NewCustomEngine(char** definition, uintptr_t dictHandle, uintptr_t tableHandle);

/**
 * @brief Creates a macro table.
 * @param definition NULL-terminated key/value array: key, value, ..., NULL.
 * @return macro table handle or 0 on failure.
 */
uintptr_t NewMacroTable(char** definition);

/**
 * @brief Creates a dictionary from an open file descriptor.
 * The fd is transferred with SCM_RIGHTS and always closed by this call.
 * @param fd open read-only descriptor of a newline separated word list.
 * @return dictionary handle or 0 on failure.
 */
uintptr_t NewDictionary(uintptr_t fd);

/** @brief Releases a handle obtained from any New* function. */
void DeleteObject(uintptr_t handle);

/**
 * @brief Feeds a key event to the engine.
 * @return true if the engine consumed the key.
 */
bool EngineProcessKeyEvent(uintptr_t engine, uint32_t keyVal, uint32_t state);

/**
 * @brief Returns the current preedit text.
 * @return malloc'ed UTF-8 string, free() it; NULL only on transport failure.
 */
char* EnginePullPreedit(uintptr_t engine);

/** @brief Commits the current preedit and resets composition. */
void EngineCommitPreedit(uintptr_t engine);

/**
 * @brief Drains the pending commit text.
 * @return malloc'ed UTF-8 string (possibly empty), free() it.
 */
char* EnginePullCommit(uintptr_t engine);

/** @brief Enables or disables macro expansion. */
void EngineSetMacroEnabled(uintptr_t engine, bool enabled);

/** @brief Applies a batch of engine options. */
void EngineSetOption(uintptr_t engine, const FcitxBambooEngineOption* option);

/** @brief Resets composition state without committing. */
void ResetEngine(uintptr_t engine);

/** @brief Rebuilds composition state from committed text (surrounding text). */
void EngineRebuildFromText(uintptr_t engine, const char* text);

/**
 * @brief Lists the supported output charset names.
 * @return NULL-terminated malloc'ed array of malloc'ed strings, free() them.
 */
char** GetCharsetNames(void);

/**
 * @brief Lists the built-in input method names.
 * @return NULL-terminated malloc'ed array of malloc'ed strings, free() them.
 */
char** GetInputMethodNames(void);

/**
 * @brief Closes the helper connection and reaps the helper process.
 * Called from the engine destructor; reconnected on demand afterwards.
 */
void LotusEngineHelperShutdown(void);

#ifdef __cplusplus
}
#endif

#endif // _FCITX5_LOTUS_BAMBOO_CORE_H_
