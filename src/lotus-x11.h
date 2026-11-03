/*
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file lotus-x11.h
 * @brief Thin libxcb helpers: the EWMH focused window as an application name.
 */

#ifndef _LOTUS_X11_H_
#define _LOTUS_X11_H_

#include <cstdint>
#include <memory>
#include <string>

#include <xcb/xcb.h>

#include "common-export.h"

/**
 * @brief Releases an X connection: disconnects it before the pointer dies.
 */
struct XcbConnectionDeleter {
    /**
     * @brief Disconnects the connection.
     * @param connection Connection to release, may be null.
     */
    void operator()(xcb_connection_t* connection) const noexcept;
};

using XcbConnectionPtr = std::unique_ptr<xcb_connection_t, XcbConnectionDeleter>; ///< Owning X connection

/**
 * @brief Releases an xcb_intern_atom reply, which libxcb hands out through malloc.
 */
struct InternAtomReplyDeleter {
    /**
     * @brief Frees the reply.
     * @param reply Reply to release, may be null.
     */
    void operator()(xcb_intern_atom_reply_t* reply) const noexcept;
};

/**
 * @brief Releases an xcb_get_property reply, which libxcb hands out through malloc.
 */
struct GetPropertyReplyDeleter {
    /**
     * @brief Frees the reply.
     * @param reply Reply to release, may be null.
     */
    void operator()(xcb_get_property_reply_t* reply) const noexcept;
};

using InternAtomReplyPtr  = std::unique_ptr<xcb_intern_atom_reply_t, InternAtomReplyDeleter>;   ///< Owning intern reply
using GetPropertyReplyPtr = std::unique_ptr<xcb_get_property_reply_t, GetPropertyReplyDeleter>; ///< Owning property reply

/**
 * @brief Releases an xcb_get_input_focus reply, which libxcb hands out through malloc.
 */
struct GetInputFocusReplyDeleter {
    /**
     * @brief Frees the reply.
     * @param reply Reply to release, may be null.
     */
    void operator()(xcb_get_input_focus_reply_t* reply) const noexcept;
};

/**
 * @brief Releases an xcb_query_tree reply, which libxcb hands out through malloc.
 */
struct QueryTreeReplyDeleter {
    /**
     * @brief Frees the reply.
     * @param reply Reply to release, may be null.
     */
    void operator()(xcb_query_tree_reply_t* reply) const noexcept;
};

using GetInputFocusReplyPtr = std::unique_ptr<xcb_get_input_focus_reply_t, GetInputFocusReplyDeleter>; ///< Owning focus reply
using QueryTreeReplyPtr     = std::unique_ptr<xcb_query_tree_reply_t, QueryTreeReplyDeleter>;          ///< Owning tree reply

/**
 * @brief Atoms resolved once per connection.
 */
struct XcbAtoms {
    xcb_atom_t activeWindow = XCB_ATOM_NONE; ///< _NET_ACTIVE_WINDOW on the root window
    xcb_atom_t windowPid    = XCB_ATOM_NONE; ///< _NET_WM_PID on a client window
    xcb_atom_t wmClass      = XCB_ATOM_NONE; ///< WM_CLASS on a client window
};

/**
 * @brief Interns a property or type name.
 * @param conn Connection to use.
 * @param name Name to intern.
 * @return Atom id, or XCB_ATOM_NONE when the reply failed.
 */
xcb_atom_t internAtom(xcb_connection_t* conn, const char* name);

/**
 * @brief Reads the first 32-bit value of a window property, whatever its type.
 * @param conn Connection to use.
 * @param window Window holding the property.
 * @param atom Property atom; XCB_ATOM_NONE yields 0.
 * @return First value, or 0 when the property is missing or not 32-bit.
 */
uint32_t firstCardinal(xcb_connection_t* conn, xcb_window_t window, xcb_atom_t atom);

/**
 * @brief Reads the WM_CLASS class name (the second NUL-terminated string) of a window.
 * @param conn Connection to use.
 * @param window Window holding WM_CLASS.
 * @return Class name, the instance name when there is only one string, or empty when the property is missing.
 */
std::string wmClassName(xcb_connection_t* conn, xcb_window_t window);

/**
 * @brief Reads the root window of screen 0.
 * @param conn Connection to use.
 * @return Root window, or XCB_WINDOW_NONE when the setup has no screen.
 */
xcb_window_t rootWindow(xcb_connection_t* conn);

/**
 * @brief Ascends one step towards the root window.
 * @param conn Connection to use.
 * @param window Window to ascend from.
 * @return Parent window, or XCB_WINDOW_NONE when it cannot be queried.
 */
xcb_window_t parentWindow(xcb_connection_t* conn, xcb_window_t window);

/**
 * @brief Reads the application name published by a single window.
 * @param conn Connection to use.
 * @param window Window to inspect.
 * @return Program name from _NET_WM_PID, else the WM_CLASS class name, else empty.
 */
std::string programForWindow(xcb_connection_t* conn, xcb_window_t window);

/**
 * @brief Resolves the application name of a window, ascending until a window publishes one.
 * @param conn Connection to use.
 * @param start Window to start from; XCB_WINDOW_NONE yields empty.
 * @return First non-empty program name on the chain to the root, or empty when none publishes one.
 */
std::string programForWindowChain(xcb_connection_t* conn, xcb_window_t start);

/**
 * @brief Reads the window holding the keyboard focus.
 * @param conn Connection to use.
 * @return Focused window, or XCB_WINDOW_NONE when the focus is PointerRoot or None.
 */
xcb_window_t focusedWindow(xcb_connection_t* conn);

/**
 * @brief Resolves the application name of the focused X window.
 * @return Program name, empty when there is no X display, no focused window, or no usable property.
 */
std::string LOTUS_EXPORT activeWindowProgram();

#endif // _LOTUS_X11_H_
