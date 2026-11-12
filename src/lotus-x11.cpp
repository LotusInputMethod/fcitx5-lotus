/*
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "lotus-x11.h"
#include "lotus-utils.h"

#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>

namespace {
    std::mutex       connectionMutex; ///< Serialises the cached connection; engine calls arrive on the fcitx main thread
    XcbConnectionPtr connection;      ///< Connection opened on first use, dropped on IO errors
    XcbAtoms         atoms;
} // namespace

void XcbConnectionDeleter::operator()(xcb_connection_t* connection) const noexcept {
    if (connection != nullptr) {
        xcb_disconnect(connection);
    }
}

void InternAtomReplyDeleter::operator()(xcb_intern_atom_reply_t* reply) const noexcept {
    std::free(reply); // NOLINT(cppcoreguidelines-no-malloc, cppcoreguidelines-owning-memory)
}

void GetPropertyReplyDeleter::operator()(xcb_get_property_reply_t* reply) const noexcept {
    std::free(reply); // NOLINT(cppcoreguidelines-no-malloc, cppcoreguidelines-owning-memory)
}

xcb_atom_t internAtom(xcb_connection_t* conn, const char* name) {
    const InternAtomReplyPtr reply(xcb_intern_atom_reply(conn, xcb_intern_atom(conn, 0, static_cast<uint16_t>(std::strlen(name)), name), nullptr));
    if (reply == nullptr) {
        return XCB_ATOM_NONE;
    }
    return reply->atom;
}

uint32_t firstCardinal(xcb_connection_t* conn, xcb_window_t window, xcb_atom_t atom) {
    if (atom == XCB_ATOM_NONE) {
        return 0;
    }
    const GetPropertyReplyPtr reply(xcb_get_property_reply(conn, xcb_get_property(conn, static_cast<uint8_t>(false), window, atom, XCB_GET_PROPERTY_TYPE_ANY, 0, 1), nullptr));
    if (reply == nullptr || reply->type == XCB_ATOM_NONE || reply->format != 32 || reply->value_len == 0) {
        return 0;
    }
    return *static_cast<uint32_t*>(xcb_get_property_value(reply.get()));
}

std::string wmClassName(xcb_connection_t* conn, xcb_window_t window) {
    if (atoms.wmClass == XCB_ATOM_NONE) {
        return {};
    }
    const GetPropertyReplyPtr reply(
        xcb_get_property_reply(conn, xcb_get_property(conn, static_cast<uint8_t>(false), window, atoms.wmClass, XCB_GET_PROPERTY_TYPE_ANY, 0, 256), nullptr));
    if (reply == nullptr || reply->type == XCB_ATOM_NONE) {
        return {};
    }
    const auto   size  = static_cast<size_t>(reply->value_len);
    const auto*  bytes = static_cast<const char*>(xcb_get_property_value(reply.get()));
    std::string  value(bytes, size);
    const size_t first = value.find('\0');
    if (first == std::string::npos) {
        return value;
    }
    const size_t second = value.find('\0', first + 1);
    if (second == std::string::npos) {
        return value.substr(0, first);
    }
    return value.substr(first + 1, second - first - 1);
}

void GetInputFocusReplyDeleter::operator()(xcb_get_input_focus_reply_t* reply) const noexcept {
    std::free(reply); // NOLINT(cppcoreguidelines-no-malloc, cppcoreguidelines-owning-memory)
}

void QueryTreeReplyDeleter::operator()(xcb_query_tree_reply_t* reply) const noexcept {
    std::free(reply); // NOLINT(cppcoreguidelines-no-malloc, cppcoreguidelines-owning-memory)
}

xcb_window_t rootWindow(xcb_connection_t* conn) {
    xcb_screen_iterator_t screens = xcb_setup_roots_iterator(xcb_get_setup(conn));
    if (screens.rem == 0) {
        return XCB_WINDOW_NONE;
    }
    return screens.data->root;
}

xcb_window_t parentWindow(xcb_connection_t* conn, xcb_window_t window) {
    const QueryTreeReplyPtr reply(xcb_query_tree_reply(conn, xcb_query_tree(conn, window), nullptr));
    if (reply == nullptr) {
        return XCB_WINDOW_NONE;
    }
    return reply->parent;
}

std::string programForWindow(xcb_connection_t* conn, xcb_window_t window) {
    const uint32_t pid = firstCardinal(conn, window, atoms.windowPid);
    if (pid > 0 && pid <= static_cast<uint32_t>(std::numeric_limits<int>::max())) {
        std::string program = programForPid(static_cast<int>(pid));
        if (!program.empty()) {
            return program;
        }
    }
    return wmClassName(conn, window);
}

std::string programForWindowChain(xcb_connection_t* conn, xcb_window_t start) {
    const xcb_window_t root = rootWindow(conn);
    if (root == XCB_WINDOW_NONE) {
        return {};
    }
    xcb_window_t current = start;
    while (current != XCB_WINDOW_NONE && current != root) {
        std::string program = programForWindow(conn, current);
        if (!program.empty()) {
            return program;
        }
        const xcb_window_t parent = parentWindow(conn, current);
        if (parent == current) {
            break;
        }
        current = parent;
    }
    return {};
}

xcb_window_t focusedWindow(xcb_connection_t* conn) {
    const GetInputFocusReplyPtr reply(xcb_get_input_focus_reply(conn, xcb_get_input_focus(conn), nullptr));
    if (reply == nullptr) {
        return XCB_WINDOW_NONE;
    }
    const xcb_window_t focus = reply->focus;
    if (focus == XCB_WINDOW_NONE || focus == XCB_INPUT_FOCUS_POINTER_ROOT) {
        return XCB_WINDOW_NONE;
    }
    return focus;
}

std::string activeWindowProgram() {
    std::scoped_lock lock(connectionMutex);
    if (!connection || xcb_connection_has_error(connection.get()) != 0) {
        connection.reset(xcb_connect(nullptr, nullptr));
        if (!connection || xcb_connection_has_error(connection.get()) != 0) {
            connection.reset();
            return {};
        }
        atoms = XcbAtoms{
            .activeWindow = internAtom(connection.get(), "_NET_ACTIVE_WINDOW"),
            .windowPid    = internAtom(connection.get(), "_NET_WM_PID"),
            .wmClass      = internAtom(connection.get(), "WM_CLASS"),
        };
    }
    xcb_connection_t*  handle = connection.get();
    std::string        result;
    const xcb_window_t root = rootWindow(handle);
    if (root != XCB_WINDOW_NONE) {
        result = programForWindowChain(handle, focusedWindow(handle));
        if (result.empty()) {
            result = programForWindowChain(handle, firstCardinal(handle, root, atoms.activeWindow));
        }
    }
    if (xcb_connection_has_error(handle) != 0) {
        connection.reset();
        result.clear();
    }
    return result;
}
