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

#include <pwd.h>
#include "lotus-key-command.h"

OSKController::OSKController(QObject* parent) : QObject(parent), m_visible(false), m_window(nullptr), m_socketFd(-1) {
    qDebug() << "Lotus OSK Controller initialized";

    m_hideTimer.setSingleShot(true);
    m_hideTimer.setInterval(250);
    connect(&m_hideTimer, &QTimer::timeout, this, &OSKController::hideWindow);

    QDBusConnection::sessionBus().registerService("app.lotus.Osk");
    QDBusConnection::sessionBus().registerObject("/app/lotus/Osk/Controller", this, QDBusConnection::ExportAllSlots);

    // Socket to lotus-server is only needed for CapsLock queries; connect lazily on first use.
}

void OSKController::connectToServer() {
    if (m_notifier) {
        m_notifier->setEnabled(false);
        delete m_notifier;
        m_notifier = nullptr;
    }
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
        if (m_notifier) {
            m_notifier->setEnabled(false);
            delete m_notifier;
        }
        m_notifier = new QSocketNotifier(m_socketFd, QSocketNotifier::Read, this);
        connect(m_notifier, &QSocketNotifier::activated, this, &OSKController::handleSocketActivated);
    }
}

OSKController::~OSKController() {
    if (m_notifier)
        m_notifier->setEnabled(false);
    delete m_notifier;
    if (m_socketFd >= 0)
        close(m_socketFd);
}

void OSKController::setVisible(bool visible) {
    if (visible) {
        m_hideTimer.stop();
        if (!m_visible) {
            showWindow();
        }
    } else {
        if (m_visible && !m_hideTimer.isActive()) {
            m_hideTimer.start();
        }
    }
}

void OSKController::showWindow() {
    if (!m_window) {
        m_window = new OSKWindow(this);
        m_window->setWhiteTheme(m_whiteTheme);
        m_window->setOSKSize(m_oskSize);
    }
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
        cmd.type  = m_visible ? LotusKeyCommandType::OskShow : LotusKeyCommandType::OskHide;
        cmd.code  = 0;
        cmd.value = 0;

        if (send(m_socketFd, &cmd, sizeof(cmd), MSG_NOSIGNAL) != sizeof(cmd)) {
            close(m_socketFd);
            m_socketFd = -1;
        }
    }
}

void OSKController::sendKey(bool isRelease, uint keycode) {
    // Key events are sent via the uinput socket to lotus-server, which injects
    // them as real hardware key events through /dev/uinput.
    if (m_socketFd < 0)
        connectToServer();

    if (m_socketFd >= 0 && keycode > 0) {
        LotusKeyCommand cmd;
        cmd.type  = LotusKeyCommandType::KeyEvent;
        cmd.code  = keycode;
        cmd.value = isRelease ? 0 : 1;

        if (send(m_socketFd, &cmd, sizeof(cmd), MSG_NOSIGNAL) != sizeof(cmd)) {
            close(m_socketFd);
            m_socketFd = -1;
            if (m_notifier) {
                m_notifier->setEnabled(false);
                delete m_notifier;
                m_notifier = nullptr;
            }
        }
    }
}

void OSKController::queryCapsLockState() {
    if (m_socketFd < 0)
        connectToServer();
    if (m_socketFd < 0)
        return;

    LotusKeyCommand cmd;
    cmd.type  = LotusKeyCommandType::QueryCapsLock;
    cmd.code  = 0;
    cmd.value = 0;

    if (send(m_socketFd, &cmd, sizeof(cmd), MSG_NOSIGNAL) != sizeof(cmd)) {
        close(m_socketFd);
        m_socketFd = -1;
        if (m_notifier) {
            m_notifier->setEnabled(false);
            delete m_notifier;
            m_notifier = nullptr;
        }
    }
}

void OSKController::handleSocketActivated(int /*socket*/) {
    int     state = 0;
    ssize_t n     = recv(m_socketFd, &state, sizeof(state), 0);
    if (n == sizeof(state)) {
        bool active = state > 0;
        if (active != m_capsLockActive) {
            m_capsLockActive = active;
            emit capsLockActiveChanged();
        }
    } else {
        // Server disconnected or error - cleanup to avoid busy loop
        if (m_notifier) {
            m_notifier->setEnabled(false);
            delete m_notifier;
            m_notifier = nullptr;
        }
        if (m_socketFd >= 0) {
            close(m_socketFd);
            m_socketFd = -1;
        }
        qDebug() << "Server disconnected, cleaned up socket notifier";
    }
}

void OSKController::Show() {
    qDebug() << "DBus Show called";
    setVisible(true);
}

void OSKController::Hide() {
    qDebug() << "DBus Hide called";
    m_hideTimer.stop();
    hideWindow();
}

void OSKController::Toggle() {
    qDebug() << "DBus Toggle called";
    setVisible(!m_visible);
}

void OSKController::setWhiteTheme(bool white) {
    if (m_whiteTheme != white) {
        m_whiteTheme = white;
        if (m_window) {
            m_window->setWhiteTheme(white);
        }
        emit whiteThemeChanged();
    }
}

void OSKController::SetTheme(bool white) {
    qDebug() << "DBus SetTheme called:" << white;
    setWhiteTheme(white);
}

void OSKController::setOskSize(const QString& size) {
    if (m_oskSize != size) {
        m_oskSize = size;
        if (m_window) {
            m_window->setOSKSize(size);
        }
        emit oskSizeChanged();
    }
}

void OSKController::SetSize(const QString& size) {
    qDebug() << "DBus SetSize called:" << size;
    setOskSize(size);
}
