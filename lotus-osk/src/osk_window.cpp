#include "osk_window.h"
#include "osk_controller.h"
#include <QPainter>
#include <QWindow>
#include <QGridLayout>
#include <QPushButton>
#include <QVBoxLayout>
#include <QPair>
#include <QtDBus/QDBusMessage>
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusPendingReply>
#ifdef HAVE_LAYER_SHELL
#include <LayerShellQt/Window>
#endif

OSKWindow::OSKWindow(OSKController* controller, QWidget* parent) : QWidget(parent), m_controller(controller) {
    // Set window properties for OSK
    setAttribute(Qt::WA_TranslucentBackground);
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool | Qt::WindowDoesNotAcceptFocus);

    // Initial size
    resize(800, 320);
    setWindowTitle("Lotus OSK");

    setupLayout();

    // Sync initial CapsLock state
    m_capsLockActive = m_controller->queryCapsLockState();
    updateKeyLabels();

    // Sync when OSK becomes visible
    connect(m_controller, &OSKController::visibleChanged, this, [this]() {
        if (m_controller->visible()) {
            m_capsLockActive = m_controller->queryCapsLockState();
            updateKeyLabels();
        }
    });
}

OSKWindow::~OSKWindow() {}

void OSKWindow::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);

#ifdef HAVE_LAYER_SHELL
    auto layerWindow = LayerShellQt::Window::get(windowHandle());
    if (layerWindow) {
        layerWindow->setLayer(LayerShellQt::Window::LayerOverlay);
        layerWindow->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
        layerWindow->setAnchors(LayerShellQt::Window::AnchorBottom);
        layerWindow->setExclusiveZone(height());
        // For centering: only AnchorBottom, do not anchor to sides
        layerWindow->setAnchors(LayerShellQt::Window::AnchorBottom);
    }
#else
    // KWin Workaround: Use DBus to tell KWin to keep this window on top
    QDBusMessage msg = QDBusMessage::createMethodCall("org.kde.KWin", "/Scripting", "org.kde.KWin.Scripting", "loadScript");

    // Script to find the OSK window and set keepAbove
    QString script = "var clients = workspace.stackingOrder;"
                     "for (var i = 0; i < clients.length; i++) {"
                     "    if (clients[i].caption === 'Lotus OSK') {"
                     "        clients[i].keepAbove = true;"
                     "        clients[i].onAllDesktops = true;"
                     "        clients[i].skipTaskbar = true;"
                     "        clients[i].skipPager = true;"
                     "        clients[i].skipSwitcher = true;"
                     "    }"
                     "}";

    msg << "/tmp/lotus-osk-kwin-script.js" << script;

    QDBusMessage reply = QDBusConnection::sessionBus().call(msg);
    if (reply.type() == QDBusMessage::ReplyMessage) {
        int          scriptId = reply.arguments().at(0).toInt();
        QDBusMessage runMsg   = QDBusMessage::createMethodCall("org.kde.KWin", "/Scripting/Script" + QString::number(scriptId), "org.kde.KWin.Script", "run");
        QDBusConnection::sessionBus().send(runMsg);
    }
#endif
}

void OSKWindow::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Draw background
    painter.setBrush(QColor(25, 25, 25, 220)); // Slightly darker and more opaque
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(rect(), 10, 10); // Rounded corners for premium feel
}

void OSKWindow::updateKeyLabels() {
    bool upper = m_capsLockActive;

    // Update alphabet labels
    for (auto it = m_alphabetButtons.begin(); it != m_alphabetButtons.end(); ++it) {
        it.value()->setText(upper ? it.key().toUpper() : it.key().toLower());
    }

    // Note: CapsLock should NOT affect number and symbol keys.
    // They remain as they are.

    // Update special key styles (CapsLock colors)
    for (auto* btn : m_specialButtons) {
        bool active = false;
        if (btn->text() == "⇪")
            active = m_capsLockActive;

        QString bg    = active ? "#005a9e" : "#252525";
        QString extra = m_buttonExtraStyles.value(btn);
        btn->setStyleSheet(QString("QPushButton {"
                                   "  background-color: %1;"
                                   "  color: %2;"
                                   "  border-radius: 6px;"
                                   "  font-size: 20px;"
                                   "  border: 1px solid #444444;"
                                   "}"
                                   "QPushButton:hover { background-color: #444444; }"
                                   "QPushButton:pressed { background-color: #555555; padding: 2px 0 0 0; }")
                               .arg(bg)
                               .arg(active ? "#ffffff" : "#999999") +
                           extra);
    }
}

void OSKWindow::setupLayout() {
    auto mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(15, 15, 15, 15);
    mainLayout->setSpacing(8);

    auto createKey = [this](const QString& key, const QString& text = "", double widthFactor = 1.0, const QString& extraStyle = "") {
        QString label = text.isEmpty() ? key : text;
        auto    btn   = new QPushButton(label, this);
        btn->setProperty("osk_key", key); // For label updates
        int baseHeight = 55;
        int baseWidth  = 70;
        btn->setFixedSize(static_cast<int>(baseWidth * widthFactor), baseHeight);
        btn->setFocusPolicy(Qt::NoFocus);

        if (key.length() == 1 && key[0].toLower() >= 'a' && key[0].toLower() <= 'z') {
            m_alphabetButtons[key.toUpper()] = btn;
        }
        if (label == "⇪") {
            m_specialButtons.append(btn);
        }

        m_buttonExtraStyles[btn] = extraStyle;
        btn->setStyleSheet("QPushButton {"
                           "  background-color: #333333;"
                           "  color: #ffffff;"
                           "  border-radius: 6px;"
                           "  font-size: 20px;"
                           "  border: 1px solid #444444;"
                           "}"
                           "QPushButton:hover { background-color: #444444; }"
                           "QPushButton:pressed { background-color: #555555; padding: 2px 0 0 0; }" +
                           extraStyle);

        auto getKeyInfo = [this](const QString& k) -> QPair<uint, uint> {
            uint    keysym  = 0;
            uint    keycode = 0;
            bool    upper   = m_capsLockActive;

            QString low = k.toLower();
            if (low.length() == 1) {
                char              c       = low[0].toLatin1();
                static const uint codes[] = {30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38, 50, 49, 24, 25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44};
                if (c >= 'a' && c <= 'z') {
                    keycode = codes[c - 'a'];
                    keysym  = upper ? (uint)toupper(c) : (uint)c;
                } else if (c >= '0' && c <= '9') {
                    static const uint nums[] = {11, 2, 3, 4, 5, 6, 7, 8, 9, 10};
                    keycode                  = nums[c - '0'];
                    if (upper) {
                        static const char syms[] = {')', '!', '@', '#', '$', '%', '^', '&', '*', '('};
                        keysym                   = syms[c - '0'];
                    } else {
                        keysym = (uint)c;
                    }
                } else if (c == '-') {
                    keycode = 12;
                    keysym  = upper ? '_' : '-';
                } else if (c == '=') {
                    keycode = 13;
                    keysym  = upper ? '+' : '=';
                } else if (c == '[') {
                    keycode = 26;
                    keysym  = upper ? '{' : '[';
                } else if (c == ']') {
                    keycode = 27;
                    keysym  = upper ? '}' : ']';
                } else if (c == '\\') {
                    keycode = 43;
                    keysym  = upper ? '|' : '\\';
                } else if (c == ';') {
                    keycode = 39;
                    keysym  = upper ? ':' : ';';
                } else if (c == '\'') {
                    keycode = 40;
                    keysym  = upper ? '"' : '\'';
                } else if (c == ',') {
                    keycode = 51;
                    keysym  = upper ? '<' : ',';
                } else if (c == '.') {
                    keycode = 52;
                    keysym  = upper ? '>' : '.';
                } else if (c == '/') {
                    keycode = 53;
                    keysym  = upper ? '?' : '/';
                } else if (c == '`') {
                    keycode = 41;
                    keysym  = upper ? '~' : '`';
                }
            } else {
                if (k == "Backspace") {
                    keysym  = 0xff08;
                    keycode = 14;
                } else if (k == "Space") {
                    keysym  = 0x20;
                    keycode = 57;
                } else if (k == "Enter") {
                    keysym  = 0xff0d;
                    keycode = 28;
                } else if (k == "Tab") {
                    keysym  = 0xff09;
                    keycode = 15;
                } else if (k == "CapsLock") {
                    keysym  = 0xffe5;
                    keycode = 58;
                } else if (k == "Up") {
                    keysym  = 0xff52;
                    keycode = 103;
                } else if (k == "Down") {
                    keysym  = 0xff54;
                    keycode = 108;
                } else if (k == "Left") {
                    keysym  = 0xff51;
                    keycode = 105;
                } else if (k == "Right") {
                    keysym  = 0xff53;
                    keycode = 106;
                }
            }
            return {keysym, keycode};
        };

        connect(btn, &QPushButton::pressed, this, [this, key, getKeyInfo]() {
            if (key == "CapsLock") {
                m_capsLockActive = !m_capsLockActive;
                updateKeyLabels();

                // Also send physical CapsLock to the system
                auto info = getKeyInfo(key);
                m_controller->sendKey(info.first, false, info.second);
                return;
            }

            auto info = getKeyInfo(key);
            m_controller->sendKey(info.first, false, info.second, false);
        });
        connect(btn, &QPushButton::released, this, [this, key, getKeyInfo]() {
            if (key == "CapsLock") {
                auto info = getKeyInfo(key);
                m_controller->sendKey(info.first, true, info.second);
                return;
            }
            auto info = getKeyInfo(key);
            m_controller->sendKey(info.first, true, info.second, false);
        });
        return btn;
    };

    QString ctrlStyle = "background-color: #252525; font-size: 16px; color: #999999;";

    // Row 0: Numbers
    auto row0 = new QHBoxLayout();
    row0->setSpacing(4);
    row0->setAlignment(Qt::AlignCenter);
    for (const char* k : {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "="}) {
        row0->addWidget(createKey(k));
    }
    row0->addWidget(createKey("Backspace", "⌫", 1.5, ctrlStyle));
    mainLayout->addLayout(row0);

    // Row 1: QWERTY
    auto row1 = new QHBoxLayout();
    row1->setSpacing(4);
    row1->setAlignment(Qt::AlignCenter);
    row1->addWidget(createKey("Tab", "⇥", 1.2, ctrlStyle));
    for (const char* k : {"Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "[", "]", "\\"}) {
        row1->addWidget(createKey(k));
    }
    mainLayout->addLayout(row1);

    // Row 2: ASDF
    auto row2 = new QHBoxLayout();
    row2->setSpacing(4);
    row2->setAlignment(Qt::AlignCenter);
    row2->addWidget(createKey("CapsLock", "⇪", 1.5, ctrlStyle));
    for (const char* k : {"A", "S", "D", "F", "G", "H", "J", "K", "L", ";", "'"}) {
        row2->addWidget(createKey(k));
    }
    row2->addWidget(createKey("Enter", "⏎", 1.8, "background-color: #005a9e; color: white;"));
    mainLayout->addLayout(row2);

    // Row 3: ZXCV
    auto row3 = new QHBoxLayout();
    row3->setSpacing(4);
    row3->setAlignment(Qt::AlignCenter);
    for (const char* k : {"Z", "X", "C", "V", "B", "N", "M", ",", ".", "/"}) {
        row3->addWidget(createKey(k));
    }

    // Up Arrow at the end of Row 3
    auto arrowStyle = "background-color: #2a2a2a; color: #aaaaaa; font-size: 18px;";
    row3->addWidget(createKey("Up", "↑", 1.2, arrowStyle));
    mainLayout->addLayout(row3);

    // Row 4: Bottom
    auto row4 = new QHBoxLayout();
    row4->setSpacing(4);
    row4->setAlignment(Qt::AlignCenter);
    row4->addWidget(createKey("Space", "␣", 6.5));

    // Other Arrows
    row4->addWidget(createKey("Left", "←", 1.2, arrowStyle));
    row4->addWidget(createKey("Down", "↓", 1.2, arrowStyle));
    row4->addWidget(createKey("Right", "→", 1.2, arrowStyle));

    mainLayout->addLayout(row4);

    updateKeyLabels();

    setFixedSize(1100, 320);
}
