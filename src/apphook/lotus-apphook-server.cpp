/*
 * SPDX-FileCopyrightText: 2026 Kazoku <k4zoku@disr.it>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

/**
 * @file lotus-apphook-server.cpp
 * @brief Receives application-hook registrations and dispatches keyboard requests.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "lotus-apphook-server.h"
#include "lotus-apphook-fd.h"
#include "lotus-apphook-protocol.h"
#include "lotus-log.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <poll.h>
#include <string>
#include <utility>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

namespace fcitx {
    namespace {
        using namespace lotus::apphook;

        /** @brief Rejects peers that are not applications owned by the current user. */
        bool validPeer(int fd, pid_t& pid) {
            ucred     peer{};
            socklen_t length = sizeof(peer);
            if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &length) != 0 || length != sizeof(peer) || peer.uid != getuid() || peer.pid <= 0 || peer.pid == getpid()) {
                return false;
            }
            pid = peer.pid;
            return true;
        }

        // A zero stamp comes from an older hook: retain its arrival-order behavior.
        bool focusIsCurrent(uint32_t candidate, uint32_t current) {
            return candidate == 0U || current == 0U || static_cast<int32_t>(candidate - current) >= 0;
        }
    } // namespace

    LotusAppHook::LotusAppHook() : stopFd_(eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)), server_(&LotusAppHook::serve, this) {
        for (auto& worker : workers_)
            worker = std::thread(&LotusAppHook::run, this);
    }

    LotusAppHook::~LotusAppHook() {
        shutdown();
    }

    void LotusAppHook::shutdown() {
        std::deque<Work> abandoned;
        {
            std::scoped_lock lock(mutex_);
            if (stopping_.exchange(true)) {
                return;
            }
            abandoned.swap(queue_);
            for (auto& [pid, session] : sessions_)
                disconnect(session);
            for (const auto& request : activeRequests_)
                cancelled_.insert(request);
        }
        condition_.notify_all();
        if (stopFd_.isValid()) {
            const uint64_t one = 1;
            // EAGAIN means the eventfd is already signalled.
            (void)retryOnEintr([&] { return write(stopFd_.get(), &one, sizeof(one)); });
        }
        for (auto& work : abandoned)
            if (work.completion)
                work.completion(-ECANCELED);
        if (server_.joinable())
            server_.join();
        for (auto& worker : workers_)
            if (worker.joinable())
                worker.join();
        {
            std::scoped_lock lock(mutex_);
            sessions_.clear();
            focusedPid_   = 0;
            focusedStamp_ = 0;
        }
    }

    bool LotusAppHook::ready(pid_t pid) const {
        if (pid <= 0) {
            return false;
        }
        std::scoped_lock lock(mutex_);
        const auto       it = sessions_.find(pid);
        return it != sessions_.end() && it->second->connected.load() && it->second->ready.load() && it->second->focused.load() && focusedPid_ == pid;
    }

    pid_t LotusAppHook::focusedPid() const {
        std::scoped_lock lock(mutex_);
        const auto       it = sessions_.find(focusedPid_);
        return it != sessions_.end() && it->second->connected.load() && it->second->ready.load() && it->second->focused.load() ? focusedPid_ : 0;
    }

    bool LotusAppHook::emit(pid_t pid, std::vector<KeyOperation> operations, uint64_t requestId, uint32_t preDelayMs, uint32_t postDelayMs, Completion complete) {
        if (!ready(pid) || operations.empty()) {
            return false;
        }
        {
            std::scoped_lock lock(mutex_);
            if (stopping_) {
                return false;
            }
            queue_.push_back(
                {.pid = pid, .operations = std::move(operations), .requestId = requestId, .preDelayMs = preDelayMs, .postDelayMs = postDelayMs, .completion = std::move(complete)});
        }
        condition_.notify_one();
        return true;
    }

    int LotusAppHook::sendRequest(const std::shared_ptr<Session>& session, const Request& request, size_t size) {
        std::scoped_lock lock(session->sendMutex);
        if (!session->connected.load()) {
            return -ENOTCONN;
        }
        const ssize_t sent = send(session->fd.get(), &request, size, MSG_NOSIGNAL | MSG_DONTWAIT);
        if (std::cmp_not_equal(sent, size)) {
            return sent < 0 ? -errno : -EIO;
        }
        return 0;
    }

    void LotusAppHook::disconnect(const std::shared_ptr<Session>& session) {
        session->connected.store(false);
        session->ready.store(false);
        session->focused.store(false);
        session->completed.notify_all();
    }

    int LotusAppHook::cancel(pid_t pid, uint64_t requestId) {
        Completion               completion;
        std::shared_ptr<Session> session;
        bool                     removed = false;
        bool                     active  = false;
        {
            std::scoped_lock lock(mutex_);
            const RequestKey key{.pid = pid, .requestId = requestId};
            const auto       it = std::ranges::find_if(queue_, [&key](const Work& item) { return item.pid == key.pid && item.requestId == key.requestId; });
            if (it != queue_.end()) {
                completion = std::move(it->completion);
                queue_.erase(it);
                removed = true;
            } else if (activeRequests_.contains(key)) {
                cancelled_.insert(key);
                active           = true;
                const auto found = sessions_.find(pid);
                if (found != sessions_.end())
                    session = found->second;
            }
        }
        condition_.notify_all();
        if (removed) {
            if (completion) {
                completion(-ECANCELED);
            }
            return 0;
        }
        if (!active) {
            return -ENOENT;
        }
        if (session) {
            Request request{};
            request.type      = Command::Cancel;
            request.requestId = requestId;
            (void)sendRequest(session, request, kRequestHeaderSize);
        }
        return 0;
    }

    int LotusAppHook::sendKeys(const Work& work) {
        std::shared_ptr<Session> session;
        {
            std::scoped_lock lock(mutex_);
            const auto       it = sessions_.find(work.pid);
            if (it == sessions_.end() || focusedPid_ != work.pid) {
                return -ENOTCONN;
            }
            session = it->second;
        }
        if (!session->connected.load() || !session->ready.load() || !session->focused.load() || work.operations.size() > kMaxOps) {
            return -ENOTCONN;
        }

        Request request{};
        request.type      = Command::Emit;
        request.requestId = work.requestId;
        request.count     = static_cast<uint32_t>(work.operations.size());
        uint64_t taps     = 0;
        for (const auto& op : work.operations) {
            taps += op.count;
            if ((op.count == 0U) || op.modifiers > 15 || taps > kMaxTaps) {
                return -EINVAL;
            }
        }
        // Bounded above: operations.size() <= kMaxOps.
        std::ranges::copy(work.operations, request.ops.begin());

        {
            std::scoped_lock lock(session->mutex);
            session->waitingId = work.requestId;
            session->hasReply  = false;
        }
        const int sent = sendRequest(session, request, kRequestHeaderSize + (request.count * sizeof(KeyOperation)));
        if (sent != 0) {
            std::scoped_lock lock(session->mutex);
            session->waitingId = 0;
            return sent;
        }
        // Close the race where cancellation reaches the target before Emit.
        bool cancelled = false;
        {
            std::scoped_lock lock(mutex_);
            cancelled = cancelled_.contains({.pid = work.pid, .requestId = work.requestId});
        }
        if (cancelled) {
            Request cancel{};
            cancel.type      = Command::Cancel;
            cancel.requestId = work.requestId;
            (void)sendRequest(session, cancel, kRequestHeaderSize);
        }

        std::unique_lock lock(session->mutex);
        const bool done = session->completed.wait_for(lock, std::chrono::milliseconds(3000), [&] { return session->hasReply || !session->connected.load() || stopping_.load(); });
        // Once Emit was accepted by the socket, neither a timeout nor a lost
        // connection proves that no keys reached the application.
        const int status   = session->hasReply ? session->result : -EINPROGRESS;
        session->waitingId = 0;
        lock.unlock();
        if (!done || status == -EINPROGRESS) {
            Request cancel{};
            cancel.type      = Command::Cancel;
            cancel.requestId = work.requestId;
            (void)sendRequest(session, cancel, kRequestHeaderSize);
        }
        return status;
    }

    void LotusAppHook::run() {
        const auto waitForDelay = [this](const Work& work, uint32_t delayMs) {
            std::unique_lock lock(mutex_);
            const RequestKey key{.pid = work.pid, .requestId = work.requestId};
            return !condition_.wait_for(lock, std::chrono::milliseconds(delayMs), [&] { return stopping_ || cancelled_.contains(key); });
        };

        while (true) {
            Work work{};
            {
                std::unique_lock lock(mutex_);
                // An unresponsive application must not block requests to other
                // PIDs. Only one burst per PID may run at once.
                const auto available = [&] { return std::ranges::find_if(queue_, [&](const Work& item) { return !activePids_.contains(item.pid); }); };
                condition_.wait(lock, [&] { return stopping_ || available() != queue_.end(); });
                if (stopping_)
                    return;
                auto it = available();
                work    = std::move(*it);
                queue_.erase(it);
                activePids_.insert(work.pid);
                activeRequests_.insert({.pid = work.pid, .requestId = work.requestId});
            }

            int        result         = -ECANCELED;
            const bool delayCompleted = work.preDelayMs == 0 || waitForDelay(work, work.preDelayMs);
            if (delayCompleted) {
                bool cancelled = false;
                {
                    std::scoped_lock lock(mutex_);
                    cancelled = cancelled_.contains({.pid = work.pid, .requestId = work.requestId}) || stopping_.load();
                }
                if (!cancelled)
                    result = sendKeys(work);
            }
            if (result == 0 && (work.postDelayMs != 0U))
                (void)waitForDelay(work, work.postDelayMs);
            if (work.completion)
                work.completion(result);
            {
                std::scoped_lock lock(mutex_);
                cancelled_.erase({.pid = work.pid, .requestId = work.requestId});
                activeRequests_.erase({.pid = work.pid, .requestId = work.requestId});
                activePids_.erase(work.pid);
            }
            condition_.notify_all();
        }
    }

    void LotusAppHook::serve() {
        sockaddr_un address{};
        socklen_t   addressSize = 0;
        if (!socketAddress(address, addressSize)) {
            return;
        }
        FdGuard listener(socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0));
        if (listener.get() < 0) {
            LOTUS_WARN("apphook: socket failed: " << std::strerror(errno));
            return;
        }
        if (bind(listener.get(), reinterpret_cast<sockaddr*>(&address), addressSize) != 0) {
            LOTUS_WARN("apphook: bind failed: " << std::strerror(errno));
            return;
        }
        if (listen(listener.get(), 64) != 0) {
            LOTUS_WARN("apphook: listen failed: " << std::strerror(errno));
            return;
        }
        while (!stopping_) {
            std::vector<std::shared_ptr<Session>> clients;
            {
                std::scoped_lock lock(mutex_);
                clients.reserve(sessions_.size());
                for (const auto& [pid, session] : sessions_) {
                    clients.push_back(session);
                }
            }
            std::vector<pollfd> fds;
            const size_t        clientOffset = stopFd_.isValid() ? 2 : 1;
            fds.reserve(clients.size() + clientOffset);
            fds.push_back({.fd = listener.get(), .events = POLLIN, .revents = 0});
            if (stopFd_.isValid())
                fds.push_back({.fd = stopFd_.get(), .events = POLLIN, .revents = 0});
            for (const auto& session : clients)
                fds.push_back({.fd = session->fd.get(), .events = POLLIN, .revents = 0});
            // Fall back to bounded polling if eventfd creation failed.
            const int polled = poll(fds.data(), fds.size(), stopFd_.isValid() ? -1 : 200);
            if (polled < 0 && errno == EINTR)
                continue;
            if (polled < 0 || stopping_ || (stopFd_.isValid() && (fds[1].revents != 0)))
                break;
            if (hasPollEvents(fds[0].revents, POLLIN)) {
                while (true) {
                    FdGuard peer(accept4(listener.get(), nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK));
                    if (peer.get() < 0) {
                        if (errno == EINTR) {
                            continue;
                        }
                        break;
                    }
                    pid_t pid = 0;
                    if (!validPeer(peer.get(), pid)) {
                        continue;
                    }
                    std::scoped_lock lock(mutex_);
                    if (sessions_.size() >= kMaxPeers && !sessions_.contains(pid)) {
                        continue;
                    }
                    auto session = std::make_shared<Session>(peer.release(), pid);
                    auto found   = sessions_.find(pid);
                    if (found != sessions_.end()) {
                        disconnect(found->second);
                        if (focusedPid_ == pid) {
                            focusedPid_   = 0;
                            focusedStamp_ = 0;
                        }
                    }
                    sessions_[pid] = std::move(session);
                }
            }
            for (size_t i = 0; i < clients.size(); ++i) {
                const auto& session = clients[i];
                const short events  = fds[i + clientOffset].revents;
                if (events == 0) {
                    continue;
                }
                const bool peerClosed = hasPollEvents(events, POLLERR | POLLHUP | POLLNVAL);
                bool       drop       = peerClosed;
                if (hasPollEvents(events, POLLIN)) {
                    bool responseError = false;
                    // HUP can accompany several queued packets (e.g. Focus,
                    // then Complete). Drain them before disconnecting the peer.
                    bool readMore = true;
                    while (readMore) {
                        Response      response{};
                        const ssize_t got   = retryOnEintr([&] { return recv(session->fd.get(), &response, sizeof(response), MSG_DONTWAIT | MSG_TRUNC); });
                        const bool    valid = got == sizeof(response) && response.magic == kMagic && response.version == kVersion;
                        if (valid && response.type == ResponseKind::Ready && response.requestId == 0 && (response.reserved & ~kReadyFocused) == 0) {
                            {
                                std::scoped_lock lock(mutex_);
                                const auto       found = sessions_.find(session->pid);
                                if (found != sessions_.end() && found->second == session) {
                                    const bool focused = response.status == 0 && (response.reserved & kReadyFocused) != 0;
                                    session->ready.store(response.status == 0);
                                    session->focused.store(focused);
                                    session->focusStamp = 0;
                                    // A stamped Focus follows from current hooks. Do not let an
                                    // unstamped reconnect snapshot steal a newer focused process.
                                    if (focused && focusedPid_ == 0) {
                                        focusedPid_   = session->pid;
                                        focusedStamp_ = 0;
                                    } else if (!focused && focusedPid_ == session->pid) {
                                        focusedPid_   = 0;
                                        focusedStamp_ = 0;
                                    }
                                }
                            }
                            if (response.status != 0) {
                                drop          = true;
                                responseError = true;
                            }
                        } else if (valid && response.type == ResponseKind::Focus && response.requestId == 0 && (response.status == 0 || response.status == 1)) {
                            std::scoped_lock lock(mutex_);
                            const auto       found = sessions_.find(session->pid);
                            if (found != sessions_.end() && found->second == session) {
                                const bool     focused = response.status == 1;
                                const uint32_t stamp   = response.reserved;
                                session->focused.store(focused);
                                session->focusStamp = focused ? stamp : 0;
                                if (focused && (focusedPid_ == 0 || focusedPid_ == session->pid || focusIsCurrent(stamp, focusedStamp_))) {
                                    focusedPid_   = session->pid;
                                    focusedStamp_ = stamp;
                                } else if (!focused && focusedPid_ == session->pid) {
                                    focusedPid_   = 0;
                                    focusedStamp_ = 0;
                                }
                            }
                        } else if (valid && response.type == ResponseKind::Complete && response.requestId != 0) {
                            std::scoped_lock lock(session->mutex);
                            if (response.requestId == session->waitingId) {
                                session->result   = response.status != 0 && ((response.reserved & kCompleteMayHaveApplied) != 0U) ? -EINPROGRESS : response.status;
                                session->hasReply = true;
                                session->completed.notify_all();
                            }
                        } else {
                            drop          = true;
                            responseError = true;
                        }
                        readMore = peerClosed && !responseError;
                    }
                }
                if (drop) {
                    disconnect(session);
                    std::scoped_lock lock(mutex_);
                    const auto       found = sessions_.find(session->pid);
                    if (found != sessions_.end() && found->second == session) {
                        if (focusedPid_ == session->pid) {
                            focusedPid_   = 0;
                            focusedStamp_ = 0;
                        }
                        sessions_.erase(found);
                    }
                }
            }
        }
        std::scoped_lock lock(mutex_);
        for (auto& [pid, session] : sessions_) {
            disconnect(session);
        }
        sessions_.clear();
        focusedPid_   = 0;
        focusedStamp_ = 0;
    }
} // namespace fcitx
