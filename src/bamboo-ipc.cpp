/*
 * SPDX-FileCopyrightText: 2022-2022 CSSlayer <wengxt@gmail.com>
 * SPDX-FileCopyrightText: 2026-2026 Nguyen Hoong Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

/**
 * @file bamboo-ipc.cpp
 * @brief Unix-socket client replacing the in-process CGo bridge.
 *
 * lotus.so used to link the Go engine as a c-archive, which dragged a matching
 * libc into every build and made musl painful. This file keeps the old C API
 * (bamboo-core.h) and forwards every call to the lotus-engine-helper process
 * instead: spawn it with a pipe on stdout, read back one abstract-socket name,
 * connect, handshake, then keep that single connection for the addon's
 * lifetime; if the helper dies mid-session the next call respawns it once.
 * The helper path resolves from $LOTUS_ENGINE_HELPER_BIN, then the compile-time
 * LOTUS_ENGINE_HELPER_EXECUTABLE, then $PATH. POSIX + C++ stdlib only, so this
 * file links wherever the addon builds, glibc or musl alike.
 */

#include "bamboo-core.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>
#include <vector>

extern char** environ;

namespace {

    // Wire protocol shared with bamboo/protocol.go; keep both in sync.
    constexpr uint8_t  kOpHello         = 1;
    constexpr uint8_t  kOpPing          = 2;
    constexpr uint8_t  kOpNewEngine     = 3;
    constexpr uint8_t  kOpNewCustom     = 4;
    constexpr uint8_t  kOpNewMacroTable = 5;
    constexpr uint8_t  kOpNewDictionary = 6;
    constexpr uint8_t  kOpDeleteObject  = 7;
    constexpr uint8_t  kOpProcessKey    = 8;
    constexpr uint8_t  kOpPullPreedit   = 9;
    constexpr uint8_t  kOpCommitPreedit = 10;
    constexpr uint8_t  kOpPullCommit    = 11;
    constexpr uint8_t  kOpSetMacro      = 12;
    constexpr uint8_t  kOpSetOption     = 13;
    constexpr uint8_t  kOpResetEngine   = 14;
    constexpr uint8_t  kOpRebuildText   = 15;
    constexpr uint8_t  kOpGetCharsets   = 16;
    constexpr uint8_t  kOpGetIMNames    = 17;

    constexpr uint8_t  kStatusOk    = 0;
    constexpr uint8_t  kStatusError = 1;

    constexpr uint32_t kProtocolVersion = 1;
    constexpr uint32_t kMaxMessageSize  = 64u << 20;

    // pipe + spawn + read name + connect + handshake must not hang the main loop.
    constexpr int             kSpawnTimeoutMs  = 5000;
    constexpr int             kRequestTimeoutS = 2;

    [[nodiscard]] std::string logPrefix() {
        return "fcitx5-lotus: bamboo-ipc: ";
    }

    // ---- little-endian wire codec ----

    void putU32(std::vector<uint8_t>& buf, uint32_t v) {
        buf.push_back(static_cast<uint8_t>(v));
        buf.push_back(static_cast<uint8_t>(v >> 8));
        buf.push_back(static_cast<uint8_t>(v >> 16));
        buf.push_back(static_cast<uint8_t>(v >> 24));
    }

    void putU64(std::vector<uint8_t>& buf, uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            buf.push_back(static_cast<uint8_t>(v >> (8 * i)));
        }
    }

    [[nodiscard]] uint32_t getU32(const uint8_t* p) {
        return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
    }

    [[nodiscard]] uint64_t getU64(const uint8_t* p) {
        uint64_t v = 0;
        for (int i = 7; i >= 0; --i) {
            v = (v << 8) | p[i];
        }
        return v;
    }

    void putString(std::vector<uint8_t>& buf, const char* s) {
        const size_t len = (s != nullptr) ? std::strlen(s) : 0;
        putU32(buf, static_cast<uint32_t>(len));
        if (len != 0) {
            buf.insert(buf.end(), s, s + len);
        }
    }

    void putBool(std::vector<uint8_t>& buf, bool v) {
        buf.push_back(v ? 1 : 0);
    }

    /** Decodes [u32 len][bytes] and returns a malloc'ed NUL terminated copy. */
    [[nodiscard]] char* takeString(const std::vector<uint8_t>& payload, size_t& pos) {
        if (pos + 4 > payload.size()) {
            return nullptr;
        }
        const uint32_t len = getU32(payload.data() + pos);
        pos += 4;
        if (pos + len > payload.size()) {
            return nullptr;
        }
        // NOLINTNEXTLINE(cppcoreguidelines-no-malloc)
        char* out = static_cast<char*>(malloc(static_cast<size_t>(len) + 1));
        if (out == nullptr) {
            return nullptr;
        }
        if (len != 0) {
            std::memcpy(out, payload.data() + pos, len);
        }
        out[len] = '\0';
        pos += len;
        return out;
    }

    /** Decodes [u32 count][string...] into a NULL terminated malloc'ed array. */
    [[nodiscard]] char** takeStringList(const std::vector<uint8_t>& payload) {
        size_t pos = 0;
        if (pos + 4 > payload.size()) {
            return nullptr;
        }
        const uint32_t count = getU32(payload.data() + pos);
        pos += 4;
        // NOLINTNEXTLINE(cppcoreguidelines-no-malloc)
        char** list = static_cast<char**>(malloc(sizeof(char*) * (static_cast<size_t>(count) + 1)));
        if (list == nullptr) {
            return nullptr;
        }
        for (uint32_t i = 0; i < count; ++i) {
            list[i] = takeString(payload, pos);
            if (list[i] == nullptr) {
                for (uint32_t j = 0; j < i; ++j) {
                    free(list[j]); //NOLINT(cppcoreguidelines-no-malloc)
                }
                free(list); //NOLINT(cppcoreguidelines-no-malloc)
                return nullptr;
            }
        }
        list[count] = nullptr;
        return list;
    }

    /** Encodes a NULL terminated key/value array the way decodePairs expects. */
    [[nodiscard]] std::vector<uint8_t> encodePairs(char* const* definition) {
        std::vector<uint8_t> buf;
        uint32_t             count = 0;
        if (definition != nullptr) {
            for (size_t i = 0; definition[i] != nullptr && definition[i + 1] != nullptr; i += 2) {
                ++count;
            }
        }
        putU32(buf, count);
        if (definition != nullptr) {
            for (size_t i = 0; definition[i] != nullptr && definition[i + 1] != nullptr; i += 2) {
                putString(buf, definition[i]);
                putString(buf, definition[i + 1]);
            }
        }
        return buf;
    }

    // ---- blocking I/O with EINTR / timeout handling ----

    [[nodiscard]] bool sendAll(int fd, const void* data, size_t len) {
        const auto* p    = static_cast<const uint8_t*>(data);
        size_t      sent = 0;
        while (sent < len) {
            const ssize_t n = ::send(fd, p + sent, len - sent, MSG_NOSIGNAL);
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return false;
            }
            sent += static_cast<size_t>(n);
        }
        return true;
    }

    [[nodiscard]] bool recvAll(int fd, void* data, size_t len) {
        auto*  p        = static_cast<uint8_t*>(data);
        size_t received = 0;
        while (received < len) {
            const ssize_t n = ::recv(fd, p + received, len - received, 0);
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return false;
            }
            if (n == 0) {
                return false;
            }
            received += static_cast<size_t>(n);
        }
        return true;
    }

    /** Sends one framed request, optionally with an SCM_RIGHTS descriptor. */
    [[nodiscard]] bool sendRequest(int fd, uint8_t op, uint64_t handle, const std::vector<uint8_t>& payload, int sendFd) {
        std::vector<uint8_t> frame;
        frame.reserve(13 + payload.size());
        putU32(frame, static_cast<uint32_t>(13 + payload.size()));
        frame.push_back(op);
        for (int i = 0; i < 8; ++i) {
            frame.push_back(static_cast<uint8_t>(handle >> (8 * i)));
        }
        frame.insert(frame.end(), payload.begin(), payload.end());

        if (sendFd < 0) {
            return sendAll(fd, frame.data(), frame.size());
        }

        struct msghdr msg{};
        struct iovec  iov{};
        iov.iov_base   = frame.data();
        iov.iov_len    = frame.size();
        msg.msg_iov    = &iov;
        msg.msg_iovlen = 1;

        union {
            struct cmsghdr align;
            char           buf[CMSG_SPACE(sizeof(int))];
        } control{};
        msg.msg_control      = control.buf;
        msg.msg_controllen   = sizeof(control.buf);
        struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
        cmsg->cmsg_level     = SOL_SOCKET;
        cmsg->cmsg_type      = SCM_RIGHTS;
        cmsg->cmsg_len       = CMSG_LEN(sizeof(int));
        std::memcpy(CMSG_DATA(cmsg), &sendFd, sizeof(int));

        while (::sendmsg(fd, &msg, MSG_NOSIGNAL) < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        return true;
    }

    /** Reads one framed response: [u32 len][u8 status][payload]. */
    [[nodiscard]] bool recvResponse(int fd, uint8_t& status, std::vector<uint8_t>& payload) {
        uint8_t head[4];
        if (!recvAll(fd, head, sizeof(head))) {
            return false;
        }
        // The length prefix counts the whole frame, itself included (as in Go's writeMessage).
        const uint32_t len = getU32(head);
        if (len < 5 || len > kMaxMessageSize) {
            return false;
        }
        std::vector<uint8_t> body(len - 4);
        if (!recvAll(fd, body.data(), body.size())) {
            return false;
        }
        status = body[0];
        payload.assign(body.begin() + 1, body.end());
        return true;
    }

    // ---- helper process management ----

    [[nodiscard]] std::string readHelperSocketName(int pipeFd) {
        struct pollfd pfd{};
        pfd.fd          = pipeFd;
        pfd.events      = POLLIN;
        const int ready = ::poll(&pfd, 1, kSpawnTimeoutMs);
        if (ready <= 0) {
            return {};
        }
        char          buf[128];
        const ssize_t n = ::read(pipeFd, buf, sizeof(buf) - 1);
        if (n <= 0) {
            return {};
        }
        buf[n] = '\0';
        std::string  name(buf);
        const size_t end = name.find('\n');
        if (end != std::string::npos) {
            name.resize(end);
        }
        if (name.empty() || name.size() >= sizeof(sockaddr_un::sun_path)) {
            return {};
        }
        for (const char c : name) {
            const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
            if (!ok) {
                return {};
            }
        }
        return name;
    }

    [[nodiscard]] int connectToAbstractSocket(const std::string& name) {
        const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd < 0) {
            return -1;
        }
        struct sockaddr_un addr{};
        addr.sun_family  = AF_UNIX;
        addr.sun_path[0] = '\0';
        std::strncpy(addr.sun_path + 1, name.c_str(), sizeof(addr.sun_path) - 2);
        const socklen_t len = static_cast<socklen_t>(offsetof(struct sockaddr_un, sun_path) + 1 + name.size());
        if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), len) != 0) {
            ::close(fd);
            return -1;
        }
        struct timeval tv{};
        tv.tv_sec  = kRequestTimeoutS;
        tv.tv_usec = 0;
        ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        return fd;
    }

    class HelperConnection {
      public:
        static HelperConnection& instance() {
            static HelperConnection conn;
            return conn;
        }

        [[nodiscard]] bool transact(uint8_t op, uint64_t handle, const std::vector<uint8_t>& payload, int sendFd, uint8_t& status, std::vector<uint8_t>& resp) {
            std::lock_guard<std::mutex> lock(mutex_);
            for (int attempt = 0; attempt < 2; ++attempt) {
                if (!ensureConnected()) {
                    continue;
                }
                if (sendRequest(fd_, op, handle, payload, sendFd) && recvResponse(fd_, status, resp)) {
                    loggedDown_ = false;
                    reapChild();
                    return true;
                }
                closeConnection();
            }
            if (!loggedDown_) {
                std::fprintf(stderr, "%sengine helper unreachable, engine calls will fail this round\n", logPrefix().c_str());
                loggedDown_ = true;
            }
            return false;
        }

        void shutdown() {
            std::lock_guard<std::mutex> lock(mutex_);
            closeConnection();
            if (childPid_ > 0) {
                // Short EOF grace period, then terminate so teardown never blocks.
                for (int i = 0; i < 20; ++i) {
                    if (reapChildBlocking(false)) {
                        return;
                    }
                    struct timespec ts{0, 10 * 1000 * 1000};
                    nanosleep(&ts, nullptr);
                }
                ::kill(childPid_, SIGTERM);
                (void)reapChildBlocking(true);
            }
        }

      private:
        HelperConnection() = default;
        ~HelperConnection() {
            closeConnection();
        }

        HelperConnection(const HelperConnection&)             = delete;
        HelperConnection&  operator=(const HelperConnection&) = delete;

        [[nodiscard]] bool ensureConnected() {
            if (fd_ >= 0) {
                return true;
            }
            reapChild();
            for (const auto& candidate : helperCandidates()) {
                const Spawned spawned = spawnHelper(candidate);
                if (!spawned.ok) {
                    continue;
                }
                const int fd = connectToAbstractSocket(spawned.sockName);
                if (fd < 0) {
                    killSpawned(spawned);
                    continue;
                }
                if (!handshake(fd)) {
                    ::close(fd);
                    killSpawned(spawned);
                    continue;
                }
                fd_       = fd;
                childPid_ = spawned.pid;
                return true;
            }
            return false;
        }

        [[nodiscard]] static bool handshake(int fd) {
            std::vector<uint8_t> payload;
            putU32(payload, kProtocolVersion);
            uint8_t              status = kStatusError;
            std::vector<uint8_t> resp;
            if (!sendRequest(fd, kOpHello, 0, payload, -1) || !recvResponse(fd, status, resp)) {
                return false;
            }
            return status == kStatusOk && resp.size() == 4 && getU32(resp.data()) == kProtocolVersion;
        }

        struct Spawned {
            bool        ok;
            pid_t       pid;
            std::string sockName;
        };

        [[nodiscard]] static std::vector<std::string> helperCandidates() {
            std::vector<std::string> candidates;
            if (const char* env = std::getenv("LOTUS_ENGINE_HELPER_BIN"); env != nullptr && env[0] != '\0') {
                candidates.emplace_back(env);
            }
#ifdef LOTUS_ENGINE_HELPER_EXECUTABLE
            candidates.emplace_back(LOTUS_ENGINE_HELPER_EXECUTABLE);
#endif
            candidates.emplace_back("lotus-engine-helper");
            return candidates;
        }

        [[nodiscard]] static Spawned spawnHelper(const std::string& program) {
            int pipeFds[2];
            if (::pipe2(pipeFds, O_CLOEXEC) != 0) {
                return {};
            }

            posix_spawn_file_actions_t actions;
            if (posix_spawn_file_actions_init(&actions) != 0) {
                ::close(pipeFds[0]);
                ::close(pipeFds[1]);
                return {};
            }
            posix_spawn_file_actions_adddup2(&actions, pipeFds[1], STDOUT_FILENO);
            posix_spawn_file_actions_addclose(&actions, pipeFds[0]);

            std::vector<char*> argv;
            argv.push_back(const_cast<char*>(program.c_str())); //NOLINT(cppcoreguidelines-pro-type-const-cast)
            argv.push_back(nullptr);

            // A bare program name is searched on $PATH; anything with a slash or
            // an absolute path goes to posix_spawn directly.
            const bool usePath = program.find('/') == std::string::npos;
            pid_t      pid     = -1;
            const int  rc      = usePath ? posix_spawnp(&pid, program.c_str(), &actions, nullptr, argv.data(), environ) :
                                           posix_spawn(&pid, program.c_str(), &actions, nullptr, argv.data(), environ);
            posix_spawn_file_actions_destroy(&actions);
            ::close(pipeFds[1]);
            if (rc != 0) {
                ::close(pipeFds[0]);
                return {};
            }

            const std::string name = readHelperSocketName(pipeFds[0]);
            ::close(pipeFds[0]);
            if (name.empty()) {
                ::kill(pid, SIGKILL);
                int st = 0;
                while (::waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
                return {};
            }
            return Spawned{true, pid, name};
        }

        static void killSpawned(const Spawned& spawned) {
            if (!spawned.ok) {
                return;
            }
            ::kill(spawned.pid, SIGKILL);
            int st = 0;
            while (::waitpid(spawned.pid, &st, 0) < 0 && errno == EINTR) {}
        }

        void closeConnection() {
            if (fd_ >= 0) {
                ::close(fd_);
                fd_ = -1;
            }
        }

        /** Non-blocking opportunistic reaping of the helper zombie. */
        void reapChild() {
            if (childPid_ <= 0) {
                return;
            }
            int         st = 0;
            const pid_t r  = ::waitpid(childPid_, &st, WNOHANG);
            if (r == childPid_ || (r < 0 && errno == ECHILD)) {
                childPid_ = -1;
            }
        }

        [[nodiscard]] bool reapChildBlocking(bool ignoreEintrOnly) {
            if (childPid_ <= 0) {
                return true;
            }
            int         st = 0;
            const pid_t r  = ::waitpid(childPid_, &st, ignoreEintrOnly ? 0 : WNOHANG);
            if (r == childPid_) {
                childPid_ = -1;
                return true;
            }
            if (r < 0 && errno == ECHILD) {
                childPid_ = -1;
                return true;
            }
            return false;
        }

        std::mutex mutex_;
        int        fd_         = -1;
        pid_t      childPid_   = -1;
        bool       loggedDown_ = false;
    };

} // namespace

// ---- public C API (same signatures the cgo bridge used to export) ----

extern "C" void Init(void) {
    // Connect eagerly so a missing helper surfaces at engine construction.
    uint8_t              status = kStatusError;
    std::vector<uint8_t> resp;
    (void)HelperConnection::instance().transact(kOpPing, 0, {}, -1, status, resp);
}

extern "C" uintptr_t NewEngine(const char* name, uintptr_t dictHandle, uintptr_t tableHandle) {
    std::vector<uint8_t> payload;
    putString(payload, name);
    putU64(payload, dictHandle);
    putU64(payload, tableHandle);
    uint8_t              status = kStatusError;
    std::vector<uint8_t> resp;
    if (!HelperConnection::instance().transact(kOpNewEngine, 0, payload, -1, status, resp)) {
        return 0;
    }
    if (status != kStatusOk || resp.size() != 8) {
        return 0;
    }
    return getU64(resp.data());
}

extern "C" uintptr_t NewCustomEngine(char** definition, uintptr_t dictHandle, uintptr_t tableHandle) {
    std::vector<uint8_t> payload = encodePairs(definition);
    putU64(payload, dictHandle);
    putU64(payload, tableHandle);
    uint8_t              status = kStatusError;
    std::vector<uint8_t> resp;
    if (!HelperConnection::instance().transact(kOpNewCustom, 0, payload, -1, status, resp)) {
        return 0;
    }
    if (status != kStatusOk || resp.size() != 8) {
        return 0;
    }
    return getU64(resp.data());
}

extern "C" uintptr_t NewMacroTable(char** definition) {
    uint8_t              status = kStatusError;
    std::vector<uint8_t> resp;
    if (!HelperConnection::instance().transact(kOpNewMacroTable, 0, encodePairs(definition), -1, status, resp)) {
        return 0;
    }
    if (status != kStatusOk || resp.size() != 8) {
        return 0;
    }
    return getU64(resp.data());
}

extern "C" uintptr_t NewDictionary(uintptr_t fdHandle) {
    const int            fd     = static_cast<int>(fdHandle);
    uint8_t              status = kStatusError;
    std::vector<uint8_t> resp;
    const bool           ok = HelperConnection::instance().transact(kOpNewDictionary, 0, {}, fd, status, resp);
    // The old bridge took ownership of the fd in-process; do the same now that
    // it lives on in the helper. Never leak it.
    if (fd >= 0) {
        ::close(fd);
    }
    if (!ok || status != kStatusOk || resp.size() != 8) {
        return 0;
    }
    return getU64(resp.data());
}

extern "C" void DeleteObject(uintptr_t handle) {
    uint8_t              status = kStatusError;
    std::vector<uint8_t> resp;
    (void)HelperConnection::instance().transact(kOpDeleteObject, handle, {}, -1, status, resp);
}

extern "C" bool EngineProcessKeyEvent(uintptr_t engine, uint32_t keyVal, uint32_t state) {
    std::vector<uint8_t> payload;
    putU32(payload, keyVal);
    putU32(payload, state);
    uint8_t              status = kStatusError;
    std::vector<uint8_t> resp;
    if (!HelperConnection::instance().transact(kOpProcessKey, engine, payload, -1, status, resp)) {
        return false;
    }
    return status == kStatusOk && resp.size() == 1 && resp[0] != 0;
}

extern "C" char* EnginePullPreedit(uintptr_t engine) {
    uint8_t              status = kStatusError;
    std::vector<uint8_t> resp;
    if (!HelperConnection::instance().transact(kOpPullPreedit, engine, {}, -1, status, resp)) {
        return nullptr;
    }
    if (status != kStatusOk) {
        return nullptr;
    }
    size_t pos = 0;
    return takeString(resp, pos);
}

extern "C" void EngineCommitPreedit(uintptr_t engine) {
    uint8_t              status = kStatusError;
    std::vector<uint8_t> resp;
    (void)HelperConnection::instance().transact(kOpCommitPreedit, engine, {}, -1, status, resp);
}

extern "C" char* EnginePullCommit(uintptr_t engine) {
    uint8_t              status = kStatusError;
    std::vector<uint8_t> resp;
    if (!HelperConnection::instance().transact(kOpPullCommit, engine, {}, -1, status, resp)) {
        return nullptr;
    }
    if (status != kStatusOk) {
        return nullptr;
    }
    size_t pos = 0;
    return takeString(resp, pos);
}

extern "C" void EngineSetMacroEnabled(uintptr_t engine, bool enabled) {
    std::vector<uint8_t> payload;
    putBool(payload, enabled);
    uint8_t              status = kStatusError;
    std::vector<uint8_t> resp;
    (void)HelperConnection::instance().transact(kOpSetMacro, engine, payload, -1, status, resp);
}

extern "C" void EngineSetOption(uintptr_t engine, const FcitxBambooEngineOption* option) {
    if (option == nullptr) {
        return;
    }
    // Field order must stay in sync with decodeOption in bamboo/dispatch.go.
    std::vector<uint8_t> payload;
    putBool(payload, option->autoNonVnRestore);
    putBool(payload, option->ddFreeStyle);
    putBool(payload, option->macroEnabled);
    putBool(payload, option->autoCapitalizeMacro);
    putBool(payload, option->spellCheckWithDicts);
    putString(payload, option->outputCharset);
    putBool(payload, option->modernStyle);
    putBool(payload, option->freeMarking);
    putU32(payload, static_cast<uint32_t>(option->w2u));
    putU32(payload, static_cast<uint32_t>(option->bracketTransform));
    putString(payload, option->timeFormat);
    putString(payload, option->dateFormat);
    uint8_t              status = kStatusError;
    std::vector<uint8_t> resp;
    (void)HelperConnection::instance().transact(kOpSetOption, engine, payload, -1, status, resp);
}

extern "C" void ResetEngine(uintptr_t engine) {
    uint8_t              status = kStatusError;
    std::vector<uint8_t> resp;
    (void)HelperConnection::instance().transact(kOpResetEngine, engine, {}, -1, status, resp);
}

extern "C" void EngineRebuildFromText(uintptr_t engine, const char* text) {
    std::vector<uint8_t> payload;
    putString(payload, text);
    uint8_t              status = kStatusError;
    std::vector<uint8_t> resp;
    (void)HelperConnection::instance().transact(kOpRebuildText, engine, payload, -1, status, resp);
}

extern "C" char** GetCharsetNames(void) {
    uint8_t              status = kStatusError;
    std::vector<uint8_t> resp;
    if (!HelperConnection::instance().transact(kOpGetCharsets, 0, {}, -1, status, resp)) {
        return nullptr;
    }
    if (status != kStatusOk) {
        return nullptr;
    }
    return takeStringList(resp);
}

extern "C" char** GetInputMethodNames(void) {
    uint8_t              status = kStatusError;
    std::vector<uint8_t> resp;
    if (!HelperConnection::instance().transact(kOpGetIMNames, 0, {}, -1, status, resp)) {
        return nullptr;
    }
    if (status != kStatusOk) {
        return nullptr;
    }
    return takeStringList(resp);
}

extern "C" void LotusEngineHelperShutdown(void) {
    HelperConnection::instance().shutdown();
}
