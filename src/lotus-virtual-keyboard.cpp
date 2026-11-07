/*
 * SPDX-FileCopyrightText: 2026 Zebra2711
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "lotus-virtual-keyboard.h"
#include "lotus-log.h"

#include <fcitx-utils/utf8.h>

#include <algorithm>
#include <utility>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <linux/input-event-codes.h>
#include <wayland-client.h>
#include "virtual-keyboard-unstable-v1-client-protocol.h"

namespace fcitx {
    namespace {
        // Linux evdev codes for the printable section of a PC keyboard. Chromium may
        // fall back to a key's physical meaning even when its keysym is printable.
        // Never put literal text on Escape, Backspace, Tab, Enter, or modifier codes.
        // NOLINTNEXTLINE(modernize-avoid-c-arrays)
        constexpr uint32_t kTextKeys[] = {
            2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27,
            30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 57,
        };
        constexpr size_t   kTextKeysLen = sizeof(kTextKeys) / sizeof(kTextKeys[0]);
        constexpr uint32_t kShiftMod    = 1;

        struct RegistryData {
            wl_seat*                         seat    = nullptr;
            zwp_virtual_keyboard_manager_v1* manager = nullptr;
        };

        void registryGlobal(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t /*version*/) {
            auto* d = static_cast<RegistryData*>(data);
            if (std::strcmp(interface, wl_seat_interface.name) == 0) {
                if (d->seat == nullptr) {
                    d->seat = static_cast<wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface, 1));
                }
            } else if (std::strcmp(interface, zwp_virtual_keyboard_manager_v1_interface.name) == 0) {
                d->manager = static_cast<zwp_virtual_keyboard_manager_v1*>(wl_registry_bind(registry, name, &zwp_virtual_keyboard_manager_v1_interface, 1));
            }
        }

        void                       registryGlobalRemove(void* /*data*/, wl_registry* /*registry*/, uint32_t /*name*/) {}

        const wl_registry_listener kRegistryListener = {.global = registryGlobal, .global_remove = registryGlobalRemove};

        int                        createMemfd(const char* name) {
#ifdef SYS_memfd_create
            int fd = static_cast<int>(syscall(SYS_memfd_create, name, 1 /* MFD_CLOEXEC */));
            if (fd >= 0) {
                return fd;
            }
#endif
            // NOLINTNEXTLINE(modernize-avoid-c-arrays)
            char path[] = "/tmp/lotus-vk-keymap-XXXXXX";
            int  tmpFd  = mkstemp(path);
            if (tmpFd < 0) {
                return -1;
            }
            unlink(path);
            return tmpFd;
        }

        std::string xkbSymName(uint32_t cp) {
            // NOLINTNEXTLINE(modernize-avoid-c-arrays)
            char buf[16];
            if (cp <= 0xFFFF) {
                std::snprintf(buf, sizeof(buf), "U%04X", cp);
            } else {
                std::snprintf(buf, sizeof(buf), "U%08X", cp);
            }
            return buf;
        }
    } // namespace

    LotusVirtualKeyboard& LotusVirtualKeyboard::instance() {
        static LotusVirtualKeyboard k;
        return k;
    }

    LotusVirtualKeyboard::~LotusVirtualKeyboard() {
        disconnect();
    }

    void LotusVirtualKeyboard::disconnect() {
        if (keyboard_ != nullptr) {
            zwp_virtual_keyboard_v1_destroy(keyboard_);
            keyboard_ = nullptr;
        }
        if (manager_ != nullptr) {
            zwp_virtual_keyboard_manager_v1_destroy(manager_);
            manager_ = nullptr;
        }
        if (seat_ != nullptr) {
            wl_seat_destroy(seat_);
            seat_ = nullptr;
        }
        if (registry_ != nullptr) {
            wl_registry_destroy(registry_);
            registry_ = nullptr;
        }
        if (display_ != nullptr) {
            wl_display_disconnect(display_);
            display_ = nullptr;
        }
    }

    bool LotusVirtualKeyboard::ensureConnected() {
        if (keyboard_ != nullptr) {
            return true;
        }
        display_ = wl_display_connect(nullptr);
        if (display_ == nullptr) {
            return false;
        }
        registry_ = wl_display_get_registry(display_);
        RegistryData data;
        wl_registry_add_listener(registry_, &kRegistryListener, &data);
        wl_display_roundtrip(display_);
        seat_    = data.seat;
        manager_ = data.manager;
        if (seat_ == nullptr || manager_ == nullptr) {
            disconnect();
            return false;
        }
        keyboard_ = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(manager_, seat_);
        if (keyboard_ == nullptr) {
            disconnect();
            return false;
        }
        return true;
    }

    bool LotusVirtualKeyboard::available() {
        if (probed_) {
            return supported_;
        }
        probed_    = true;
        supported_ = ensureConnected();
        if (supported_) {
            LOTUS_INFO("compositor supports virtual-keyboard-unstable-v1, using LotusVirtualKeyboard");
        } else {
            LOTUS_INFO("compositor has no virtual-keyboard-unstable-v1, using uinput");
        }
        return supported_;
    }

    bool LotusVirtualKeyboard::uploadKeymap(const uint32_t* cps, size_t n) {
        if (n > kTextKeysLen) {
            return false;
        }
        const uint32_t maxXkb = std::max(kTextKeys[kTextKeysLen - 1] + 8U, KEY_DELETE + 8U);
        std::string    keycodes;
        std::string    symbols;
        keycodes.reserve(128 + (n * 24));
        symbols.reserve(160 + (n * 40));
        keycodes += "    <BKSP> = ";
        keycodes += std::to_string(KEY_BACKSPACE + 8U);
        keycodes += ";\n    <LFSH> = ";
        keycodes += std::to_string(KEY_LEFTSHIFT + 8U);
        keycodes += ";\n    <LEFT> = ";
        keycodes += std::to_string(KEY_LEFT + 8U);
        keycodes += ";\n    <DELE> = ";
        keycodes += std::to_string(KEY_DELETE + 8U);
        keycodes += ";\n";
        symbols += "    key <BKSP> { [ BackSpace ] };\n";
        symbols += "    key <LFSH> { [ Shift_L ] };\n";
        symbols += "    key <LEFT> { [ Left ] };\n";
        symbols += "    key <DELE> { [ Delete ] };\n";
        symbols += "    modifier_map Shift { <LFSH> };\n";
        for (size_t i = 0; i < n; ++i) {
            const uint32_t xkb = kTextKeys[i] + 8U; // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
            // NOLINTNEXTLINE(modernize-avoid-c-arrays)
            char name[16];
            std::snprintf(name, sizeof(name), "K%02zu", i);
            keycodes += "    <";
            keycodes += name;
            keycodes += "> = ";
            keycodes += std::to_string(xkb);
            keycodes += ";\n";
            symbols += "    key <";
            symbols += name;
            symbols += "> { [ ";
            symbols += xkbSymName(cps[i]); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            symbols += " ] };\n";
        }

        std::string keymap;
        keymap.reserve(256 + keycodes.size() + symbols.size());
        keymap += "xkb_keymap {\n";
        keymap += "xkb_keycodes \"lotus-virtual-keyboard\" {\n";
        keymap += "    minimum = 8;\n";
        keymap += "    maximum = ";
        keymap += std::to_string(maxXkb);
        keymap += ";\n";
        keymap += keycodes;
        keymap += "};\n";
        keymap += "xkb_types \"complete\" { include \"complete\" };\n";
        keymap += "xkb_compatibility \"complete\" { include \"complete\" };\n";
        keymap += "xkb_symbols \"lotus_vk\" {\n";
        keymap += "    name[Group1] = \"lotus-virtual-keyboard\";\n";
        keymap += symbols;
        keymap += "};\n";
        keymap += "};\n";

        const int fd = createMemfd("lotus-virtual-keyboard-keymap");
        if (fd < 0) {
            LOTUS_ERROR("virtual-keyboard: memfd_create failed");
            return false;
        }
        const auto size = static_cast<uint32_t>(keymap.size() + 1);
        if (ftruncate(fd, static_cast<off_t>(size)) != 0) {
            close(fd);
            return false;
        }
        ssize_t written = 0;
        while (std::cmp_less(written, size)) {
            const ssize_t w = write(fd, keymap.c_str() + written, size - static_cast<uint32_t>(written)); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            if (w < 0) {
                if (errno == EINTR) {
                    continue;
                }
                close(fd);
                return false;
            }
            written += w;
        }
        lseek(fd, 0, SEEK_SET);
        zwp_virtual_keyboard_v1_keymap(keyboard_, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd, size);
        close(fd);
        zwp_virtual_keyboard_v1_modifiers(keyboard_, 0, 0, 0, 0);
        return wl_display_roundtrip(display_) >= 0;
    }

    void LotusVirtualKeyboard::tap(uint32_t evdevKey, bool track) {
        if (track) {
            echo_.emplace_back(evdevKey + 8U, time_);
        }
        zwp_virtual_keyboard_v1_key(keyboard_, time_++, evdevKey, WL_KEYBOARD_KEY_STATE_PRESSED);
        zwp_virtual_keyboard_v1_key(keyboard_, time_++, evdevKey, WL_KEYBOARD_KEY_STATE_RELEASED);
    }

    void LotusVirtualKeyboard::setShift(bool down) {
        zwp_virtual_keyboard_v1_modifiers(keyboard_, down ? kShiftMod : 0, 0, 0, 0);
    }

    bool LotusVirtualKeyboard::consumeEcho(uint32_t code, uint32_t time) {
        for (auto it = echo_.begin(); it != echo_.end(); ++it) {
            if (it->first == code && it->second == time) {
                echo_.erase(it);
                return true;
            }
        }
        return false;
    }

    bool LotusVirtualKeyboard::emit(int backspaces, std::string_view utf8, bool trackEcho) {
        return emitImpl(backspaces, utf8, trackEcho, false);
    }

    bool LotusVirtualKeyboard::emitSelect(int count, std::string_view utf8, bool trackEcho) {
        return emitImpl(count, utf8, trackEcho, true);
    }

    bool LotusVirtualKeyboard::emitImpl(int count, std::string_view utf8, bool trackEcho, bool shiftLeft) {
        count = std::max(count, 0);
        std::vector<uint32_t> cps;
        if (!utf8.empty()) {
            if (!utf8::validate(utf8)) {
                LOTUS_ERROR("virtual-keyboard: invalid UTF-8");
                return false;
            }
            for (auto cp : utf8::MakeUTF8CharRange(utf8)) {
                cps.push_back(static_cast<uint32_t>(cp));
            }
        }
        if (count == 0 && cps.empty()) {
            return true;
        }
        if (!ensureConnected()) {
            return false;
        }
        injecting_ = true;
        if (echo_.size() > 512) {
            echo_.clear();
        }
        bool   ok    = true;
        bool   first = true;
        size_t pos   = 0;
        while (ok && (first || pos < cps.size())) {
            const size_t n = std::min(kTextKeysLen, cps.size() - pos);
            ok             = uploadKeymap(cps.data() + pos, n); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            if (!ok) {
                break;
            }
            if (first) {
                if (shiftLeft) {
                    setShift(true);
                    wl_display_roundtrip(display_);
                    for (int i = 0; i < count; ++i) {
                        tap(KEY_LEFT, trackEcho);
                    }
                    setShift(false);
                    wl_display_roundtrip(display_);
                    if (!cps.empty()) {
                        tap(KEY_DELETE, trackEcho);
                    }
                } else {
                    for (int i = 0; i < count; ++i) {
                        tap(KEY_BACKSPACE, trackEcho);
                    }
                }
                first = false;
            }
            for (size_t i = 0; i < n; ++i) {
                tap(kTextKeys[i], trackEcho); // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
            }
            wl_display_roundtrip(display_);
            pos += n;
        }
        injecting_ = false;
        if (!ok) {
            echo_.clear();
            setShift(false);
            LOTUS_ERROR("virtual-keyboard: keymap upload failed");
        }
        return ok;
    }
} // namespace fcitx
