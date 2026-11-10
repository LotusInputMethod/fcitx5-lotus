/*
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "lotus-x11-keyboard.h"
#include "lotus-log.h"
#include "lotus-utils.h"

#include <fcitx-utils/utf8.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <optional>
#include <span>
#include <thread>
#include <vector>

#include <X11/keysym.h>
#include <xcb/xcb.h>
#include <xcb/xtest.h>

namespace fcitx {
    namespace {
        /// @brief Base of the Unicode keysym range (see the X11 keysym definition, 0x01000000 + codepoint).
        constexpr xcb_keysym_t kUnicodeKeysymBase = 0x01000000;
        /// @brief Upper bound of the text keycodes remapped in one burst (same bound as the wlroots backend).
        constexpr size_t kMaxTextKeycodes = 48;

        /// @brief Releases an xcb_get_keyboard_mapping reply, which libxcb hands out through malloc.
        struct KeyboardMappingReplyDeleter {
            /**
             * @brief Frees the reply.
             * @param reply Reply to release, may be null.
             */
            void operator()(xcb_get_keyboard_mapping_reply_t* reply) const noexcept {
                std::free(reply); // NOLINT(cppcoreguidelines-no-malloc, cppcoreguidelines-owning-memory)
            }
        };

        /// @brief Releases an xcb_test_get_version reply, which libxcb hands out through malloc.
        struct TestVersionReplyDeleter {
            /**
             * @brief Frees the reply.
             * @param reply Reply to release, may be null.
             */
            void operator()(xcb_test_get_version_reply_t* reply) const noexcept {
                std::free(reply); // NOLINT(cppcoreguidelines-no-malloc, cppcoreguidelines-owning-memory)
            }
        };

        /// @brief Releases an xcb_query_keymap reply, which libxcb hands out through malloc.
        struct QueryKeymapReplyDeleter {
            /**
             * @brief Frees the reply.
             * @param reply Reply to release, may be null.
             */
            void operator()(xcb_query_keymap_reply_t* reply) const noexcept {
                std::free(reply); // NOLINT(cppcoreguidelines-no-malloc, cppcoreguidelines-owning-memory)
            }
        };

        using KeyboardMappingReplyPtr = std::unique_ptr<xcb_get_keyboard_mapping_reply_t, KeyboardMappingReplyDeleter>; ///< Owning mapping reply
        using TestVersionReplyPtr     = std::unique_ptr<xcb_test_get_version_reply_t, TestVersionReplyDeleter>;         ///< Owning XTEST version reply
        using QueryKeymapReplyPtr     = std::unique_ptr<xcb_query_keymap_reply_t, QueryKeymapReplyDeleter>;             ///< Owning keymap reply

        /// @brief True when the keymap bit of @p keycode is set, i.e. that key is currently pressed.
        bool keyPressed(const xcb_query_keymap_reply_t& reply, xcb_keycode_t keycode) {
            const std::span<const uint8_t> keys(reply.keys, sizeof(reply.keys));
            const size_t                   index = keycode / 8U;
            return index < keys.size() && (keys[index] & (1U << (keycode % 8U))) != 0;
        }

        /// @brief The server keyboard mapping of the keycode range.
        struct KeyboardMap {
            xcb_keycode_t              minKeycode        = 0;
            xcb_keycode_t              maxKeycode        = 0;
            uint8_t                    keysymsPerKeycode = 0;
            std::vector<xcb_keysym_t>  keysyms;

            [[nodiscard]] xcb_keysym_t at(xcb_keycode_t keycode, uint8_t level) const {
                const size_t base = (static_cast<size_t>(keycode - minKeycode) * keysymsPerKeycode) + level;
                return base < keysyms.size() ? keysyms[base] : XCB_NO_SYMBOL;
            }
        };

        KeyboardMap readKeyboardMap(xcb_connection_t* conn) {
            KeyboardMap                   map;
            const xcb_setup_t*            setup = xcb_get_setup(conn);
            const auto                    count = static_cast<uint8_t>(setup->max_keycode - setup->min_keycode + 1);
            const KeyboardMappingReplyPtr reply(xcb_get_keyboard_mapping_reply(conn, xcb_get_keyboard_mapping(conn, setup->min_keycode, count), nullptr));
            if (!reply) {
                return map;
            }
            map.minKeycode        = setup->min_keycode;
            map.maxKeycode        = setup->max_keycode;
            map.keysymsPerKeycode = reply->keysyms_per_keycode;
            const std::span<const xcb_keysym_t> keysyms(xcb_get_keyboard_mapping_keysyms(reply.get()), static_cast<size_t>(xcb_get_keyboard_mapping_keysyms_length(reply.get())));
            map.keysyms.assign(keysyms.begin(), keysyms.end());
            return map;
        }

        std::optional<xcb_keycode_t> findKeycode(const KeyboardMap& map, xcb_keysym_t keysym) {
            for (uint32_t keycode = map.minKeycode; keycode <= map.maxKeycode; ++keycode) {
                for (uint8_t level = 0; level < map.keysymsPerKeycode; ++level) {
                    if (map.at(static_cast<xcb_keycode_t>(keycode), level) == keysym) {
                        return static_cast<xcb_keycode_t>(keycode);
                    }
                }
            }
            return std::nullopt;
        }

        std::vector<xcb_keycode_t> spareKeycodes(const KeyboardMap& map, size_t limit) {
            std::vector<xcb_keycode_t> spares;
            for (uint32_t keycode = map.minKeycode; keycode <= map.maxKeycode && spares.size() < limit; ++keycode) {
                bool empty = true;
                for (uint8_t level = 0; level < map.keysymsPerKeycode && empty; ++level) {
                    empty = map.at(static_cast<xcb_keycode_t>(keycode), level) == XCB_NO_SYMBOL;
                }
                if (empty) {
                    spares.push_back(static_cast<xcb_keycode_t>(keycode));
                }
            }
            return spares;
        }

        std::vector<xcb_keysym_t> keycodeKeysyms(const KeyboardMap& map, xcb_keycode_t keycode) {
            std::vector<xcb_keysym_t> keysyms(map.keysymsPerKeycode, XCB_NO_SYMBOL);
            for (uint8_t level = 0; level < map.keysymsPerKeycode; ++level) {
                keysyms[level] = map.at(keycode, level);
            }
            return keysyms;
        }

        void changeKeycode(xcb_connection_t* conn, const KeyboardMap& map, xcb_keycode_t keycode, const std::vector<xcb_keysym_t>& keysyms) {
            xcb_change_keyboard_mapping(conn, 1, keycode, map.keysymsPerKeycode, keysyms.data());
        }

        void tapKey(xcb_connection_t* conn, xcb_keycode_t keycode, int intervalMs) {
            xcb_test_fake_input(conn, XCB_KEY_PRESS, keycode, 0, XCB_NONE, 0, 0, 0);
            xcb_test_fake_input(conn, XCB_KEY_RELEASE, keycode, 0, XCB_NONE, 0, 0, 0);
            xcb_flush(conn);
            std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
        }
    } // namespace

    LotusX11Keyboard& LotusX11Keyboard::instance() {
        static LotusX11Keyboard k;
        return k;
    }

    LotusX11Keyboard::~LotusX11Keyboard() {
        cancelPendingTasks();
    }

    bool LotusX11Keyboard::available() {
        if (probed_) {
            return supported_;
        }
        probed_ = true;
        if (getEnv("DISPLAY").empty()) {
            supported_ = false;
            LOTUS_INFO("no DISPLAY, x11-keyboard unavailable, using uinput");
            return supported_;
        }
        const XcbConnectionPtr probe(xcb_connect(nullptr, nullptr));
        if (!probe || xcb_connection_has_error(probe.get()) != 0) {
            supported_ = false;
            LOTUS_INFO("cannot open the X display, x11-keyboard unavailable, using uinput");
            return supported_;
        }
        const xcb_query_extension_reply_t* test = xcb_get_extension_data(probe.get(), &xcb_test_id);
        bool                               ok   = test != nullptr && test->present != 0;
        if (ok) {
            const TestVersionReplyPtr reply(xcb_test_get_version_reply(probe.get(), xcb_test_get_version(probe.get(), 2, 2), nullptr));
            ok = reply != nullptr;
        }
        supported_ = ok;
        if (supported_) {
            LOTUS_INFO("XTEST is available, using x11-keyboard");
        } else {
            LOTUS_INFO("XTEST is unavailable, using uinput");
        }
        return supported_;
    }

    bool LotusX11Keyboard::shiftHeld() {
        if (getEnv("DISPLAY").empty()) {
            // No X session: Shift cannot be held on the X keyboard.
            return false;
        }
        const XcbConnectionPtr conn(xcb_connect(nullptr, nullptr));
        if (!conn || xcb_connection_has_error(conn.get()) != 0) {
            // Fail safe: behave like the uinput server's unknown physical state.
            return true;
        }
        const KeyboardMap map        = readKeyboardMap(conn.get());
        const auto        leftShift  = findKeycode(map, XK_Shift_L);
        const auto        rightShift = findKeycode(map, XK_Shift_R);
        if (map.keysyms.empty() || (!leftShift && !rightShift)) {
            return true;
        }
        const QueryKeymapReplyPtr reply(xcb_query_keymap_reply(conn.get(), xcb_query_keymap(conn.get()), nullptr));
        if (!reply) {
            return true;
        }
        return (leftShift && keyPressed(*reply, *leftShift)) || (rightShift && keyPressed(*reply, *rightShift));
    }

    xcb_connection_t* LotusX11Keyboard::ensureConnection() {
        std::scoped_lock lock(connectionMutex_);
        if (connection_ && xcb_connection_has_error(connection_.get()) == 0) {
            return connection_.get();
        }
        connection_.reset();
        connection_.reset(xcb_connect(nullptr, nullptr));
        if (!connection_ || xcb_connection_has_error(connection_.get()) != 0) {
            connection_.reset();
            return nullptr;
        }
        return connection_.get();
    }

    void LotusX11Keyboard::cancelPendingTasks() {
        cancelled_.store(true, std::memory_order_release);
        runner_.cancelPendingTasks();
        // The worker is joined, so the connection can be released from this thread.
        std::scoped_lock lock(connectionMutex_);
        connection_.reset();
    }

    bool LotusX11Keyboard::emit(int backspaces, std::string_view utf8, int intervalMs, bool select) {
        if (!available()) {
            return false;
        }
        backspaces = std::max(backspaces, 0);
        intervalMs = std::max(intervalMs, 1);
        std::vector<uint32_t> cps;
        if (!utf8.empty()) {
            if (!utf8::validate(utf8)) {
                LOTUS_ERROR("x11-keyboard: invalid UTF-8");
                return false;
            }
            for (auto cp : utf8::MakeUTF8CharRange(utf8)) {
                cps.push_back(static_cast<uint32_t>(cp));
            }
        }
        if (backspaces == 0 && cps.empty()) {
            return true;
        }
        cancelled_.store(false, std::memory_order_release);
        runner_.runAfter(0, [this, backspaces, cps = std::move(cps), intervalMs, select] { runBurst(backspaces, cps, intervalMs, select); });
        return true;
    }

    void LotusX11Keyboard::runBurst(int backspaces, const std::vector<uint32_t>& cps, int intervalMs, bool select) {
        xcb_connection_t* conn = ensureConnection();
        if (conn == nullptr) {
            LOTUS_ERROR("x11-keyboard: cannot connect to the X server");
            return;
        }
        const auto stopped           = [this] { return cancelled_.load(std::memory_order_acquire); };
        const auto releaseConnection = [this] {
            std::scoped_lock lock(connectionMutex_);
            connection_.reset();
        };

        const KeyboardMap map = readKeyboardMap(conn);
        if (map.keysyms.empty() || map.keysymsPerKeycode == 0) {
            LOTUS_ERROR("x11-keyboard: cannot read the keyboard mapping");
            if (xcb_connection_has_error(conn) != 0) {
                releaseConnection();
            }
            return;
        }
        const auto backspaceKey = findKeycode(map, XK_BackSpace);
        const auto leftKey      = findKeycode(map, XK_Left);
        const auto deleteKey    = findKeycode(map, XK_Delete);
        const auto shiftKey     = findKeycode(map, XK_Shift_L);
        if (!backspaceKey || (select && (!leftKey || !deleteKey || !shiftKey))) {
            LOTUS_ERROR("x11-keyboard: standard keysyms are missing from the keymap");
            return;
        }

        if (select) {
            // Shift must stay down for the whole Left run, so it is pressed once and always released.
            xcb_test_fake_input(conn, XCB_KEY_PRESS, *shiftKey, 0, XCB_NONE, 0, 0, 0);
            xcb_flush(conn);
            std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
            for (int i = 0; i < backspaces && !stopped(); ++i) {
                xcb_test_fake_input(conn, XCB_KEY_PRESS, *leftKey, 0, XCB_NONE, 0, 0, 0);
                xcb_test_fake_input(conn, XCB_KEY_RELEASE, *leftKey, 0, XCB_NONE, 0, 0, 0);
                xcb_flush(conn);
                std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
            }
            xcb_test_fake_input(conn, XCB_KEY_RELEASE, *shiftKey, 0, XCB_NONE, 0, 0, 0);
            xcb_flush(conn);
            if (!cps.empty() && !stopped()) {
                tapKey(conn, *deleteKey, intervalMs);
            }
        } else {
            for (int i = 0; i < backspaces && !stopped(); ++i) {
                tapKey(conn, *backspaceKey, intervalMs);
            }
        }

        const std::vector<xcb_keycode_t> spares = spareKeycodes(map, kMaxTextKeycodes);
        if (!cps.empty() && spares.empty()) {
            LOTUS_ERROR("x11-keyboard: no spare keycode for the text");
            return;
        }
        size_t pos = 0;
        while (pos < cps.size() && !stopped()) {
            const size_t chunk = std::min(spares.size(), cps.size() - pos);
            for (size_t i = 0; i < chunk && !stopped(); ++i) {
                const xcb_keycode_t             keycode  = spares[i];
                const std::vector<xcb_keysym_t> original = keycodeKeysyms(map, keycode);
                std::vector<xcb_keysym_t>       keysyms(map.keysymsPerKeycode, XCB_NO_SYMBOL);
                keysyms[0] = kUnicodeKeysymBase | cps[pos + i];
                changeKeycode(conn, map, keycode, keysyms);
                xcb_flush(conn);
                tapKey(conn, keycode, intervalMs);
                changeKeycode(conn, map, keycode, original);
                xcb_flush(conn);
            }
            pos += chunk;
        }

        if (xcb_connection_has_error(conn) != 0) {
            LOTUS_ERROR("x11-keyboard: X connection lost during the burst");
            releaseConnection();
        }
    }

} // namespace fcitx
