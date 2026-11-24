# Code style rules

This document is the single source of truth for the code rules of fcitx5-lotus. On conflict, this document wins.

## 1. Format & lint (mandatory)

- C/C++: `clang-format` per `.clang-format`; `clang-tidy` per `.clang-tidy`, **0 warnings in repository code**.
- `NOLINT` must name the check and the reason, e.g. `// NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)`.
- QML (`settings-gui`): `qmllint`, Qt 6.2+ compatible.

## 2. Language standard

- C++20 (`CMAKE_CXX_STANDARD 20`), never downgrade.
- Use the standard library: `std::string_view`, `std::span`, `std::ranges`, `std::erase_if`, `std::optional`, `std::cmp_*`, designated initializers.
- Do not rewrite what the standard library already provides. Do not use `std::regex` in the engine (`lotus/`, the part of `src/` loaded into the engine): it grows the binary and is not needed.

## 3. RAII & ownership

- Every resource needs an RAII owner: file descriptors use `FdGuard`, C handles/inherited handles use `std::unique_ptr` + a deleter (`LibevdevDevice`, `LibevdevUinput`, `LibinputEvent`, `UdevDeleter`…).
- Never keep a raw descriptor alive across statements and `close()` it by hand; wrap it as soon as it is obtained (`FdGuard fd(open(...));`), including on error paths.
- No `malloc`/`free`/`strdup`. `new`/`delete` are allowed only in fcitx5 factories (with `NOLINT`).
- Classes that own resources: explicit rule of five (delete copy, declare move); no hand-written destructor that only calls `close()`/`free()`.

## 4. Pointers & references

- Raw pointers are only for: (a) values that may be null, (b) C APIs, (c) signatures imposed by fcitx5/libinput (virtual override, callback, factory).
- Non-null borrows that never change target use a reference (e.g. `LotusEngine& engine_`).
- Do not pass a pointer when a reference is enough; do not take parameters by value when they are only read.

## 5. Strings

- Read-only parameters and data members: `std::string_view`; decoded text: `std::u32string_view`; use `std::string` only when you really own or mutate it.
- Keep `const char*` when both ends are C APIs (`open`, `getenv`, `printf`, `popen`) or when the fcitx5 API takes `const std::string&` — changing the type there only adds a useless `std::string(...)`.
- `test/` keeps `const char*` in the `check(name, ...)` helpers because they feed `std::printf("%s")`.

## 6. File & header structure

- Every `.h` must have a `.cpp` with the same name. Exceptions: headers that only hold type declarations/`constexpr`/one-line accessors/deleters/type aliases/macro log (`common/*.h`, `src/lotus-config.h`, `src/lotus-mode.h`, `src/lotus-log.h`, `src/emoji-entry.h`, `lotus/types.h`, `lotus/transformation.h`, `lotus/rule.h`, `test/*.h`).
- Headers only declare (plus doxygen); function bodies live in the `.cpp`, except one-line accessors, deleters, templates and `inline constexpr`.
- Code shared between the addon and the server lives in `common/` and is named `common-xxx.{h,cpp}`.

## 7. Naming

- Types: `PascalCase`; functions/variables: `camelCase`; data members: `camelCase_`; constants/`static constexpr`: `kPascalCase` (also inside classes: `kMaxEntries`, `kMaxBufferedKeys`); globals: `g_camelCase`; macros and environment constants: `UPPER_SNAKE`.
- Enum class: `PascalCase` for both the type and its members (`LotusMode::Smooth`, `KbOp::Backspace`, `LogLevel::Debug`).
- Files: `kebab-case.{h,cpp}`; include guards: `_<DIRECTORY>_<FILE_NAME>_H_` (e.g. `_COMMON_FD_GUARD_H_`).
- Exception: the `libinput_interface` callbacks (`open_restricted`, `close_restricted`) keep the names used by the libinput documentation.

## 8. Logging & errors

- Addon: `LOTUS_INFO`/`LOTUS_WARN`/`LOTUS_ERROR`; server: `LotusLogger::instance().info/warn/error`.
- Messages in English, stating the action and the subject; pick the right level; do not log repeatedly in a hot loop (log once, like `UinputDevice::reportWriteFailure`).

## 9. Comments & documentation

- Code comments are in English (as in the current codebase); every file has an `@file` block.
- Every public declaration has doxygen (`@brief`, `@param`, `@return`).
- Leave no commented-out code, temporary debug logs, orphan `TODO/FIXME` or joke comments — delete them before opening a PR.

## 10. Tests

- New behaviour or bug fixes need a test in `test/`, registered through the CMake helpers (`add_lotus_headless_test` / `add_lotus_unit_test`).
- Common pattern: `check(name, condition)` / `reportFailure(step, expected, actual, meaning)`, print `FAIL`, and return a non-zero exit code on failure.
- Tests must be deterministic and independent of network/display; tests that bind sockets run in their own namespace (`LOTUS_SOCKET_NAMESPACE`).

## 11. PR scope (one PR, one problem)

- **Each PR solves exactly one problem.** Several things to fix ⇒ several branches/PRs.
- Do not mix unrelated refactors, formatting, renames or documentation into a bug-fix/feature PR.
- Found out-of-scope work ⇒ file an issue or open a separate PR; do not stuff it into the current one.
- A PR must: target `dev`, build with 0 warnings, pass `ctest`, and use a one-line commit message following `type(scope): short description`.
