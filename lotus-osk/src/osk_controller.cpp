#include "osk_controller.h"
#include "osk_window.h"
#include <QDebug>
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusError>
#include <QtDBus/QDBusMessage>
#include <QDateTime>
#include <sys/socket.h>
#include <poll.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstddef>

struct LotusKeyCommand {
    uint32_t type;  // 0 = Backspace count, 1 = Key Event
    uint32_t code;  // Key code (linux/input-event-codes.h)
    uint32_t value; // 1 = press, 0 = release
};

OSKController::OSKController(QObject* parent) : QObject(parent), m_visible(false), m_window(nullptr), m_socketFd(-1) {
    qDebug() << "Lotus OSK Controller initialized";

    QDBusConnection::sessionBus().registerService("app.lotus.Osk");
    QDBusConnection::sessionBus().registerObject("/app/lotus/Osk/Controller", this, QDBusConnection::ExportAllSlots);

    // Socket to lotus-server is only needed for CapsLock queries; connect lazily on first use.
}

void OSKController::connectToServer() {
    if (m_socketFd >= 0)
        close(m_socketFd);

    m_socketFd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK, 0);
    if (m_socketFd < 0)
        return;

    QString username = qgetenv("USER");
    if (username.isEmpty())
        username = qgetenv("USERNAME");

    std::string        path = "lotussocket-" + username.toStdString() + "-osk_socket";

    struct sockaddr_un addr{};
    addr.sun_family  = AF_UNIX;
    addr.sun_path[0] = '\0';
    memcpy(&addr.sun_path[1], path.c_str(), path.length());
    socklen_t len = offsetof(struct sockaddr_un, sun_path) + path.length() + 1;

    if (::connect(m_socketFd, (struct sockaddr*)&addr, len) < 0) {
        close(m_socketFd);
        m_socketFd = -1;
    } else {
        qDebug() << "Connected to lotus-server socket";
    }
}

OSKController::~OSKController() {
    if (m_window)
        deleteLater();
    if (m_socketFd >= 0)
        close(m_socketFd);
}

void OSKController::setVisible(bool visible) {
    if (m_visible == visible)
        return;
    if (visible)
        showWindow();
    else
        hideWindow();
}

void OSKController::showWindow() {
    if (!m_window)
        m_window = new OSKWindow(this);
    m_window->show();
    m_visible = true;
    notifyServerVisibility();
    emit visibleChanged();
}

void OSKController::hideWindow() {
    if (m_window)
        m_window->hide();
    m_visible = false;
    notifyServerVisibility();
    emit visibleChanged();
}

void OSKController::notifyServerVisibility() {
    if (m_socketFd < 0)
        connectToServer();

    if (m_socketFd >= 0) {
        LotusKeyCommand cmd;
        cmd.type  = m_visible ? 3 : 4; // 3 = OSK Show, 4 = OSK Hide
        cmd.code  = 0;
        cmd.value = 0;

        if (send(m_socketFd, &cmd, sizeof(cmd), MSG_NOSIGNAL) != sizeof(cmd)) {
            close(m_socketFd);
            m_socketFd = -1;
        }
    }
}

void OSKController::sendKey(uint keyval, bool isRelease, uint keycode, bool /*shift*/) {
    // Key events are sent via the uinput socket to lotus-server, which injects
    // them as real hardware key events through /dev/uinput.
    if (m_socketFd < 0)
        connectToServer();

    if (m_socketFd >= 0 && keycode > 0) {
        LotusKeyCommand cmd;
        cmd.type  = 1; // Key Event
        cmd.code  = keycode;
        cmd.value = isRelease ? 0 : 1;

        if (send(m_socketFd, &cmd, sizeof(cmd), MSG_NOSIGNAL) != sizeof(cmd)) {
            close(m_socketFd);
            m_socketFd = -1;
        }
    }
}

bool OSKController::queryCapsLockState() {
    if (m_socketFd < 0)
        connectToServer();
    if (m_socketFd < 0)
        return false;

    LotusKeyCommand cmd;
    cmd.type  = 2; // Query CapsLock
    cmd.code  = 0;
    cmd.value = 0;

    if (send(m_socketFd, &cmd, sizeof(cmd), MSG_NOSIGNAL) == sizeof(cmd)) {
        int state = 0;
        // Wait for reply (blocking, but should be instant)
        struct pollfd pfd;
        pfd.fd     = m_socketFd;
        pfd.events = POLLIN;
        if (poll(&pfd, 1, 100) > 0) { // 100ms timeout
            if (recv(m_socketFd, &state, sizeof(state), 0) == sizeof(state)) {
                return state > 0;
            }
        }
    }

    return false;
}

void OSKController::Show() {
    qDebug() << "DBus Show called";
    setVisible(true);
}

void OSKController::Hide() {
    qDebug() << "DBus Hide called";
    setVisible(false);
}

void OSKController::Toggle() {
    qDebug() << "DBus Toggle called";
    setVisible(!m_visible);
}
