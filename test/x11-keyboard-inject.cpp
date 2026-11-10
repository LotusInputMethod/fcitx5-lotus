// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file x11-keyboard-inject.cpp
 * @brief Checks the XTEST backend: key order, keysym mapping, mandatory interval and keymap restore.
 */

#include "lotus-x11-keyboard.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <thread>

#include <X11/keysym.h>
#include <xcb/xcb.h>
#include <xcb/xtest.h>

namespace {
    int  failures = 0;

    void check(const char* name, bool condition) {
        if (!condition) {
            std::printf("FAIL %s\n", name);
            ++failures;
        }
    }

    xcb_keycode_t keycodeFor(xcb_connection_t* conn, xcb_keysym_t keysym) {
        const xcb_setup_t*                setup = xcb_get_setup(conn);
        xcb_get_keyboard_mapping_reply_t* reply =
            xcb_get_keyboard_mapping_reply(conn, xcb_get_keyboard_mapping(conn, setup->min_keycode, static_cast<uint8_t>(setup->max_keycode - setup->min_keycode + 1)), nullptr);
        if (reply == nullptr) {
            return XCB_NO_SYMBOL;
        }
        const std::span<const xcb_keysym_t> keysyms(xcb_get_keyboard_mapping_keysyms(reply), static_cast<size_t>(xcb_get_keyboard_mapping_keysyms_length(reply)));
        xcb_keycode_t                       found = XCB_NO_SYMBOL;
        for (uint32_t keycode = setup->min_keycode; keycode <= setup->max_keycode && found == XCB_NO_SYMBOL; ++keycode) {
            for (uint8_t level = 0; level < reply->keysyms_per_keycode; ++level) {
                if (keysyms[((keycode - setup->min_keycode) * reply->keysyms_per_keycode) + level] == keysym) {
                    found = static_cast<xcb_keycode_t>(keycode);
                    break;
                }
            }
        }
        std::free(reply); // NOLINT(cppcoreguidelines-no-malloc, cppcoreguidelines-owning-memory)
        return found;
    }

    xcb_keysym_t level0(xcb_connection_t* conn, xcb_keycode_t keycode) {
        xcb_get_keyboard_mapping_reply_t* reply = xcb_get_keyboard_mapping_reply(conn, xcb_get_keyboard_mapping(conn, keycode, 1), nullptr);
        if (reply == nullptr) {
            return XCB_NO_SYMBOL;
        }
        const std::span<const xcb_keysym_t> keysyms(xcb_get_keyboard_mapping_keysyms(reply), static_cast<size_t>(xcb_get_keyboard_mapping_keysyms_length(reply)));
        const xcb_keysym_t                  keysym = keysyms.empty() ? XCB_NO_SYMBOL : keysyms.front();
        std::free(reply); // NOLINT(cppcoreguidelines-no-malloc, cppcoreguidelines-owning-memory)
        return keysym;
    }

    xcb_key_press_event_t* waitKeyPress(xcb_connection_t* conn, int timeoutMs) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < deadline) {
            xcb_generic_event_t* event = xcb_poll_for_event(conn);
            if (event == nullptr) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                continue;
            }
            if (static_cast<uint8_t>(event->response_type & 0x7fU) == XCB_KEY_PRESS) {
                return reinterpret_cast<xcb_key_press_event_t*>(event);
            }
            std::free(event); // NOLINT(cppcoreguidelines-no-malloc, cppcoreguidelines-owning-memory)
        }
        return nullptr;
    }

    void sync(xcb_connection_t* conn) {
        std::free(xcb_get_input_focus_reply(conn, xcb_get_input_focus(conn), nullptr)); // NOLINT(cppcoreguidelines-no-malloc, cppcoreguidelines-owning-memory)
    }
} // namespace

int main() {
    const char* display = std::getenv("DISPLAY");
    if (std::getenv("LOTUS_X11_KEYBOARD_TEST") == nullptr || display == nullptr || *display == '\0') {
        std::printf("skipped: no X display\n");
        return 0;
    }

    xcb_connection_t* conn = xcb_connect(nullptr, nullptr);
    if (conn == nullptr || xcb_connection_has_error(conn) != 0) {
        std::printf("skipped: cannot open the X display\n");
        if (conn != nullptr) {
            xcb_disconnect(conn);
        }
        return 0;
    }

    xcb_screen_iterator_t screens = xcb_setup_roots_iterator(xcb_get_setup(conn));
    if (screens.rem == 0) {
        std::printf("skipped: no X screen\n");
        xcb_disconnect(conn);
        return 0;
    }
    const xcb_window_t root   = screens.data->root;
    const xcb_window_t window = xcb_generate_id(conn);
    xcb_create_window(conn, XCB_COPY_FROM_PARENT, window, root, 0, 0, 1, 1, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, XCB_COPY_FROM_PARENT, 0, nullptr);
    xcb_map_window(conn, window);
    const uint32_t mask = XCB_EVENT_MASK_KEY_PRESS;
    xcb_change_window_attributes(conn, window, XCB_CW_EVENT_MASK, &mask);
    xcb_flush(conn);
    // The window must be viewable before it can take the input focus.
    sync(conn);
    xcb_set_input_focus(conn, XCB_INPUT_FOCUS_PARENT, window, XCB_CURRENT_TIME);
    xcb_flush(conn);

    const xcb_keycode_t backspaceKey = keycodeFor(conn, XK_BackSpace);
    check("XTEST backend is available", fcitx::LotusX11Keyboard::instance().available());

    constexpr int kIntervalMs = 200;
    const auto    started     = std::chrono::steady_clock::now();
    check("emit schedules the burst", fcitx::LotusX11Keyboard::instance().emit(1, "ế", kIntervalMs, false));

    xcb_keycode_t textKeycode = XCB_NO_SYMBOL;
    if (xcb_key_press_event_t* first = waitKeyPress(conn, 3000); first != nullptr) {
        check("first injected key is a BackSpace", first->detail == backspaceKey);
        std::free(first); // NOLINT(cppcoreguidelines-no-malloc, cppcoreguidelines-owning-memory)
    } else {
        check("first injected key is a BackSpace", false);
    }
    if (xcb_key_press_event_t* second = waitKeyPress(conn, 3000); second != nullptr) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
        check("second injected key is the text key", second->detail != backspaceKey);
        check("the interval between the keys is honoured", elapsed >= kIntervalMs - 10);
        textKeycode = second->detail;
        check("the text key carries the Unicode keysym of ế", level0(conn, textKeycode) == (0x01000000U | 0x1EBFU));
        std::free(second); // NOLINT(cppcoreguidelines-no-malloc, cppcoreguidelines-owning-memory)
    } else {
        check("second injected key is the text key", false);
    }

    if (textKeycode != XCB_NO_SYMBOL) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2000);
        while (level0(conn, textKeycode) != XCB_NO_SYMBOL && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        check("the spare keycode is restored after the burst", level0(conn, textKeycode) == XCB_NO_SYMBOL);
    }

    const xcb_keycode_t shiftKey = keycodeFor(conn, XK_Shift_L);
    check("Shift is not held at first", !fcitx::LotusX11Keyboard::shiftHeld());
    check("the keymap publishes a Shift keycode", shiftKey != XCB_NO_SYMBOL);
    if (shiftKey != XCB_NO_SYMBOL) {
        xcb_test_fake_input(conn, XCB_KEY_PRESS, shiftKey, 0, XCB_NONE, 0, 0, 0);
        xcb_flush(conn);
        sync(conn);
        check("a held Shift is reported", fcitx::LotusX11Keyboard::shiftHeld());
        xcb_test_fake_input(conn, XCB_KEY_RELEASE, shiftKey, 0, XCB_NONE, 0, 0, 0);
        xcb_flush(conn);
        sync(conn);
        check("a released Shift is not reported", !fcitx::LotusX11Keyboard::shiftHeld());
    }

    xcb_destroy_window(conn, window);
    xcb_disconnect(conn);

    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("lotus x11 keyboard: all checks passed\n");
    return 0;
}
