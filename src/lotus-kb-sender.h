/*
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */
/**
 * @file lotus-kb-sender.h
 * @brief Sole owner of the addon side uinput keyboard socket.
 */

#ifndef _LOTUS_KB_SENDER_H_
#define _LOTUS_KB_SENDER_H_

#include "common-export.h"
#include "common-fd-guard.h"
#include "common-protocol.h"

#include <mutex>
#include <string>

/**
 * @brief Thread-safe sender for keyboard requests to the uinput server.
 */
class LOTUS_EXPORT KbSender {
  public:
    /**
     * @brief Builds a sender for the given abstract socket path.
     * @param socketPath Socket path built by buildSocketPath("kbSocket").
     */
    explicit KbSender(std::string socketPath);

    /**
     * @brief Closes the socket.
     */
    ~KbSender();

    KbSender(const KbSender&)            = delete;
    KbSender& operator=(const KbSender&) = delete;
    KbSender(KbSender&&)                 = delete;
    KbSender& operator=(KbSender&&)      = delete;

    /**
     * @brief Checks whether the socket is currently open.
     * @return True when a descriptor is held.
     */
    [[nodiscard]] bool connected() const;

    /**
     * @brief Opens the socket when it is not open yet.
     * @return True when a usable descriptor is held.
     */
    bool ensureConnected();

    /**
     * @brief Sends one request, reconnecting once when the socket was closed by the server.
     * @param msg Request to send.
     * @return True when the whole message was written.
     */
    bool send(const KbMsg& msg);

    /**
     * @brief Closes the socket and drops its descriptor.
     */
    void close();

  private:
    /**
     * @brief Opens the abstract socket.
     * @return True on success.
     */
    bool               connectLocked();

    std::string        socketPath_;
    mutable std::mutex mutex_;
    FdGuard            fd_;
};

#endif // _LOTUS_KB_SENDER_H_
