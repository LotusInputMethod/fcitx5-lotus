#include "osk_window.h"
#include "osk_controller.h"
#include <QPainter>
#include <QWindow>
#include <QPushButton>
#include <QVBoxLayout>
#include <QPair>
#include <QtDBus/QDBusMessage>
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusPendingReply>
#ifdef HAVE_LAYER_SHELL
#include <LayerShellQt/Window>
#endif
#include <QTemporaryFile>

OSKWindow::OSKWindow(OSKController* controller, QWidget* parent) : QWidget(parent), m_controller(controller) {
    // Set window properties for OSK
    setAttribute(Qt::WA_TranslucentBackground);
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool | Qt::WindowDoesNotAcceptFocus);

    // Initial size
    resize(1100, 380);
    setWindowTitle("Lotus OSK");

    setupLayout();
    setFixedSize(1100, 380);

    // Sync initial CapsLock state
    connect(m_controller, &OSKController::capsLockActiveChanged, this, [this]() {
        m_capsLockActive = m_controller->capsLockActive();
        updateKeyLabels();
    });
    m_controller->queryCapsLockState();

    // Sync when OSK becomes visible
    connect(m_controller, &OSKController::visibleChanged, this, [this]() {
        if (m_controller->visible()) {
            m_controller->queryCapsLockState();
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

    // Unload previous script if any
    if (m_kwinScriptId != -1) {
        QDBusMessage unloadMsg = QDBusMessage::createMethodCall("org.kde.KWin", "/Scripting", "org.kde.KWin.Scripting", "unloadScript");
        unloadMsg << "lotus-osk-keep-above";
        QDBusConnection::sessionBus().call(unloadMsg);
        m_kwinScriptId = -1;
    }

    m_kwinScriptFile.setFileTemplate(QDir::tempPath() + "/lotus-osk-kwin-script-XXXXXX.js");
    if (m_kwinScriptFile.open()) {
        m_kwinScriptFile.write(script.toUtf8());
        m_kwinScriptFile.close();
        msg << m_kwinScriptFile.fileName() << "lotus-osk-keep-above";
    } else {
        qWarning() << "Failed to create temporary file for KWin script";
        return;
    }

    QDBusMessage reply = QDBusConnection::sessionBus().call(msg);
    if (reply.type() == QDBusMessage::ReplyMessage) {
        m_kwinScriptId      = reply.arguments().at(0).toInt();
        QDBusMessage runMsg = QDBusMessage::createMethodCall("org.kde.KWin", "/Scripting/Script" + QString::number(m_kwinScriptId), "org.kde.KWin.Script", "run");
        QDBusConnection::sessionBus().send(runMsg);
    }
#endif
}

void OSKWindow::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);

#ifdef HAVE_LAYER_SHELL
    auto layerWindow = LayerShellQt::Window::get(windowHandle());
    if (layerWindow) {
        layerWindow->setExclusiveZone(0);
    }
#endif
}

void OSKWindow::setWhiteTheme(bool white) {
    if (m_whiteTheme != white) {
        m_whiteTheme = white;
        updateKeyLabels();
        update();
    }
}

void OSKWindow::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Draw background
    QColor bgColor(m_whiteTheme ? L_COLOR_WINDOW_BG : COLOR_WINDOW_BG);
    bgColor.setAlpha(m_whiteTheme ? 255 : 220); // More opaque for white theme
    painter.setBrush(bgColor);
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(rect(), 10, 10);
}

void OSKWindow::updateKeyLabels() {
    bool    upperAlphabet = m_capsLockActive || m_shiftActive;

    QString normalBg = m_whiteTheme ? L_COLOR_BG_NORMAL : COLOR_BG_NORMAL;
    QString normalFg = m_whiteTheme ? L_COLOR_FG_NORMAL : COLOR_FG_NORMAL;

    // Update alphabet labels and styles
    for (auto it = m_alphabetButtons.begin(); it != m_alphabetButtons.end(); ++it) {
        it.value()->setText(upperAlphabet ? it.key().toUpper() : it.key().toLower());
        it.value()->setStyleSheet(getButtonStyle(normalBg, normalFg));
    }

    bool                                 upperSymbol = m_shiftActive;
    static const QHash<QString, QString> symbolMap   = {{"1", "!"},  {"2", "@"}, {"3", "#"},  {"4", "$"}, {"5", "%"}, {"6", "^"}, {"7", "&&"},
                                                        {"8", "*"},  {"9", "("}, {"0", ")"},  {"-", "_"}, {"=", "+"}, {"[", "{"}, {"]", "}"},
                                                        {"\\", "|"}, {";", ":"}, {"'", "\""}, {",", "<"}, {".", ">"}, {"/", "?"}, {"`", "~"}};

    for (auto it = m_symbolButtons.begin(); it != m_symbolButtons.end(); ++it) {
        it.value()->setText(upperSymbol && symbolMap.contains(it.key()) ? symbolMap.value(it.key()) : it.key());
        it.value()->setStyleSheet(getButtonStyle(normalBg, normalFg));
    }

    // Update special key styles (CapsLock, Shift, Enter, Space, Arrows, etc.)
    for (auto* btn : m_specialButtons) {
        QString key    = btn->property("osk_key").toString();
        bool    active = false;
        if (key == "CapsLock")
            active = m_capsLockActive;
        else if (key == "Shift")
            active = m_shiftActive;

        QString bg;
        QString fg;

        if (active || key == "Enter") {
            bg = m_whiteTheme ? L_COLOR_BG_ACTIVE : COLOR_BG_ACTIVE;
            fg = "#ffffff"; // Always white text on active/Enter blue
        } else if (key == "Up" || key == "Down" || key == "Left" || key == "Right") {
            bg = m_whiteTheme ? L_COLOR_BG_SPECIAL : "#2a2a2a";
            fg = m_whiteTheme ? L_COLOR_FG_SPECIAL : "#aaaaaa";
        } else if (key == "Space") {
            bg = m_whiteTheme ? L_COLOR_BG_NORMAL : COLOR_BG_NORMAL;
            fg = m_whiteTheme ? L_COLOR_FG_NORMAL : COLOR_FG_NORMAL;
        } else {
            bg = m_whiteTheme ? L_COLOR_BG_SPECIAL : COLOR_BG_SPECIAL;
            fg = m_whiteTheme ? L_COLOR_FG_SPECIAL : COLOR_FG_SPECIAL;
        }

        QString extra = m_buttonExtraStyles.value(btn);
        btn->setStyleSheet(getButtonStyle(bg, fg, extra));
    }
}

QString OSKWindow::getButtonStyle(const QString& bg, const QString& fg, const QString& extra) const {
    QString borderColor  = m_whiteTheme ? L_COLOR_BORDER : COLOR_BORDER;
    QString hoverColor   = m_whiteTheme ? L_COLOR_HOVER : COLOR_HOVER;
    QString pressedColor = m_whiteTheme ? L_COLOR_PRESSED : COLOR_PRESSED;

    return QString("QPushButton {"
                   "  background-color: %1;"
                   "  color: %2;"
                   "  border-radius: 6px;"
                   "  font-size: 20px;"
                   "  border: 1px solid %3;"
                   "}"
                   "QPushButton:hover { background-color: %4; }"
                   "QPushButton:pressed { background-color: %5; padding: 2px 0 0 0; }")
               .arg(bg)
               .arg(fg)
               .arg(borderColor)
               .arg(hoverColor)
               .arg(pressedColor) +
        extra;
}

QPair<uint, uint> OSKWindow::getKeyInfo(const QString& k) const {
    bool upper = m_capsLockActive || m_shiftActive;

    // 1. Handle single-character keys
    if (k.length() == 1) {
        char c = k.toLower()[0].toLatin1();

        // Letters a-z
        if (c >= 'a' && c <= 'z') {
            static const uint codes[] = {30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38, 50, 49, 24, 25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44};
            return {upper ? (uint)toupper(c) : (uint)c, codes[c - 'a']};
        }

        // Numbers 0-9
        if (c >= '0' && c <= '9') {
            static const uint codes[] = {11, 2, 3, 4, 5, 6, 7, 8, 9, 10};
            static const char syms[]  = {')', '!', '@', '#', '$', '%', '^', '&', '*', '('};
            return {upper ? (uint)syms[c - '0'] : (uint)c, codes[c - '0']};
        }

        // Symbols
        static const QHash<char, KeyData> symbolMap = {{'-', {12, '-', '_'}},   {'=', {13, '=', '+'}}, {'[', {26, '[', '{'}},   {']', {27, ']', '}'}},
                                                       {'\\', {43, '\\', '|'}}, {';', {39, ';', ':'}}, {'\'', {40, '\'', '"'}}, {',', {51, ',', '<'}},
                                                       {'.', {52, '.', '>'}},   {'/', {53, '/', '?'}}, {'`', {41, '`', '~'}},   {' ', {57, ' ', ' '}}};

        if (symbolMap.contains(c)) {
            const auto& data = symbolMap.value(c);
            return {upper ? data.keysymUpper : data.keysym, data.keycode};
        }
    }

    // 2. Handle named special keys
    static const QHash<QString, QPair<uint, uint>> specialMap = {{"Backspace", {0xff08, 14}}, {"Enter", {0xff0d, 28}},  {"Tab", {0xff09, 15}}, {"CapsLock", {0xffe5, 58}},
                                                                 {"Shift", {0xffe1, 42}},     {"Super", {0xffeb, 125}}, {"Up", {0xff52, 103}}, {"Down", {0xff54, 108}},
                                                                 {"Left", {0xff51, 105}},     {"Right", {0xff53, 106}}, {"Space", {0x20, 57}}};

    return specialMap.value(k, {0, 0});
}

void OSKWindow::setupLayout() {
    auto mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(6);

    auto createKey = [this](const QString& key, const QString& text = "", double widthFactor = 1.0, const QString& extraStyle = "") {
        QString label = text.isEmpty() ? key : text;
        auto    btn   = new QPushButton(label, this);
        btn->setProperty("osk_key", key); // For label updates
        int baseHeight = 65;
        int baseWidth  = 72;
        btn->setFixedSize(static_cast<int>(baseWidth * widthFactor), baseHeight);
        btn->setFocusPolicy(Qt::NoFocus);

        if (key.length() == 1 && key[0].toLower() >= 'a' && key[0].toLower() <= 'z') {
            m_alphabetButtons[key.toUpper()] = btn;
        } else if (key.length() == 1 && key != " ") {
            m_symbolButtons[key] = btn;
        } else {
            m_specialButtons.append(btn);
        }

        m_buttonExtraStyles[btn] = extraStyle;
        btn->setStyleSheet(getButtonStyle(COLOR_BG_NORMAL, COLOR_FG_NORMAL, extraStyle));

        connect(btn, &QPushButton::pressed, this, [this, key]() {
            if (key == "Hide") {
                m_controller->setVisible(false);
                return;
            }
            if (key == "CapsLock") {
                m_capsLockActive = !m_capsLockActive;
                if (m_capsLockActive && m_shiftActive) {
                    m_shiftActive = false;
                }
                updateKeyLabels();

                // Also send physical CapsLock to the system
                auto info = getKeyInfo(key);
                m_controller->sendKey(false, info.second);
                return;
            }
            if (key == "Shift") {
                m_shiftActive = !m_shiftActive;
                if (m_shiftActive && m_capsLockActive) {
                    m_capsLockActive = false;
                    auto info        = getKeyInfo("CapsLock");
                    m_controller->sendKey(false, info.second);
                    m_controller->sendKey(true, info.second);
                }
                updateKeyLabels();
                return;
            }

            auto info = getKeyInfo(key);
            if (m_shiftActive) {
                m_controller->sendKey(false, 42); // Left Shift press
            }
            m_controller->sendKey(false, info.second);
        });
        connect(btn, &QPushButton::released, this, [this, key]() {
            if (key == "CapsLock") {
                auto info = getKeyInfo(key);
                m_controller->sendKey(true, info.second);
                return;
            }
            if (key == "Hide" || key == "Shift") {
                return;
            }

            auto info = getKeyInfo(key);
            m_controller->sendKey(true, info.second);
            if (m_shiftActive) {
                m_controller->sendKey(true, 42); // Left Shift release
                m_shiftActive = false;
                updateKeyLabels();
            }
        });
        return btn;
    };

    QString ctrlStyle = QString("background-color: %1; font-size: 16px; color: %2;").arg(COLOR_BG_SPECIAL).arg(COLOR_FG_SPECIAL);

    // Row 0: Numbers
    auto row0 = new QHBoxLayout();
    row0->setSpacing(3);
    row0->setAlignment(Qt::AlignCenter);
    for (const char* k : {"`", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "="}) {
        row0->addWidget(createKey(k));
    }
    row0->addWidget(createKey("Backspace", "⌫", 1.5, ctrlStyle));
    mainLayout->addLayout(row0);

    // Row 1: QWERTY
    auto row1 = new QHBoxLayout();
    row1->setSpacing(3);
    row1->setAlignment(Qt::AlignCenter);
    row1->addWidget(createKey("Tab", "⇥", 1.5, ctrlStyle));
    for (const char* k : {"Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "[", "]", "\\"}) {
        row1->addWidget(createKey(k));
    }
    mainLayout->addLayout(row1);

    // Row 2: ASDF
    auto row2 = new QHBoxLayout();
    row2->setSpacing(3);
    row2->setAlignment(Qt::AlignCenter);
    row2->addWidget(createKey("CapsLock", "⇪", 1.5, ctrlStyle));
    for (const char* k : {"A", "S", "D", "F", "G", "H", "J", "K", "L", ";", "'"}) {
        row2->addWidget(createKey(k));
    }
    row2->addWidget(createKey("Enter", "⏎", 2.0, QString("background-color: %1; color: white;").arg(COLOR_BG_ACTIVE)));
    mainLayout->addLayout(row2);

    // Row 3: ZXCV
    auto row3 = new QHBoxLayout();
    row3->setSpacing(3);
    row3->setAlignment(Qt::AlignCenter);
    row3->addWidget(createKey("Shift", "⇧", 1.5, ctrlStyle));
    for (const char* k : {"Z", "X", "C", "V", "B", "N", "M", ",", ".", "/"}) {
        row3->addWidget(createKey(k));
    }

    // Up Arrow at the end of Row 3
    auto arrowStyle = QString("background-color: #2a2a2a; color: #aaaaaa; font-size: 18px;");
    row3->addWidget(createKey("Up", "↑", 1.25, arrowStyle));
    mainLayout->addLayout(row3);

    // Row 4: Bottom
    auto row4 = new QHBoxLayout();
    row4->setSpacing(3);
    row4->setAlignment(Qt::AlignCenter);
    row4->addWidget(createKey("Hide", "⌨↓", 1.5, ctrlStyle));
    row4->addWidget(createKey("Super", "⊞", 1.5, ctrlStyle));
    row4->addWidget(createKey("Space", "␣", 7.5));

    // Other Arrows
    row4->addWidget(createKey("Left", "←", 1.25, arrowStyle));
    row4->addWidget(createKey("Down", "↓", 1.25, arrowStyle));
    row4->addWidget(createKey("Right", "→", 1.25, arrowStyle));

    mainLayout->addLayout(row4);

    updateKeyLabels();
}
