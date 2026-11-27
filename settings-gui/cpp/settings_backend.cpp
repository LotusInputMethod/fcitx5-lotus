/*
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "settings_backend.h"

#include "i18n_bridge.h"
#include "lotus-mode.h"

#include <data/input_method_data.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QMetaObject>
#include <QProcess>
#include <QRunnable>
#include <QSaveFile>
#include <QSet>
#include <QStringDecoder>
#include <QSysInfo>
#include <QTextStream>
#include <QThreadPool>
#include <QUrl>

#include <algorithm>
#include <array>
#include <libintl.h>
#include <pwd.h>
#include <unistd.h>
#include <utility>

#ifndef DICTIONARY_PATH
#define DICTIONARY_PATH "/usr/share/fcitx5/lotus/vietnamese.cm.dict"
#endif

namespace {
    constexpr int kMaxDelayMs        = 1000;
    constexpr int kResetPreviewCount = 4;
    constexpr int kLogTailLines      = 200;
    constexpr int kCmdTimeoutMs      = 5000;
    constexpr int kDiagnoseTimeoutMs = 10000;

    struct KeyValuePair {
        const char* key;
        const char* value;
    };

    constexpr auto kBambooActions = std::to_array<KeyValuePair>({
        {.key = "XoaDauThanh", .value = "Xóa dấu thanh"},
        {.key = "DauSac", .value = "Dấu sắc"},
        {.key = "DauHuyen", .value = "Dấu huyền"},
        {.key = "DauHoi", .value = "Dấu hỏi"},
        {.key = "DauNga", .value = "Dấu ngã"},
        {.key = "DauNang", .value = "Dấu nặng"},
        {.key = "A_Â", .value = "a -> â"},
        {.key = "E_Ê", .value = "e -> ê"},
        {.key = "O_Ô", .value = "o -> ô"},
        {.key = "AEO_ÂÊÔ", .value = "a/e/o -> â/ê/ô"},
        {.key = "UOA_ƯƠĂ", .value = "u/o/a -> ư/ơ/ă"},
        {.key = "D_Đ", .value = "d -> đ"},
        {.key = "UO_ƯƠ", .value = "u/o -> ư/ơ"},
        {.key = "A_Ă", .value = "a -> ă"},
        {.key = "__ă", .value = "ă"},
        {.key = "_Ă", .value = "Ă"},
        {.key = "__â", .value = "â"},
        {.key = "_Â", .value = "Â"},
        {.key = "__ê", .value = "ê"},
        {.key = "_Ê", .value = "Ê"},
        {.key = "__ô", .value = "ô"},
        {.key = "_Ô", .value = "Ô"},
        {.key = "__ư", .value = "ư"},
        {.key = "_Ư", .value = "Ư"},
        {.key = "__ơ", .value = "ơ"},
        {.key = "_Ơ", .value = "Ơ"},
        {.key = "__đ", .value = "đ"},
        {.key = "_Đ", .value = "Đ"},
        {.key = "UOA_ƯƠĂ__Ư", .value = "u/o/a -> ư/ơ/ă, ư"},
    });

    bool           asBool(const QVariant& val, bool fallback = false) {
        if (!val.isValid() || val.isNull()) {
            return fallback;
        }
        if (val.metaType() == QMetaType::fromType<bool>()) {
            return val.toBool();
        }
        const QString s = val.toString().trimmed().toLower();
        if (s == QLatin1String("true") || s == QLatin1String("1")) {
            return true;
        }
        if (s == QLatin1String("false") || s == QLatin1String("0")) {
            return false;
        }
        return fallback;
    }

    int asInt(const QVariant& val, int fallback = 0) {
        if (!val.isValid() || val.isNull()) {
            return fallback;
        }
        bool      ok     = false;
        const int parsed = val.toInt(&ok);
        return ok ? parsed : fallback;
    }

    QString extractHotkeyString(const QVariant& val, const QString& fallback = QString()) {
        if (!val.isValid() || val.isNull()) {
            return fallback;
        }
        if (val.metaType() == QMetaType::fromType<QVariantMap>()) {
            const QVariantMap map = val.toMap();
            if (map.contains(QStringLiteral("0"))) {
                return map.value(QStringLiteral("0")).toString();
            }
            for (auto it = map.cbegin(); it != map.cend(); ++it) {
                const QString s = it.value().toString();
                if (!s.isEmpty()) {
                    return s;
                }
            }
            return {};
        }
        if (val.metaType() == QMetaType::fromType<QVariantList>()) {
            const QVariantList list = val.toList();
            return list.isEmpty() ? QString() : list.constFirst().toString();
        }
        return val.toString();
    }

    int normalizeAppMode(int mode) {
        return fcitx::modeToInt(fcitx::intToMode(mode));
    }

    QString resolveLocalFilePath(const QString& fileUrlOrPath) {
        if (fileUrlOrPath.isEmpty()) {
            return {};
        }
        const QUrl url(fileUrlOrPath);
        if (url.isLocalFile()) {
            return url.toLocalFile();
        }
        return fileUrlOrPath;
    }

    QString localDictPath() {
        QString xdgDataHome = qEnvironmentVariable("XDG_DATA_HOME");
        if (xdgDataHome.isEmpty()) {
            xdgDataHome = QDir::home().filePath(QStringLiteral(".local/share"));
        }
        return QDir(xdgDataHome).filePath(QStringLiteral("fcitx5/lotus/vietnamese.cm.dict"));
    }

    QString globalDictPath() {
        const QStringList candidates = {
            QStringLiteral(DICTIONARY_PATH),
            QStringLiteral("/usr/share/fcitx5/lotus/vietnamese.cm.dict"),
            QStringLiteral("/usr/local/share/fcitx5/lotus/vietnamese.cm.dict"),
        };
        for (const QString& path : candidates) {
            if (QFileInfo::exists(path)) {
                return path;
            }
        }
        return candidates.constFirst();
    }

    bool readDictFile(const QString& path, QStringList& outWords) {
        outWords.clear();
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return false;
        }
        const QByteArray raw = file.readAll();
        QStringDecoder   decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
        const QString    decoded = decoder.decode(raw);
        if (decoder.hasError()) {
            return false;
        }
        const QStringList lines = decoded.split(QLatin1Char('\n'));
        for (const QString& line : lines) {
            const QString word = line.trimmed();
            if (!word.isEmpty() && !word.startsWith(QLatin1Char('#'))) {
                outWords.append(word);
            }
        }
        return true;
    }

    bool writeDictFileAtomic(const QString& path, const QStringList& words) {
        const QFileInfo fi(path);
        QDir().mkpath(fi.absolutePath());
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            return false;
        }
        for (const QString& raw : words) {
            const QString w = raw.trimmed();
            if (!w.isEmpty()) {
                file.write(w.toUtf8());
                file.write("\n");
            }
        }
        return file.commit();
    }

    bool writeRawTextAtomic(const QString& path, const QByteArray& utf8Content) {
        const QFileInfo fi(path);
        QDir().mkpath(fi.absolutePath());
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            return false;
        }
        if (file.write(utf8Content) != utf8Content.size()) {
            file.cancelWriting();
            return false;
        }
        return file.commit();
    }

    QStringList sortedWordCopy(QStringList list) {
        std::ranges::sort(list);
        return list;
    }

    const QHash<QString, QString>& hotkeySymMap() {
        static const QHash<QString, QString> kMap = {
            {QStringLiteral("grave"), QStringLiteral("`")},
            {QStringLiteral("minus"), QStringLiteral("-")},
            {QStringLiteral("equal"), QStringLiteral("=")},
            {QStringLiteral("bracketleft"), QStringLiteral("[")},
            {QStringLiteral("bracketright"), QStringLiteral("]")},
            {QStringLiteral("backslash"), QStringLiteral("\\")},
            {QStringLiteral("semicolon"), QStringLiteral(";")},
            {QStringLiteral("apostrophe"), QStringLiteral("'")},
            {QStringLiteral("comma"), QStringLiteral(",")},
            {QStringLiteral("period"), QStringLiteral(".")},
            {QStringLiteral("slash"), QStringLiteral("/")},
            {QStringLiteral("asciitilde"), QStringLiteral("~")},
            {QStringLiteral("underscore"), QStringLiteral("_")},
            {QStringLiteral("plus"), QStringLiteral("+")},
            {QStringLiteral("braceleft"), QStringLiteral("{")},
            {QStringLiteral("braceright"), QStringLiteral("}")},
            {QStringLiteral("bar"), QStringLiteral("|")},
            {QStringLiteral("colon"), QStringLiteral(":")},
            {QStringLiteral("quotedbl"), QStringLiteral("\"")},
            {QStringLiteral("less"), QStringLiteral("<")},
            {QStringLiteral("greater"), QStringLiteral(">")},
            {QStringLiteral("question"), QStringLiteral("?")},
            {QStringLiteral("exclam"), QStringLiteral("!")},
            {QStringLiteral("at"), QStringLiteral("@")},
            {QStringLiteral("numbersign"), QStringLiteral("#")},
            {QStringLiteral("dollar"), QStringLiteral("$")},
            {QStringLiteral("percent"), QStringLiteral("%")},
            {QStringLiteral("asciicircum"), QStringLiteral("^")},
            {QStringLiteral("ampersand"), QStringLiteral("&")},
            {QStringLiteral("asterisk"), QStringLiteral("*")},
            {QStringLiteral("parenleft"), QStringLiteral("(")},
            {QStringLiteral("parenright"), QStringLiteral(")")},
            {QStringLiteral("space"), QStringLiteral("Space")},
            {QStringLiteral("Control"), QStringLiteral("Ctrl")},
            {QStringLiteral("Escape"), QStringLiteral("Esc")},
            {QStringLiteral("Return"), QStringLiteral("Enter")},
            {QStringLiteral("BackSpace"), QStringLiteral("Backspace")},
            {QStringLiteral("Delete"), QStringLiteral("Del")},
            {QStringLiteral("Insert"), QStringLiteral("Ins")},
            {QStringLiteral("ISO_Left_Tab"), QStringLiteral("Tab")},
            {QStringLiteral("Shift_L"), QStringLiteral("L-Shift")},
            {QStringLiteral("Shift_R"), QStringLiteral("R-Shift")},
            {QStringLiteral("Control_L"), QStringLiteral("L-Ctrl")},
            {QStringLiteral("Control_R"), QStringLiteral("R-Ctrl")},
            {QStringLiteral("Alt_L"), QStringLiteral("L-Alt")},
            {QStringLiteral("Alt_R"), QStringLiteral("R-Alt")},
            {QStringLiteral("Super_L"), QStringLiteral("L-Super")},
            {QStringLiteral("Super_R"), QStringLiteral("R-Super")},
            {QStringLiteral("Meta_L"), QStringLiteral("L-Super")},
            {QStringLiteral("Meta_R"), QStringLiteral("R-Super")},
        };
        return kMap;
    }

    const QHash<QString, QString>& hotkeyUiUnshiftMap() {
        static const QHash<QString, QString> kMap = [] {
            QHash<QString, QString> m = {
                {QStringLiteral("asciitilde"), QStringLiteral("grave")},
                {QStringLiteral("exclam"), QStringLiteral("1")},
                {QStringLiteral("at"), QStringLiteral("2")},
                {QStringLiteral("numbersign"), QStringLiteral("3")},
                {QStringLiteral("dollar"), QStringLiteral("4")},
                {QStringLiteral("percent"), QStringLiteral("5")},
                {QStringLiteral("asciicircum"), QStringLiteral("6")},
                {QStringLiteral("ampersand"), QStringLiteral("7")},
                {QStringLiteral("asterisk"), QStringLiteral("8")},
                {QStringLiteral("parenleft"), QStringLiteral("9")},
                {QStringLiteral("parenright"), QStringLiteral("0")},
                {QStringLiteral("underscore"), QStringLiteral("minus")},
                {QStringLiteral("plus"), QStringLiteral("equal")},
                {QStringLiteral("braceleft"), QStringLiteral("bracketleft")},
                {QStringLiteral("braceright"), QStringLiteral("bracketright")},
                {QStringLiteral("bar"), QStringLiteral("backslash")},
                {QStringLiteral("colon"), QStringLiteral("semicolon")},
                {QStringLiteral("quotedbl"), QStringLiteral("apostrophe")},
                {QStringLiteral("less"), QStringLiteral("comma")},
                {QStringLiteral("greater"), QStringLiteral("period")},
                {QStringLiteral("question"), QStringLiteral("slash")},
            };
            for (char ch = 'a'; ch <= 'z'; ++ch) {
                const QString upper(QChar::fromLatin1(static_cast<char>(ch - ('a' - 'A'))));
                const QString lower(QChar::fromLatin1(ch));
                m.insert(upper, lower);
            }
            return m;
        }();
        return kMap;
    }

    QString capitalizeKeyName(const QString& key) {
        if (key.size() <= 1) {
            return key;
        }
        return key.at(0).toUpper() + key.mid(1).toLower();
    }

    bool isSpecialSingleKey(const QString& name) {
        static const QSet<QString> kSpecial = {
            QStringLiteral("grave"), QStringLiteral("space"), QStringLiteral("Tab"), QStringLiteral("Return"), QStringLiteral("F1"), QStringLiteral("F2"),
            QStringLiteral("F3"),    QStringLiteral("F4"),    QStringLiteral("F5"),  QStringLiteral("F6"),     QStringLiteral("F7"), QStringLiteral("F8"),
            QStringLiteral("F9"),    QStringLiteral("F10"),   QStringLiteral("F11"), QStringLiteral("F12"),
        };
        return kSpecial.contains(name);
    }

    QString qtKeyToFcitxKeysym(int qtKey, int qtModifiers, const QString& text) {
        switch (qtKey) {
            case Qt::Key_QuoteLeft: return QStringLiteral("grave");
            case Qt::Key_AsciiTilde: return QStringLiteral("asciitilde");
            case Qt::Key_Space: return QStringLiteral("space");
            case Qt::Key_Tab:
            case Qt::Key_Backtab: return QStringLiteral("Tab");
            case Qt::Key_Return:
            case Qt::Key_Enter: return QStringLiteral("Return");
            case Qt::Key_Backspace: return QStringLiteral("BackSpace");
            case Qt::Key_Delete: return QStringLiteral("Delete");
            case Qt::Key_Insert: return QStringLiteral("Insert");
            case Qt::Key_Home: return QStringLiteral("Home");
            case Qt::Key_End: return QStringLiteral("End");
            case Qt::Key_PageUp: return QStringLiteral("Page_Up");
            case Qt::Key_PageDown: return QStringLiteral("Page_Down");
            case Qt::Key_Left: return QStringLiteral("Left");
            case Qt::Key_Up: return QStringLiteral("Up");
            case Qt::Key_Right: return QStringLiteral("Right");
            case Qt::Key_Down: return QStringLiteral("Down");
            case Qt::Key_CapsLock: return QStringLiteral("Caps_Lock");
            case Qt::Key_NumLock: return QStringLiteral("Num_Lock");
            case Qt::Key_ScrollLock: return QStringLiteral("Scroll_Lock");
            case Qt::Key_Pause: return QStringLiteral("Pause");
            case Qt::Key_Print: return QStringLiteral("Print");
            case Qt::Key_Menu: return QStringLiteral("Menu");
            case Qt::Key_Exclam: return QStringLiteral("exclam");
            case Qt::Key_QuoteDbl: return QStringLiteral("quotedbl");
            case Qt::Key_NumberSign: return QStringLiteral("numbersign");
            case Qt::Key_Dollar: return QStringLiteral("dollar");
            case Qt::Key_Percent: return QStringLiteral("percent");
            case Qt::Key_Ampersand: return QStringLiteral("ampersand");
            case Qt::Key_Apostrophe: return QStringLiteral("apostrophe");
            case Qt::Key_ParenLeft: return QStringLiteral("parenleft");
            case Qt::Key_ParenRight: return QStringLiteral("parenright");
            case Qt::Key_Asterisk: return QStringLiteral("asterisk");
            case Qt::Key_Plus: return QStringLiteral("plus");
            case Qt::Key_Comma: return QStringLiteral("comma");
            case Qt::Key_Minus: return QStringLiteral("minus");
            case Qt::Key_Period: return QStringLiteral("period");
            case Qt::Key_Slash: return QStringLiteral("slash");
            case Qt::Key_Colon: return QStringLiteral("colon");
            case Qt::Key_Semicolon: return QStringLiteral("semicolon");
            case Qt::Key_Less: return QStringLiteral("less");
            case Qt::Key_Equal: return QStringLiteral("equal");
            case Qt::Key_Greater: return QStringLiteral("greater");
            case Qt::Key_Question: return QStringLiteral("question");
            case Qt::Key_At: return QStringLiteral("at");
            case Qt::Key_BracketLeft: return QStringLiteral("bracketleft");
            case Qt::Key_Backslash: return QStringLiteral("backslash");
            case Qt::Key_BracketRight: return QStringLiteral("bracketright");
            case Qt::Key_AsciiCircum: return QStringLiteral("asciicircum");
            case Qt::Key_Underscore: return QStringLiteral("underscore");
            case Qt::Key_BraceLeft: return QStringLiteral("braceleft");
            case Qt::Key_Bar: return QStringLiteral("bar");
            case Qt::Key_BraceRight: return QStringLiteral("braceright");
            default: break;
        }

        if (qtKey >= Qt::Key_F1 && qtKey <= Qt::Key_F35) {
            return QStringLiteral("F") + QString::number(qtKey - Qt::Key_F1 + 1);
        }
        if (qtKey >= Qt::Key_0 && qtKey <= Qt::Key_9) {
            return {QChar::fromLatin1(static_cast<char>('0' + (qtKey - Qt::Key_0)))};
        }
        if (qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z) {
            const auto         modFlags  = static_cast<unsigned int>(qtModifiers);
            const unsigned int upperMask = static_cast<unsigned int>(Qt::ShiftModifier) | static_cast<unsigned int>(Qt::ControlModifier) |
                static_cast<unsigned int>(Qt::AltModifier) | static_cast<unsigned int>(Qt::MetaModifier);
            const bool upper = (modFlags & upperMask) != 0U;
            const char base  = static_cast<char>((upper ? 'A' : 'a') + (qtKey - Qt::Key_A));
            return {QChar::fromLatin1(base)};
        }

        if (text.size() == 1 && text.at(0).isPrint()) {
            return text;
        }
        return QKeySequence(qtKey).toString();
    }

    void sortAppRules(QVariantList& rules) {
        std::ranges::sort(rules, [](const QVariant& a, const QVariant& b) {
            return a.toMap().value(QStringLiteral("app")).toString().toLower() < b.toMap().value(QStringLiteral("app")).toString().toLower();
        });
    }

    void sortMacros(QVariantList& macros) {
        std::ranges::sort(macros, [](const QVariant& a, const QVariant& b) {
            const QString ka  = a.toMap().value(QStringLiteral("key")).toString();
            const QString kb  = b.toMap().value(QStringLiteral("key")).toString();
            const int     cmp = QString::compare(ka, kb, Qt::CaseInsensitive);
            return (cmp != 0) ? (cmp < 0) : (ka < kb);
        });
    }

    QString runCommandOutput(const QString& program, const QStringList& args, int timeoutMs) {
        QProcess proc;
        proc.start(program, args);
        if (!proc.waitForStarted(timeoutMs)) {
            if (proc.error() == QProcess::FailedToStart) {
                return QStringLiteral("(command not found: %1)").arg(program);
            }
            return QStringLiteral("(error starting %1: %2)").arg(program, proc.errorString());
        }
        if (!proc.waitForFinished(timeoutMs)) {
            proc.kill();
            proc.waitForFinished(500);
            return QStringLiteral("(timed out after %1s)").arg(timeoutMs / 1000);
        }
        QString out = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
        QString err = QString::fromUtf8(proc.readAllStandardError()).trimmed();
        QString result;
        if (!out.isEmpty() && !err.isEmpty()) {
            result = out + QLatin1Char('\n') + err;
        } else if (!out.isEmpty()) {
            result = std::move(out);
        } else if (!err.isEmpty()) {
            result = std::move(err);
        } else {
            result = QStringLiteral("(exit %1, no output)").arg(proc.exitCode());
        }
        constexpr qsizetype kMaxCommandOutput = qsizetype{2} * 1024 * 1024;
        if (result.size() > kMaxCommandOutput) {
            result = QStringLiteral("(output truncated)\n") + result.right(kMaxCommandOutput);
        }
        return result;
    }

    QString currentUserName() {
        QString user = qEnvironmentVariable("USER");
        if (!user.isEmpty()) {
            return user;
        }
        if (const struct passwd* pw = ::getpwuid(::getuid())) {
            return QString::fromLocal8Bit(pw->pw_name);
        }
        return QStringLiteral("unknown");
    }

    QString collectDebugReport(const QString& version, const QVariantMap& configValues, bool hasConfig, bool fcitxOffline) {
        const QString user = currentUserName();
        QStringList   sections;

        sections.append(QStringList{
            QStringLiteral("=== Fcitx5 Lotus Debug Information ==="),
            QStringLiteral("Version: ") + version,
            QStringLiteral("Timestamp: ") + QDateTime::currentDateTime().toString(Qt::ISODate),
            QStringLiteral("User: ") + user,
        }
                            .join(QLatin1Char('\n')));

        QString osRelease = QStringLiteral("(unavailable)");
        QFile   osFile(QStringLiteral("/etc/os-release"));
        if (osFile.exists()) {
            if (osFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                osRelease = QString::fromUtf8(osFile.readAll()).trimmed();
            } else {
                osRelease = QStringLiteral("(error reading /etc/os-release: %1)").arg(osFile.errorString());
            }
        }

        const QStringList envKeys = {
            QStringLiteral("XDG_CURRENT_DESKTOP"), QStringLiteral("XDG_SESSION_TYPE"), QStringLiteral("WAYLAND_DISPLAY"), QStringLiteral("DISPLAY"),
            QStringLiteral("QT_IM_MODULE"),        QStringLiteral("GTK_IM_MODULE"),    QStringLiteral("XMODIFIERS"),
        };
        QStringList sysLines = {
            QStringLiteral("=== System & Environment ==="),
            QStringLiteral("Kernel: %1 %2 %3 (%4)").arg(QSysInfo::kernelType(), QSysInfo::kernelVersion(), QSysInfo::currentCpuArchitecture(), QSysInfo::prettyProductName()),
            QStringLiteral("--- /etc/os-release ---"),
            osRelease,
            QStringLiteral("--- Environment ---"),
        };
        for (const QString& k : envKeys) {
            sysLines.append(k + QLatin1Char('=') + qEnvironmentVariable(k.toUtf8().constData()));
        }
        sections.append(sysLines.join(QLatin1Char('\n')));

        QString configText;
        if (!fcitxOffline && hasConfig) {
            const QJsonObject obj = QJsonObject::fromVariantMap(configValues);
            configText            = QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Indented)).trimmed();
        } else {
            configText = QStringLiteral("(empty or Fcitx5 not running)");
        }
        sections.append(QStringLiteral("=== Lotus Configuration ===\n") + configText);

        const QString unitName    = QStringLiteral("fcitx5-lotus-server@%1.service").arg(user);
        const QString journalUnit = runCommandOutput(
            QStringLiteral("journalctl"), {QStringLiteral("-xeu"), unitName, QStringLiteral("--no-pager"), QStringLiteral("-n"), QString::number(kLogTailLines)}, kCmdTimeoutMs);
        sections.append(QStringLiteral("=== Uinput Server Unit Journal (%1) ===\n%2").arg(unitName, journalUnit));

        const QString diagnoseOut = runCommandOutput(QStringLiteral("fcitx5-diagnose"), {}, kDiagnoseTimeoutMs);
        sections.append(QStringLiteral("=== fcitx5-diagnose ===\n") + diagnoseOut);

        return sections.join(QStringLiteral("\n\n")) + QLatin1Char('\n');
    }
} // namespace

SettingsBackend::SettingsBackend(I18nBridge* i18n, QObject* parent) : QObject(parent), i18n_(i18n) {
    scanDesktopEntries();
    loadAll();
}

SettingsBackend::~SettingsBackend() {
    const std::scoped_lock lock(aliveGuard_->mutex);
    aliveGuard_->alive = false;
}

QString SettingsBackend::translate(const char* text) const {
    if (text == nullptr || *text == '\0') {
        return {};
    }
    if (i18n_ != nullptr) {
        return i18n_->tr(QString::fromUtf8(text));
    }
    return I18nBridge::lookup(QString::fromUtf8(text));
}

QString SettingsBackend::translate(const QString& text) const {
    if (text.isEmpty()) {
        return text;
    }
    if (i18n_ != nullptr) {
        return i18n_->tr(text);
    }
    return I18nBridge::lookup(text);
}

bool SettingsBackend::fcitxOffline() const {
    return fcitxOffline_;
}

bool SettingsBackend::hasUnsavedChanges() const {
    return hasUnsavedChanges_;
}

bool SettingsBackend::hasValidationErrors() const {
    return hasValidationErrors_;
}

QString SettingsBackend::validationMessage() const {
    return validationMessage_;
}

QString SettingsBackend::appVersion() {
    return QStringLiteral(LOTUS_VERSION);
}

QVariantMap SettingsBackend::values() const {
    return values_;
}

QVariantList SettingsBackend::modeList() const {
    return modeList_;
}

QVariantList SettingsBackend::appRules() const {
    return appRules_;
}

QVariantList SettingsBackend::macros() const {
    return macros_;
}

QVariantList SettingsBackend::dictionaryWords() const {
    return cachedDictionaryWords_;
}

int SettingsBackend::totalWordCount() const {
    return static_cast<int>(dictionaryWords_.size());
}

QVariantList SettingsBackend::keymapBindings() const {
    return keymapBindings_;
}

QVariantList SettingsBackend::keymapPresets() const {
    return keymapPresets_;
}

QVariantList SettingsBackend::bambooActions() const {
    return bambooActions_;
}

void SettingsBackend::retryConnection() {
    dbusClient_.reconnect();
    const LotusDBusClient::ConfigData cfg = dbusClient_.getConfig();
    if (!dbusClient_.isConnected() || !cfg.ok) {
        fcitxOffline_ = true;
        emit stateChanged();
        return;
    }

    if (!hasUnsavedChanges_) {
        applyLoadedConfig(cfg);
        emit stateChanged();
        return;
    }

    const QVariantMap  stagedValues     = values_;
    const QVariantList stagedAppRules   = appRules_;
    const QVariantList stagedMacros     = macros_;
    const QVariantList stagedKeymaps    = keymapBindings_;
    const QStringList  stagedDictionary = dictionaryWords_;

    applyLoadedConfig(cfg);

    values_          = stagedValues;
    appRules_        = stagedAppRules;
    macros_          = stagedMacros;
    keymapBindings_  = stagedKeymaps;
    dictionaryWords_ = stagedDictionary;
    rebuildCachedDictionaryWords();
    rebuildModeList();
    recomputeDerivedState();
    emit stateChanged();
}

void SettingsBackend::applyLoadedConfig(const LotusDBusClient::ConfigData& cfg) {
    fcitxOffline_ = !dbusClient_.isConnected() || !cfg.ok;
    parseMetadata(cfg.metadata);
    populateValuesFromConfig(cfg.values);
    rebuildKeymapCatalog();

    loadAppRules();
    loadMacros();
    loadDictionary();
    loadKeymaps();

    rebuildModeList();
    recomputeDerivedState();
}

void SettingsBackend::loadAll() {
    applyLoadedConfig(dbusClient_.getConfig());
}

void SettingsBackend::parseMetadata(const QVariantList& rawMetadata) {
    metadata_.clear();
    for (const QVariant& sectionVar : rawMetadata) {
        const QVariantList section = sectionVar.toList();
        if (section.size() < 2) {
            continue;
        }
        const QVariantList items = section.at(1).toList();
        for (const QVariant& itemVar : items) {
            const QVariantList item = itemVar.toList();
            if (item.size() < 5) {
                continue;
            }
            MetadataItem meta;
            meta.type         = item.at(1).toString();
            meta.title        = item.at(2).toString();
            meta.defaultValue = item.at(3);
            meta.extra        = item.at(4).toMap();
            metadata_.insert(item.at(0).toString(), meta);
        }
    }
}

QVariant SettingsBackend::defaultTypedValue(const QString& key) const {
    const auto metaIt = metadata_.constFind(key);
    if (metaIt == metadata_.cend()) {
        return {};
    }
    return normalizeConfigValue(key, metaIt->defaultValue);
}

QVariant SettingsBackend::normalizeConfigValue(const QString& key, const QVariant& raw) const {
    const auto metaIt = metadata_.constFind(key);
    if (metaIt == metadata_.cend()) {
        return raw.toString();
    }
    if (metaIt->type == QLatin1String("Boolean")) {
        return asBool(raw, false);
    }
    if (metaIt->type == QLatin1String("Integer")) {
        return std::clamp(asInt(raw, asInt(metaIt->defaultValue)), 0, kMaxDelayMs);
    }
    if (metaIt->type == QLatin1String("Key") || metaIt->type.startsWith(QLatin1String("List|Key"))) {
        return extractHotkeyString(raw);
    }
    return raw.toString();
}

QVariant SettingsBackend::toWireValue(const QString& key, const QVariant& typedVal) const {
    const auto metaIt = metadata_.constFind(key);
    if (metaIt == metadata_.cend()) {
        return typedVal.toString();
    }
    if (metaIt->type == QLatin1String("Boolean")) {
        return asBool(typedVal) ? QStringLiteral("True") : QStringLiteral("False");
    }
    if (metaIt->type == QLatin1String("Integer")) {
        return QString::number(std::clamp(asInt(typedVal), 0, kMaxDelayMs));
    }
    if (metaIt->type == QLatin1String("Key") || metaIt->type.startsWith(QLatin1String("List|Key"))) {
        const QString hk = typedVal.toString().trimmed();
        if (hk.isEmpty()) {
            return QVariantMap();
        }
        return QVariantMap{{QStringLiteral("0"), hk}};
    }
    return typedVal.toString();
}

QVariantMap SettingsBackend::buildWireConfigPayload(const QVariantMap& baseValues, const QStringList& keysToUpdate) const {
    QVariantMap out = baseValues;
    for (const QString& key : keysToUpdate) {
        out.insert(key, toWireValue(key, values_.value(key, defaultTypedValue(key))));
    }
    return out;
}

void SettingsBackend::populateValuesFromConfig(const QVariantMap& rawValues) {
    values_.clear();
    for (auto it = metadata_.cbegin(); it != metadata_.cend(); ++it) {
        const QString& key = it.key();
        if (rawValues.contains(key)) {
            values_.insert(key, normalizeConfigValue(key, rawValues.value(key)));
        } else {
            values_.insert(key, defaultTypedValue(key));
        }
    }
    initialValues_ = values_;
}

void SettingsBackend::loadAppRules() {
    appRules_.clear();
    const QVariantList rawRules = dbusClient_.getSubConfigList(QStringLiteral("app_rules"), QStringLiteral("Rules"));
    for (const QVariant& itemVar : rawRules) {
        const QVariantMap item = itemVar.toMap();
        const QString     app  = item.value(QStringLiteral("App")).toString().trimmed();
        if (app.isEmpty()) {
            continue;
        }
        const int mode      = normalizeAppMode(asInt(item.value(QStringLiteral("Mode")), fcitx::modeToInt(fcitx::LotusMode::Smooth)));
        const int commit    = std::clamp(asInt(item.value(QStringLiteral("CommitInterval")), 0), 0, kMaxDelayMs);
        const int backspace = std::clamp(asInt(item.value(QStringLiteral("BackspaceInterval")), 0), 0, kMaxDelayMs);
        const int post      = std::clamp(asInt(item.value(QStringLiteral("PostDelay")), 0), 0, kMaxDelayMs);

        appRules_.append(QVariantMap{
            {QStringLiteral("app"), app},
            {QStringLiteral("displayName"), resolveAppDisplayName(app)},
            {QStringLiteral("iconName"), resolveAppIconName(app)},
            {QStringLiteral("mode"), mode},
            {QStringLiteral("commit"), commit},
            {QStringLiteral("backspace"), backspace},
            {QStringLiteral("post"), post},
        });
    }
    sortAppRules(appRules_);
    initialAppRules_ = appRules_;
}

void SettingsBackend::loadMacros() {
    macros_.clear();
    const QVariantList rawMacros = dbusClient_.getSubConfigList(QStringLiteral("lotus-macro"), QStringLiteral("Macro"));
    for (const QVariant& itemVar : rawMacros) {
        const QVariantMap item = itemVar.toMap();
        const QString     key  = item.value(QStringLiteral("Key")).toString().trimmed();
        const QString     val  = item.value(QStringLiteral("Value")).toString();
        if (key.isEmpty()) {
            continue;
        }
        macros_.append(QVariantMap{
            {QStringLiteral("key"), key},
            {QStringLiteral("value"), val},
            {QStringLiteral("invalid"), !isValidMacroKey(key)},
        });
    }
    sortMacros(macros_);
    initialMacros_ = macros_;
}

void SettingsBackend::loadDictionary() {
    dictLoadFailed_ = false;
    globalDictionaryWords_.clear();
    const QString gPath = globalDictPath();
    if (QFileInfo::exists(gPath)) {
        readDictFile(gPath, globalDictionaryWords_);
    }

    dictionaryWords_.clear();
    const QString lPath      = localDictPath();
    const QString pathToRead = QFileInfo::exists(lPath) ? lPath : gPath;
    if (QFileInfo::exists(pathToRead) && !readDictFile(pathToRead, dictionaryWords_)) {
        dictLoadFailed_ = true;
        dictionaryWords_.clear();
    }

    initialDictionaryWords_ = dictionaryWords_;
    rebuildCachedDictionaryWords();
}

void SettingsBackend::loadKeymaps() {
    keymapBindings_.clear();
    const QVariantList rawKeymaps = dbusClient_.getSubConfigList(QStringLiteral("custom_keymap"), QStringLiteral("CustomKeymap"));
    for (const QVariant& itemVar : rawKeymaps) {
        const QVariantMap item = itemVar.toMap();
        const QString     key  = item.value(QStringLiteral("Key")).toString().trimmed();
        const QString     val  = item.value(QStringLiteral("Value")).toString();
        if (key.isEmpty()) {
            continue;
        }
        keymapBindings_.append(QVariantMap{
            {QStringLiteral("key"), key},
            {QStringLiteral("value"), val},
        });
    }
    initialKeymapBindings_ = keymapBindings_;
}

void SettingsBackend::rebuildKeymapCatalog() {
    keymapPresets_.clear();
    bambooActions_.clear();

    const auto        imIt   = metadata_.constFind(QStringLiteral("InputMethod"));
    const QVariantMap labels = imIt != metadata_.cend() ? imIt->extra.value(QStringLiteral("EnumI18n")).toMap() : QVariantMap{};
    QSet<QString>     actionCodes;

    for (std::size_t i = 0; i < lotus::data::kInputMethodCount; ++i) {
        const auto& definition = lotus::data::kInputMethodDefinitions.at(i);
        if (i == static_cast<std::size_t>(lotus::data::InputMethodId::Custom)) {
            continue;
        }
        std::vector<std::string> keys;
        keys.reserve(definition.size());
        for (const auto& [key, value] : definition) {
            keys.push_back(key);
            actionCodes.insert(QString::fromStdString(value));
        }
        std::ranges::sort(keys);

        QVariantList rules;
        rules.reserve(static_cast<qsizetype>(keys.size()));
        for (const std::string& key : keys) {
            rules.append(QVariantMap{
                {QStringLiteral("key"), QString::fromStdString(key)},
                {QStringLiteral("value"), QString::fromStdString(definition.at(key))},
            });
        }

        const QString label = labels.value(QString::number(i)).toString();
        keymapPresets_.append(QVariantMap{
            {QStringLiteral("name"), label.isEmpty() ? QString::number(i) : label},
            {QStringLiteral("rules"), rules},
        });
    }

    QStringList sortedCodes(actionCodes.cbegin(), actionCodes.cend());
    sortedCodes.sort();
    for (const QString& code : std::as_const(sortedCodes)) {
        QString label = code;
        for (const KeyValuePair& action : kBambooActions) {
            if (code == QString::fromUtf8(action.key)) {
                label = QString::fromUtf8(action.value);
                break;
            }
        }
        bambooActions_.append(QVariantMap{
            {QStringLiteral("code"), code},
            {QStringLiteral("label"), label},
        });
    }
}

QStringList SettingsBackend::modeNames() const {
    QStringList names;
    for (auto it = metadata_.cbegin(); it != metadata_.cend(); ++it) {
        const QString& key = it.key();
        if (key.startsWith(QLatin1String("Shortcut")) && key.size() > 8) {
            names.append(key.mid(8));
        }
    }
    names.sort();
    return names;
}

QString SettingsBackend::modeTitle(const QString& suffix) const {
    const auto metaIt = metadata_.constFind(QStringLiteral("Shortcut") + suffix);
    if (metaIt == metadata_.cend()) {
        return suffix;
    }
    QString              title   = metaIt->title;
    static const QString kPrefix = QStringLiteral("Shortcut for ");
    if (title.startsWith(kPrefix)) {
        title.remove(0, kPrefix.size());
    }
    return translate(title);
}

void SettingsBackend::rebuildModeList() {
    const QStringList allNames = modeNames();
    const QString     orderStr = values_.value(QStringLiteral("ModeOrder")).toString();
    const QStringList rawOrder = orderStr.split(QLatin1Char(','), Qt::SkipEmptyParts);

    QStringList       orderedNames;
    for (const QString& raw : rawOrder) {
        const QString trimmed = raw.trimmed();
        if (allNames.contains(trimmed) && !orderedNames.contains(trimmed)) {
            orderedNames.append(trimmed);
        }
    }
    for (const QString& name : allNames) {
        if (!orderedNames.contains(name)) {
            orderedNames.append(name);
        }
    }

    QHash<QString, int> visibleShortcutCounts;
    for (const QString& name : std::as_const(orderedNames)) {
        const QString showKey = QStringLiteral("ShowMode") + name;
        const QString scKey   = QStringLiteral("Shortcut") + name;
        const bool    vis     = asBool(values_.value(showKey, asBool(defaultTypedValue(showKey), true)), true);
        const QString sc      = values_.value(scKey, defaultTypedValue(scKey)).toString();
        if (vis && !sc.isEmpty()) {
            visibleShortcutCounts[sc] += 1;
        }
    }

    modeList_.clear();
    modeList_.reserve(orderedNames.size());
    for (const QString& name : std::as_const(orderedNames)) {
        const QString showKey    = QStringLiteral("ShowMode") + name;
        const QString scKey      = QStringLiteral("Shortcut") + name;
        const QString defaultKey = defaultTypedValue(scKey).toString();
        const bool    vis        = asBool(values_.value(showKey, asBool(defaultTypedValue(showKey), true)), true);
        const QString sc         = values_.value(scKey, defaultKey).toString();
        const bool    conflict   = vis && !sc.isEmpty() && visibleShortcutCounts.value(sc) > 1;

        modeList_.append(QVariantMap{
            {QStringLiteral("name"), name},
            {QStringLiteral("title"), modeTitle(name)},
            {QStringLiteral("shortcutKey"), scKey},
            {QStringLiteral("showKey"), showKey},
            {QStringLiteral("shortcut"), sc},
            {QStringLiteral("visible"), vis},
            {QStringLiteral("defaultKey"), defaultKey},
            {QStringLiteral("conflict"), conflict},
        });
    }
}

void SettingsBackend::recomputeDerivedState() {
    dictDirtyInitial_   = sortedWordCopy(dictionaryWords_) != sortedWordCopy(initialDictionaryWords_);
    dictDirtyDefault_   = QFileInfo::exists(localDictPath()) || (sortedWordCopy(dictionaryWords_) != sortedWordCopy(globalDictionaryWords_));
    dictHasInvalidWord_ = std::ranges::any_of(dictionaryWords_, [](const QString& w) { return !isValidWord(w); });

    hasUnsavedChanges_ =
        (values_ != initialValues_) || (appRules_ != initialAppRules_) || (macros_ != initialMacros_) || dictDirtyInitial_ || (keymapBindings_ != initialKeymapBindings_);

    QStringList   errors;
    QSet<QString> reportedShortcuts;
    for (const QVariant& itemVar : std::as_const(modeList_)) {
        const QVariantMap item = itemVar.toMap();
        if (item.value(QStringLiteral("conflict")).toBool()) {
            const QString sc = item.value(QStringLiteral("shortcut")).toString();
            if (!reportedShortcuts.contains(sc)) {
                reportedShortcuts.insert(sc);
                QString msg = translate("Duplicate shortcut '{}' used for multiple enabled modes.");
                msg.replace(QStringLiteral("{}"), sc);
                errors.append(msg);
            }
        }
    }

    const bool hasInvalidMacro = std::ranges::any_of(macros_, [](const QVariant& itemVar) { return itemVar.toMap().value(QStringLiteral("invalid")).toBool(); });
    if (hasInvalidMacro && macros_ != initialMacros_) {
        errors.append(translate("Macro keys cannot contain spaces or special characters."));
    }

    if (dictDirtyInitial_ || dictDirtyDefault_) {
        if (dictLoadFailed_) {
            errors.append(translate("Cannot save dictionary because loading failed earlier. Please fix the file format first."));
        } else if (dictHasInvalidWord_) {
            errors.append(translate("Dictionary words should not contain spaces."));
        }
    }

    hasValidationErrors_ = !errors.isEmpty();
    validationMessage_   = errors.join(QLatin1Char('\n'));
}

void SettingsBackend::setValue(const QString& key, const QVariant& val) {
    if (key.isEmpty()) {
        return;
    }
    const QVariant normalized = normalizeConfigValue(key, val);
    if (values_.value(key) == normalized) {
        return;
    }
    values_.insert(key, normalized);
    if (key == QLatin1String("ModeOrder") || key.startsWith(QLatin1String("ShowMode")) || key.startsWith(QLatin1String("Shortcut"))) {
        rebuildModeList();
    }
    recomputeDerivedState();
    emit stateChanged();
}

QVariantList SettingsBackend::getAppModeOptions() const {
    QVariantList out;
    const auto   metaIt = metadata_.constFind(QStringLiteral("Mode"));
    if (metaIt == metadata_.cend()) {
        return out;
    }
    const QVariantMap enumMap  = metaIt->extra.value(QStringLiteral("Enum")).toMap();
    const QVariantMap enumI18n = metaIt->extra.value(QStringLiteral("EnumI18n")).toMap();
    QStringList       idxKeys  = enumMap.keys();
    std::ranges::sort(idxKeys, [](const QString& a, const QString& b) { return a.toInt() < b.toInt(); });
    for (const QString& idxKey : std::as_const(idxKeys)) {
        const auto mode = static_cast<fcitx::LotusMode>(idxKey.toInt());
        out.append(QVariantMap{
            {QStringLiteral("value"), fcitx::modeToInt(mode)},
            {QStringLiteral("label"), enumI18n.value(idxKey, enumMap.value(idxKey)).toString()},
        });
    }
    return out;
}

QString SettingsBackend::optionTitle(const QString& key) const {
    const auto metaIt = metadata_.constFind(key);
    return metaIt == metadata_.cend() ? key : translate(metaIt->title);
}

QString SettingsBackend::optionDescription(const QString& key) const {
    const auto metaIt = metadata_.constFind(key);
    if (metaIt == metadata_.cend()) {
        return key;
    }
    const QString tooltip = metaIt->extra.value(QStringLiteral("Tooltip")).toString();
    return translate(tooltip.isEmpty() ? metaIt->title : tooltip);
}

QVariant SettingsBackend::optionDefault(const QString& key) const {
    return defaultTypedValue(key);
}

QVariantList SettingsBackend::getEnumOptions(const QString& key) const {
    auto makeOption = [](const QString& val, const QString& label) { return QVariantMap{{QStringLiteral("value"), val}, {QStringLiteral("label"), label}}; };

    if (key == QLatin1String("TimeFormat")) {
        return {
            makeOption(QStringLiteral("%H:%M"), QStringLiteral("15:04 (24h)")),
            makeOption(QStringLiteral("%H:%M:%S"), QStringLiteral("15:04:05 (24h)")),
            makeOption(QStringLiteral("%I:%M %p"), QStringLiteral("03:04 PM")),
            makeOption(QStringLiteral("%I:%M:%S %p"), QStringLiteral("03:04:05 PM")),
            makeOption(QString(), translate("None")),
        };
    }
    if (key == QLatin1String("DateFormat")) {
        return {
            makeOption(QStringLiteral("%d/%m/%Y"), QStringLiteral("dd/MM/yyyy")), makeOption(QStringLiteral("%d/%m/%y"), QStringLiteral("dd/MM/yy")),
            makeOption(QStringLiteral("%m/%d/%Y"), QStringLiteral("MM/dd/yyyy")), makeOption(QStringLiteral("%Y-%m-%d"), QStringLiteral("yyyy-MM-dd")),
            makeOption(QStringLiteral("%y-%m-%d"), QStringLiteral("yy-MM-dd")),   makeOption(QString(), translate("None")),
        };
    }

    const auto metaIt = metadata_.constFind(key);
    if (metaIt != metadata_.cend()) {
        const QVariantMap& extra    = metaIt->extra;
        const QVariant     enumVar  = extra.value(QStringLiteral("Enum"));
        const QVariantMap  enumI18n = extra.value(QStringLiteral("EnumI18n")).toMap();

        if (enumVar.metaType() == QMetaType::fromType<QVariantMap>()) {
            const QVariantMap enumMap = enumVar.toMap();
            if (!enumMap.isEmpty()) {
                QStringList idxKeys = enumMap.keys();
                std::ranges::sort(idxKeys, [](const QString& a, const QString& b) {
                    bool      okA = false;
                    bool      okB = false;
                    const int nA  = a.toInt(&okA);
                    const int nB  = b.toInt(&okB);
                    if (okA != okB) {
                        return okA;
                    }
                    return okA ? (nA < nB) : (a < b);
                });
                QVariantList out;
                for (const QString& idxKey : std::as_const(idxKeys)) {
                    const QString raw = enumMap.value(idxKey).toString();
                    QString       label;
                    if (enumI18n.contains(idxKey)) {
                        label = enumI18n.value(idxKey).toString();
                    } else {
                        label = translate(raw);
                    }
                    out.append(makeOption(raw, label));
                }
                return out;
            }
        } else if (enumVar.metaType() == QMetaType::fromType<QVariantList>()) {
            const QVariantList enumList = enumVar.toList();
            if (!enumList.isEmpty()) {
                QVariantList out;
                for (const QVariant& item : enumList) {
                    const QString raw = item.toString();
                    out.append(makeOption(raw, translate(raw)));
                }
                return out;
            }
        }
    }

    return {};
}

bool SettingsBackend::applyAll() {
    recomputeDerivedState();
    if (hasValidationErrors_) {
        emit toastRequested(validationMessage_);
        return false;
    }
    if (fcitxOffline_ || !dbusClient_.isConnected()) {
        emit toastRequested(translate("Failed to save settings. Please check if Fcitx5 is running."));
        return false;
    }

    const bool  dictChanged = isDictionaryModifiedFromInitial();

    QStringList changedConfigKeys;
    for (auto it = values_.cbegin(); it != values_.cend(); ++it) {
        if (initialValues_.value(it.key()) != it.value()) {
            changedConfigKeys.append(it.key());
        }
    }

    QStringList savedSections;

    if (appRules_ != initialAppRules_) {
        QVariantList payload;
        payload.reserve(appRules_.size());
        for (const QVariant& itemVar : std::as_const(appRules_)) {
            const QVariantMap rule = itemVar.toMap();
            payload.append(QVariantMap{
                {QStringLiteral("App"), rule.value(QStringLiteral("app")).toString()},
                {QStringLiteral("Mode"), QString::number(rule.value(QStringLiteral("mode")).toInt())},
                {QStringLiteral("CommitInterval"), QString::number(rule.value(QStringLiteral("commit")).toInt())},
                {QStringLiteral("BackspaceInterval"), QString::number(rule.value(QStringLiteral("backspace")).toInt())},
                {QStringLiteral("PostDelay"), QString::number(rule.value(QStringLiteral("post")).toInt())},
            });
        }
        if (!dbusClient_.setSubConfigList(QStringLiteral("app_rules"), QStringLiteral("Rules"), payload)) {
            fcitxOffline_ = !dbusClient_.isConnected();
            emit stateChanged();
            emit toastRequested(
                translate("Failed to save Application Rules. Saved: %1").arg(savedSections.isEmpty() ? translate("None") : savedSections.join(QStringLiteral(", "))));
            return false;
        }
        savedSections.append(translate("Application Rules"));
        initialAppRules_ = appRules_;
    }

    if (macros_ != initialMacros_) {
        QVariantList payload;
        payload.reserve(macros_.size());
        for (const QVariant& itemVar : std::as_const(macros_)) {
            const QVariantMap m = itemVar.toMap();
            payload.append(QVariantMap{
                {QStringLiteral("Key"), m.value(QStringLiteral("key")).toString()},
                {QStringLiteral("Value"), m.value(QStringLiteral("value")).toString()},
            });
        }
        if (!dbusClient_.setSubConfigList(QStringLiteral("lotus-macro"), QStringLiteral("Macro"), payload)) {
            fcitxOffline_ = !dbusClient_.isConnected();
            emit stateChanged();
            emit toastRequested(translate("Failed to save Macros. Saved: %1").arg(savedSections.isEmpty() ? translate("None") : savedSections.join(QStringLiteral(", "))));
            return false;
        }
        savedSections.append(translate("Macros"));
        initialMacros_ = macros_;
    }

    if (keymapBindings_ != initialKeymapBindings_) {
        QVariantList payload;
        payload.reserve(keymapBindings_.size());
        for (const QVariant& itemVar : std::as_const(keymapBindings_)) {
            const QVariantMap km = itemVar.toMap();
            payload.append(QVariantMap{
                {QStringLiteral("Key"), km.value(QStringLiteral("key")).toString()},
                {QStringLiteral("Value"), km.value(QStringLiteral("value")).toString()},
            });
        }
        if (!dbusClient_.setSubConfigList(QStringLiteral("custom_keymap"), QStringLiteral("CustomKeymap"), payload)) {
            fcitxOffline_ = !dbusClient_.isConnected();
            emit stateChanged();
            emit toastRequested(translate("Failed to save Custom Keymap. Saved: %1").arg(savedSections.isEmpty() ? translate("None") : savedSections.join(QStringLiteral(", "))));
            return false;
        }
        savedSections.append(translate("Custom Keymap"));
        initialKeymapBindings_ = keymapBindings_;
    }

    if (!changedConfigKeys.isEmpty() || dictChanged) {
        const LotusDBusClient::ConfigData currentCfg = dbusClient_.getConfig();
        if (!currentCfg.ok) {
            fcitxOffline_ = true;
            emit stateChanged();
            emit toastRequested(
                translate("Failed to save General & Typing. Saved: %1").arg(savedSections.isEmpty() ? translate("None") : savedSections.join(QStringLiteral(", "))));
            return false;
        }
        const QVariantMap wirePayload = buildWireConfigPayload(currentCfg.values, changedConfigKeys);
        if (!dbusClient_.setConfig(wirePayload)) {
            fcitxOffline_ = !dbusClient_.isConnected();
            emit stateChanged();
            emit toastRequested(
                translate("Failed to save General & Typing. Saved: %1").arg(savedSections.isEmpty() ? translate("None") : savedSections.join(QStringLiteral(", "))));
            return false;
        }
        initialValues_ = values_;
    }

    if (dictChanged) {
        const QString lPath = localDictPath();
        if (sortedWordCopy(dictionaryWords_) == sortedWordCopy(globalDictionaryWords_)) {
            if (QFileInfo::exists(lPath) && !QFile::remove(lPath)) {
                emit toastRequested(
                    translate("Failed to save Custom Dictionary. Saved: %1").arg(savedSections.isEmpty() ? translate("None") : savedSections.join(QStringLiteral(", "))));
                return false;
            }
        } else if (!writeDictFileAtomic(lPath, dictionaryWords_)) {
            emit toastRequested(
                translate("Failed to save Custom Dictionary. Saved: %1").arg(savedSections.isEmpty() ? translate("None") : savedSections.join(QStringLiteral(", "))));
            return false;
        }
    }

    initialDictionaryWords_ = dictionaryWords_;
    recomputeDerivedState();
    emit stateChanged();
    emit toastRequested(translate("Settings saved"));
    return true;
}

void SettingsBackend::rebuildCachedDictionaryWords() {
    cachedDictionaryWords_.clear();
    cachedDictionaryWords_.reserve(dictionaryWords_.size());
    for (const QString& w : std::as_const(dictionaryWords_)) {
        cachedDictionaryWords_.append(w);
    }
}

void SettingsBackend::cancelAll() {
    values_          = initialValues_;
    appRules_        = initialAppRules_;
    macros_          = initialMacros_;
    dictionaryWords_ = initialDictionaryWords_;
    keymapBindings_  = initialKeymapBindings_;

    rebuildCachedDictionaryWords();

    rebuildModeList();
    recomputeDerivedState();
    emit stateChanged();
}

QStringList SettingsBackend::sectionConfigKeys(const QString& sectionId) const {
    if (sectionId == QLatin1String("input")) {
        return {QStringLiteral("InputMethod"), QStringLiteral("Mode"), QStringLiteral("OutputCharset")};
    }
    if (sectionId == QLatin1String("quick_keys")) {
        return {QStringLiteral("W2U"), QStringLiteral("BracketTransform")};
    }
    if (sectionId == QLatin1String("tray_icon")) {
        return {QStringLiteral("UseLotusIcons"), QStringLiteral("IconTheme")};
    }
    if (sectionId == QLatin1String("backend")) {
        return {QStringLiteral("PreferVirtualKeyboard")};
    }
    if (sectionId == QLatin1String("spelling")) {
        return {QStringLiteral("SpellCheck"), QStringLiteral("AutoNonVnRestore"), QStringLiteral("DdFreeStyle")};
    }
    if (sectionId == QLatin1String("tone_punctuation")) {
        return {
            QStringLiteral("ModernStyle"),         QStringLiteral("FreeMarking"),          QStringLiteral("AutoCapitalizeAfterPunctuation"),
            QStringLiteral("DoubleSpaceToPeriod"), QStringLiteral("DoubleHyphenToEmDash"),
        };
    }
    if (sectionId == QLatin1String("main_shortcuts")) {
        return {QStringLiteral("ModeMenuKey"), QStringLiteral("CycleModeKey")};
    }
    if (sectionId == QLatin1String("mode_menu")) {
        QStringList keys = {QStringLiteral("ModeOrder")};
        for (const QString& name : modeNames()) {
            keys.append(QStringLiteral("ShowMode") + name);
            keys.append(QStringLiteral("Shortcut") + name);
        }
        return keys;
    }
    if (sectionId == QLatin1String("default_timing")) {
        return {QStringLiteral("DefaultCommitInterval"), QStringLiteral("DefaultBackspaceInterval"), QStringLiteral("DefaultPostDelay")};
    }
    if (sectionId == QLatin1String("macro_behavior")) {
        return {
            QStringLiteral("EnableMacro"), QStringLiteral("CapitalizeMacro"), QStringLiteral("EnableMacroInOffMode"), QStringLiteral("MacroSkipTriggerModifier"),
            QStringLiteral("TimeFormat"),  QStringLiteral("DateFormat"),
        };
    }
    if (sectionId == QLatin1String("dict_status")) {
        return {QStringLiteral("EnableDictionary")};
    }
    if (sectionId == QLatin1String("keymap_config")) {
        return {QStringLiteral("EnableCustomKeymap")};
    }
    return {};
}

QString SettingsBackend::sectionDisplayTitle(const QString& sectionId) const {
    if (sectionId == QLatin1String("input"))
        return translate("Input");
    if (sectionId == QLatin1String("quick_keys"))
        return translate("Quick Keys");
    if (sectionId == QLatin1String("tray_icon"))
        return translate("Tray Icon");
    if (sectionId == QLatin1String("backend"))
        return translate("Backend");
    if (sectionId == QLatin1String("spelling"))
        return translate("Spelling");
    if (sectionId == QLatin1String("tone_punctuation"))
        return translate("Tone & Punctuation");
    if (sectionId == QLatin1String("main_shortcuts"))
        return translate("Main Shortcuts");
    if (sectionId == QLatin1String("mode_menu"))
        return translate("Mode Menu Switching");
    if (sectionId == QLatin1String("default_timing"))
        return translate("Default Timing");
    if (sectionId == QLatin1String("app_rules"))
        return translate("Application Rules");
    if (sectionId == QLatin1String("macro_behavior"))
        return translate("Behavior");
    if (sectionId == QLatin1String("macro_list"))
        return translate("Macros");
    if (sectionId == QLatin1String("dict_status"))
        return translate("Custom Dictionary");
    if (sectionId == QLatin1String("dict_words"))
        return translate("Words");
    if (sectionId == QLatin1String("keymap_config"))
        return translate("Configuration");
    if (sectionId == QLatin1String("keymap_bindings"))
        return translate("Bindings");
    return sectionId;
}

bool SettingsBackend::isConfigKeyModifiedFromDefault(const QString& key) const {
    return values_.value(key, defaultTypedValue(key)) != defaultTypedValue(key);
}

bool SettingsBackend::isModeOrderModifiedFromDefault() const {
    const QString     defOrderStr = defaultTypedValue(QStringLiteral("ModeOrder")).toString();
    const QStringList defRaw      = defOrderStr.split(QLatin1Char(','), Qt::SkipEmptyParts);
    QStringList       defOrder;
    for (const QString& s : defRaw) {
        const QString t = s.trimmed();
        if (!t.isEmpty() && !defOrder.contains(t)) {
            defOrder.append(t);
        }
    }
    for (const QString& name : modeNames()) {
        if (!defOrder.contains(name)) {
            defOrder.append(name);
        }
    }

    QStringList curOrder;
    curOrder.reserve(modeList_.size());
    for (const QVariant& itemVar : modeList_) {
        curOrder.append(itemVar.toMap().value(QStringLiteral("name")).toString());
    }
    return curOrder != defOrder;
}

bool SettingsBackend::isModeMenuModifiedFromDefault() const {
    if (isModeOrderModifiedFromDefault()) {
        return true;
    }

    return std::ranges::any_of(modeNames(), [this](const QString& name) {
        return isConfigKeyModifiedFromDefault(QStringLiteral("ShowMode") + name) || isConfigKeyModifiedFromDefault(QStringLiteral("Shortcut") + name);
    });
}

bool SettingsBackend::isDictionaryModifiedFromDefault() const {
    return dictDirtyDefault_;
}

bool SettingsBackend::isDictionaryModifiedFromInitial() const {
    return dictDirtyInitial_;
}

bool SettingsBackend::isSectionModifiedFromDefault(const QString& sectionId) const {
    if (sectionId == QLatin1String("mode_menu")) {
        return isModeMenuModifiedFromDefault();
    }
    if (sectionId == QLatin1String("app_rules")) {
        return !appRules_.isEmpty();
    }
    if (sectionId == QLatin1String("macro_list")) {
        return !macros_.isEmpty();
    }
    if (sectionId == QLatin1String("dict_words")) {
        return isDictionaryModifiedFromDefault();
    }
    if (sectionId == QLatin1String("keymap_bindings")) {
        return !keymapBindings_.isEmpty();
    }
    const QStringList keys = sectionConfigKeys(sectionId);
    return std::ranges::any_of(keys, [this](const QString& k) { return isConfigKeyModifiedFromDefault(k); });
}

QString SettingsBackend::formatShortcutDisplay(const QString& shortcut) const {
    if (shortcut.isEmpty()) {
        return translate("Disabled");
    }
    const QStringList parts = prettyHotkeyParts(shortcut);
    return parts.isEmpty() ? shortcut : parts.join(QLatin1Char('+'));
}

QString SettingsBackend::formatDisplayValue(const QString& key, const QVariant& val) const {
    const auto metaIt = metadata_.constFind(key);
    if (metaIt != metadata_.cend()) {
        if (metaIt->type == QLatin1String("Boolean")) {
            return asBool(val) ? translate("On") : translate("Off");
        }
        if (metaIt->type == QLatin1String("Integer")) {
            return QStringLiteral("%1 ms").arg(asInt(val));
        }
        if (metaIt->type == QLatin1String("Key") || metaIt->type.startsWith(QLatin1String("List|Key"))) {
            return formatShortcutDisplay(val.toString());
        }
    }
    const QVariantList options = getEnumOptions(key);
    const QString      raw     = val.toString();
    for (const QVariant& optVar : options) {
        const QVariantMap opt = optVar.toMap();
        if (opt.value(QStringLiteral("value")).toString() == raw) {
            return opt.value(QStringLiteral("label")).toString();
        }
    }
    return raw.isEmpty() ? translate("None") : raw;
}

QVariantList SettingsBackend::getSectionResetChanges(const QString& sectionId) const {
    QVariantList changes;
    auto         appendChange = [&changes](const QString& label, const QString& current, const QString& defaultVal) {
        changes.append(QVariantMap{
            {QStringLiteral("label"), label},
            {QStringLiteral("current"), current},
            {QStringLiteral("defaultVal"), defaultVal},
        });
    };

    if (sectionId == QLatin1String("mode_menu")) {
        if (isModeOrderModifiedFromDefault()) {
            appendChange(translate("Order"), translate("Custom"), translate("Default"));
        }
        for (const QString& name : modeNames()) {
            const QString title   = modeTitle(name);
            const QString showKey = QStringLiteral("ShowMode") + name;
            const QString scKey   = QStringLiteral("Shortcut") + name;
            const bool    curVis  = asBool(values_.value(showKey, true), true);
            const bool    defVis  = asBool(defaultTypedValue(showKey), true);
            if (curVis != defVis) {
                appendChange(title, curVis ? translate("Visible") : translate("Hidden"), defVis ? translate("Visible") : translate("Hidden"));
            }
            const QString curSc = values_.value(scKey, defaultTypedValue(scKey)).toString();
            const QString defSc = defaultTypedValue(scKey).toString();
            if (curSc != defSc) {
                appendChange(title, formatShortcutDisplay(curSc), formatShortcutDisplay(defSc));
            }
        }
        return changes;
    }

    if (sectionId == QLatin1String("app_rules")) {
        if (!appRules_.isEmpty()) {
            QStringList names;
            names.reserve(appRules_.size());
            for (const QVariant& itemVar : appRules_) {
                names.append(itemVar.toMap().value(QStringLiteral("app")).toString());
            }
            QString preview = names.mid(0, kResetPreviewCount).join(QStringLiteral(", "));
            if (names.size() > kResetPreviewCount) {
                preview += QStringLiteral(", …");
            }
            QString currentDesc = translate("%1 rules (%2)");
            if (currentDesc.contains(QStringLiteral("{count}"))) {
                currentDesc.replace(QStringLiteral("{count}"), QString::number(names.size()));
                currentDesc.replace(QStringLiteral("{apps}"), preview);
            } else {
                currentDesc = currentDesc.arg(names.size()).arg(preview);
            }
            appendChange(translate("Application Rules"), currentDesc, translate("None (0 rules)"));
        }
        return changes;
    }

    if (sectionId == QLatin1String("macro_list")) {
        if (!macros_.isEmpty()) {
            QString currentDesc = translate("%1 macros");
            if (currentDesc.contains(QStringLiteral("{count}"))) {
                currentDesc.replace(QStringLiteral("{count}"), QString::number(macros_.size()));
            } else {
                currentDesc = currentDesc.arg(macros_.size());
            }
            appendChange(translate("Macro Entries"), currentDesc, translate("Empty (0 macros)"));
        }
        return changes;
    }

    if (sectionId == QLatin1String("dict_words")) {
        if (isDictionaryModifiedFromDefault()) {
            QString currentDesc;
            QString defaultDesc;
            if (!globalDictionaryWords_.isEmpty()) {
                const QSet<QString> curSet(dictionaryWords_.cbegin(), dictionaryWords_.cend());
                const QSet<QString> defSet(globalDictionaryWords_.cbegin(), globalDictionaryWords_.cend());
                const int           added   = static_cast<int>((curSet - defSet).size());
                const int           removed = static_cast<int>((defSet - curSet).size());
                currentDesc                 = translate("%1 words (+%2 / -%3)").arg(dictionaryWords_.size()).arg(added).arg(removed);
                defaultDesc                 = translate("Built-in dictionary (%1 words)").arg(globalDictionaryWords_.size());
            } else {
                currentDesc = translate("%1 words").arg(dictionaryWords_.size());
                defaultDesc = translate("Empty (0 words)");
            }
            appendChange(translate("Custom Words"), currentDesc, defaultDesc);
        }
        return changes;
    }

    if (sectionId == QLatin1String("keymap_bindings")) {
        if (!keymapBindings_.isEmpty()) {
            appendChange(translate("Key Bindings"), translate("%1 bindings").arg(keymapBindings_.size()), translate("Empty (0 bindings)"));
        }
        return changes;
    }

    static const QHash<QString, const char*> kKeyLabels = {
        {QStringLiteral("InputMethod"), "Input Method"},
        {QStringLiteral("Mode"), "Input Mode"},
        {QStringLiteral("OutputCharset"), "Output Charset"},
        {QStringLiteral("W2U"), "Type w as ư"},
        {QStringLiteral("BracketTransform"), "Bracket Keys"},
        {QStringLiteral("UseLotusIcons"), "Use Lotus Icons"},
        {QStringLiteral("IconTheme"), "Icon Theme"},
        {QStringLiteral("PreferVirtualKeyboard"), "System Keyboard Injection"},
        {QStringLiteral("SpellCheck"), "Spell Check"},
        {QStringLiteral("AutoNonVnRestore"), "Auto Restore"},
        {QStringLiteral("DdFreeStyle"), "Free dd to đ"},
        {QStringLiteral("ModernStyle"), "Modern Tone Style"},
        {QStringLiteral("FreeMarking"), "Free Tone Marking"},
        {QStringLiteral("AutoCapitalizeAfterPunctuation"), "Auto Capitalize"},
        {QStringLiteral("DoubleSpaceToPeriod"), "Double Space Period"},
        {QStringLiteral("DoubleHyphenToEmDash"), "Double Hyphen Dash"},
        {QStringLiteral("ModeMenuKey"), "Mode Menu Hotkey"},
        {QStringLiteral("CycleModeKey"), "Cycle Mode Hotkey"},
        {QStringLiteral("DefaultCommitInterval"), "Commit Interval (ms)"},
        {QStringLiteral("DefaultBackspaceInterval"), "Backspace Interval (ms)"},
        {QStringLiteral("DefaultPostDelay"), "Post Delay (ms)"},
        {QStringLiteral("EnableMacro"), "Enable Macros"},
        {QStringLiteral("CapitalizeMacro"), "Match Case"},
        {QStringLiteral("EnableMacroInOffMode"), "Off-Mode Macros"},
        {QStringLiteral("MacroSkipTriggerModifier"), "Skip Modifier"},
        {QStringLiteral("TimeFormat"), "Time Format"},
        {QStringLiteral("DateFormat"), "Date Format"},
        {QStringLiteral("EnableDictionary"), "Enable Dictionary"},
        {QStringLiteral("EnableCustomKeymap"), "Custom Keymap"},
    };

    const QStringList keys = sectionConfigKeys(sectionId);
    for (const QString& key : keys) {
        if (!isConfigKeyModifiedFromDefault(key)) {
            continue;
        }
        const QString  label = kKeyLabels.contains(key) ? translate(kKeyLabels.value(key)) : key;
        const QVariant cur   = values_.value(key, defaultTypedValue(key));
        const QVariant def   = defaultTypedValue(key);
        appendChange(label, formatDisplayValue(key, cur), formatDisplayValue(key, def));
    }
    return changes;
}

bool SettingsBackend::resetSection(const QString& sectionId) {
    if (sectionId == QLatin1String("app_rules")) {
        appRules_.clear();
    } else if (sectionId == QLatin1String("macro_list")) {
        macros_.clear();
    } else if (sectionId == QLatin1String("dict_words")) {
        if (fcitxOffline_) {
            emit toastRequested(translate("Failed to reset custom dictionary. Please check if Fcitx5 is running."));
            return false;
        }
        dictLoadFailed_  = false;
        dictionaryWords_ = globalDictionaryWords_;
        rebuildCachedDictionaryWords();
    } else if (sectionId == QLatin1String("keymap_bindings")) {
        keymapBindings_.clear();
    } else {
        const QStringList keys = sectionConfigKeys(sectionId);
        if (keys.isEmpty()) {
            return false;
        }
        for (const QString& key : keys) {
            values_.insert(key, defaultTypedValue(key));
        }
        if (sectionId == QLatin1String("mode_menu")) {
            rebuildModeList();
        }
    }

    recomputeDerivedState();
    emit stateChanged();
    emit toastRequested(translate("Restored defaults for %1").arg(sectionDisplayTitle(sectionId)));
    return true;
}

void SettingsBackend::reorderModes(int fromIdx, int toIdx) {
    if (fromIdx < 0 || fromIdx >= modeList_.size() || toIdx < 0 || toIdx >= modeList_.size() || fromIdx == toIdx) {
        return;
    }
    QStringList order;
    order.reserve(modeList_.size());
    for (const QVariant& itemVar : std::as_const(modeList_)) {
        order.append(itemVar.toMap().value(QStringLiteral("name")).toString());
    }
    order.move(fromIdx, toIdx);
    values_.insert(QStringLiteral("ModeOrder"), order.join(QLatin1Char(',')));
    rebuildModeList();
    recomputeDerivedState();
    emit stateChanged();
}

void SettingsBackend::setModeVisible(const QString& name, bool vis) {
    if (!modeNames().contains(name)) {
        return;
    }
    setValue(QStringLiteral("ShowMode") + name, vis);
}

void SettingsBackend::setModeShortcut(const QString& name, const QString& key) {
    if (!modeNames().contains(name)) {
        return;
    }
    setValue(QStringLiteral("Shortcut") + name, key);
}

void SettingsBackend::scanDesktopEntries() {
    desktopIconCache_.clear();
    desktopApps_.clear();

    QStringList searchPaths = {
        QDir::home().filePath(QStringLiteral(".local/share/applications")),
        QDir::home().filePath(QStringLiteral(".local/share/flatpak/exports/share/applications")),
        QStringLiteral("/usr/share/applications"),
        QStringLiteral("/var/lib/flatpak/exports/share/applications"),
        QStringLiteral("/var/lib/snapd/desktop/applications"),
    };
    const QStringList xdgDirs = qEnvironmentVariable("XDG_DATA_DIRS").split(QLatin1Char(':'), Qt::SkipEmptyParts);
    for (const QString& dir : xdgDirs) {
        const QString appPath = QDir(dir).filePath(QStringLiteral("applications"));
        if (!searchPaths.contains(appPath)) {
            searchPaths.append(appPath);
        }
    }

    struct ParsedEntry {
        QStringList keys;
        QString     name;
        QString     icon;
    };
    QList<ParsedEntry> visibleEntries;
    QList<ParsedEntry> hiddenEntries;

    for (const QString& folder : std::as_const(searchPaths)) {
        const QDir dir(folder);
        if (!dir.exists()) {
            continue;
        }
        const QStringList files = dir.entryList({QStringLiteral("*.desktop")}, QDir::Files, QDir::Name);
        for (const QString& filename : files) {
            QFile file(dir.filePath(filename));
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                continue;
            }
            const QString desktopId = filename.chopped(8);
            QString       name      = desktopId;
            QString       icon;
            QString       wmClass;
            QString       execLine;
            bool          isHidden = false;
            bool          inEntry  = false;

            QTextStream   in(&file);
            while (!in.atEnd()) {
                const QString line = in.readLine().trimmed();
                if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
                    continue;
                }
                if (line.startsWith(QLatin1Char('['))) {
                    if (line == QLatin1String("[Desktop Entry]")) {
                        inEntry = true;
                        continue;
                    }
                    if (inEntry) {
                        break;
                    }
                    continue;
                }
                if (!inEntry) {
                    continue;
                }
                if (line.startsWith(QLatin1String("Icon="))) {
                    icon = line.mid(5).trimmed();
                } else if (line.startsWith(QLatin1String("Name="))) {
                    name = line.mid(5).trimmed();
                } else if (line.startsWith(QLatin1String("StartupWMClass="))) {
                    wmClass = line.mid(15).trimmed().toLower();
                } else if (line.startsWith(QLatin1String("Exec="))) {
                    execLine = line.mid(5).trimmed();
                } else if (line.compare(QLatin1String("NoDisplay=true"), Qt::CaseInsensitive) == 0 || line.compare(QLatin1String("Hidden=true"), Qt::CaseInsensitive) == 0) {
                    isHidden = true;
                }
            }

            QStringList keys = {desktopId.toLower()};
            if (desktopId.contains(QLatin1Char('.'))) {
                keys.append(desktopId.section(QLatin1Char('.'), -1).toLower());
            }
            if (!name.isEmpty()) {
                keys.append(name.toLower());
            }
            if (!wmClass.isEmpty()) {
                keys.append(wmClass);
            }
            if (!execLine.isEmpty()) {
                QString binPath;
                if (execLine.startsWith(QLatin1Char('"'))) {
                    binPath = execLine.mid(1).section(QLatin1Char('"'), 0, 0);
                } else {
                    binPath = execLine.section(QLatin1Char(' '), 0, 0);
                }
                const QString binName = QFileInfo(binPath).fileName().toLower();
                if (!binName.isEmpty()) {
                    keys.append(binName);
                }
                if (binName == QLatin1String("flatpak") && execLine.contains(QLatin1String(" --command="))) {
                    const QString cmdPart = execLine.section(QLatin1String("--command="), 1).section(QLatin1Char(' '), 0, 0).trimmed().toLower();
                    if (!cmdPart.isEmpty()) {
                        keys.append(cmdPart);
                    }
                }
            }

            ParsedEntry entry{.keys = keys, .name = name, .icon = icon};
            if (isHidden) {
                hiddenEntries.append(entry);
            } else {
                visibleEntries.append(entry);
            }
        }
    }

    auto indexEntries = [this](const QList<ParsedEntry>& entries) {
        for (const ParsedEntry& entry : entries) {
            for (const QString& k : entry.keys) {
                if (!entry.icon.isEmpty() && !desktopIconCache_.contains(k)) {
                    desktopIconCache_.insert(k, entry.icon);
                }
                if (!desktopApps_.contains(k)) {
                    desktopApps_.insert(k, DesktopAppEntry{.name = entry.name, .icon = entry.icon});
                }
            }
        }
    };
    indexEntries(visibleEntries);
    indexEntries(hiddenEntries);

    static const QHash<QString, QString> kFallbackIcons = {
        {QStringLiteral("discord"), QStringLiteral("discord")},
        {QStringLiteral("upnote"), QStringLiteral("upnote")},
        {QStringLiteral("antigravity"), QStringLiteral("preferences-desktop-accessibility")},
    };
    for (auto it = kFallbackIcons.cbegin(); it != kFallbackIcons.cend(); ++it) {
        if (!desktopIconCache_.contains(it.key())) {
            desktopIconCache_.insert(it.key(), it.value());
        }
    }
}

QString SettingsBackend::resolveAppDisplayName(const QString& appId) const {
    const QString lower = appId.toLower();
    if (desktopApps_.contains(lower)) {
        return desktopApps_.value(lower).name;
    }
    const QString base = QFileInfo(appId).fileName().toLower();
    if (desktopApps_.contains(base)) {
        return desktopApps_.value(base).name;
    }
    if (base.contains(QLatin1Char('.'))) {
        const QString shortName = base.section(QLatin1Char('.'), -1);
        if (desktopApps_.contains(shortName)) {
            return desktopApps_.value(shortName).name;
        }
    }
    return appId;
}

QString SettingsBackend::resolveAppIconName(const QString& appId) const {
    const QString lower    = appId.toLower();
    QString       iconName = desktopIconCache_.value(lower);
    if (iconName.isEmpty()) {
        const QString base = QFileInfo(appId).fileName().toLower();
        iconName           = desktopIconCache_.value(base);
        if (iconName.isEmpty() && base.contains(QLatin1Char('.'))) {
            iconName = desktopIconCache_.value(base.section(QLatin1Char('.'), -1));
        }
    }
    if (iconName.isEmpty()) {
        iconName = lower;
    }
    if (iconName == QLatin1String("application-x-executable")) {
        return {};
    }
    if (QFileInfo::exists(iconName) || QIcon::hasThemeIcon(iconName)) {
        return iconName;
    }
    return {};
}

void SettingsBackend::addAppRule(const QString& appId) {
    const QString cleaned = appId.trimmed();
    if (cleaned.isEmpty()) {
        return;
    }
    for (const QVariant& itemVar : std::as_const(appRules_)) {
        if (itemVar.toMap().value(QStringLiteral("app")).toString() == cleaned) {
            return;
        }
    }
    appRules_.append(QVariantMap{
        {QStringLiteral("app"), cleaned},
        {QStringLiteral("displayName"), resolveAppDisplayName(cleaned)},
        {QStringLiteral("iconName"), resolveAppIconName(cleaned)},
        {QStringLiteral("mode"), fcitx::modeToInt(fcitx::LotusMode::Smooth)},
        {QStringLiteral("commit"), 0},
        {QStringLiteral("backspace"), 0},
        {QStringLiteral("post"), 0},
    });
    sortAppRules(appRules_);
    recomputeDerivedState();
    emit stateChanged();
}

void SettingsBackend::updateAppRule(const QString& appId, int mode, int commit, int backspace, int post) {
    const int normMode      = normalizeAppMode(mode);
    const int normCommit    = std::clamp(commit, 0, kMaxDelayMs);
    const int normBackspace = std::clamp(backspace, 0, kMaxDelayMs);
    const int normPost      = std::clamp(post, 0, kMaxDelayMs);

    for (auto& appRule : appRules_) {
        QVariantMap rule = appRule.toMap();
        if (rule.value(QStringLiteral("app")).toString() == appId) {
            rule.insert(QStringLiteral("mode"), normMode);
            rule.insert(QStringLiteral("commit"), normCommit);
            rule.insert(QStringLiteral("backspace"), normBackspace);
            rule.insert(QStringLiteral("post"), normPost);
            appRule = rule;
            recomputeDerivedState();
            emit stateChanged();
            return;
        }
    }
}

void SettingsBackend::removeAppRule(const QString& appId) {
    for (int i = 0; i < appRules_.size(); ++i) {
        if (appRules_.at(i).toMap().value(QStringLiteral("app")).toString() == appId) {
            appRules_.removeAt(i);
            recomputeDerivedState();
            emit stateChanged();
            return;
        }
    }
}

bool SettingsBackend::isValidMacroKey(const QString& key) {
    if (key.isEmpty()) {
        return false;
    }
    const QList<uint> ucs4 = key.toUcs4();
    return std::ranges::all_of(ucs4, [](uint cp) { return QChar::isLetterOrNumber(cp); });
}

void SettingsBackend::upsertMacro(const QString& oldKey, const QString& newKey, const QString& val) {
    const QString trimmedKey = newKey.trimmed();
    const QString trimmedVal = val.trimmed();
    if (trimmedKey.isEmpty() || trimmedVal.isEmpty()) {
        return;
    }

    for (qsizetype i = macros_.size() - 1; i >= 0; --i) {
        const QString existingKey = macros_.at(i).toMap().value(QStringLiteral("key")).toString();
        if ((!oldKey.isEmpty() && existingKey == oldKey) || existingKey == trimmedKey) {
            macros_.removeAt(i);
        }
    }

    macros_.append(QVariantMap{
        {QStringLiteral("key"), trimmedKey},
        {QStringLiteral("value"), trimmedVal},
        {QStringLiteral("invalid"), !isValidMacroKey(trimmedKey)},
    });

    sortMacros(macros_);

    recomputeDerivedState();
    emit stateChanged();
}

void SettingsBackend::removeMacro(const QString& key) {
    for (int i = 0; i < macros_.size(); ++i) {
        if (macros_.at(i).toMap().value(QStringLiteral("key")).toString() == key) {
            macros_.removeAt(i);
            recomputeDerivedState();
            emit stateChanged();
            return;
        }
    }
}

bool SettingsBackend::isValidWord(const QString& word) {
    const QString trimmed = word.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }
    return std::ranges::none_of(trimmed, [](QChar ch) { return ch.isSpace(); });
}

void SettingsBackend::addWord(const QString& word) {
    const QString trimmed = word.trimmed();
    if (!isValidWord(trimmed) || dictionaryWords_.contains(trimmed)) {
        return;
    }
    dictionaryWords_.prepend(trimmed);
    cachedDictionaryWords_.prepend(trimmed);
    recomputeDerivedState();
    emit stateChanged();
}

void SettingsBackend::removeWord(const QString& word) {
    const qsizetype idx = dictionaryWords_.indexOf(word);
    if (idx < 0) {
        return;
    }
    dictionaryWords_.removeAt(idx);
    cachedDictionaryWords_.removeAt(idx);
    recomputeDerivedState();
    emit stateChanged();
}

QVariantList SettingsBackend::filterWords(const QString& query, int limit) const {
    const QString q = query.trimmed().toLower();
    QVariantList  out;
    for (const QString& word : std::as_const(dictionaryWords_)) {
        if (q.isEmpty() || word.toLower().contains(q)) {
            out.append(word);
            if (limit > 0 && out.size() >= limit) {
                break;
            }
        }
    }
    return out;
}

int SettingsBackend::countMatchingWords(const QString& query) const {
    const QString q = query.trimmed().toLower();
    if (q.isEmpty()) {
        return static_cast<int>(dictionaryWords_.size());
    }
    int count = 0;
    for (const QString& word : std::as_const(dictionaryWords_)) {
        if (word.toLower().contains(q)) {
            ++count;
        }
    }
    return count;
}

void SettingsBackend::applyKeymapPreset(const QString& presetName) {
    for (const QVariant& presetVar : std::as_const(keymapPresets_)) {
        const QVariantMap preset = presetVar.toMap();
        if (presetName == preset.value(QStringLiteral("name")).toString()) {
            keymapBindings_ = preset.value(QStringLiteral("rules")).toList();
            recomputeDerivedState();
            emit stateChanged();
            return;
        }
    }
}

void SettingsBackend::upsertKeymap(const QString& key, const QString& actionCode) {
    const QString trimmedKey = key.trimmed();
    if (trimmedKey.isEmpty() || actionCode.isEmpty()) {
        return;
    }
    for (auto& keymapBinding : keymapBindings_) {
        QVariantMap km = keymapBinding.toMap();
        if (km.value(QStringLiteral("key")).toString() == trimmedKey) {
            km.insert(QStringLiteral("value"), actionCode);
            keymapBinding = km;
            recomputeDerivedState();
            emit stateChanged();
            return;
        }
    }
    keymapBindings_.append(QVariantMap{
        {QStringLiteral("key"), trimmedKey},
        {QStringLiteral("value"), actionCode},
    });
    recomputeDerivedState();
    emit stateChanged();
}

void SettingsBackend::removeKeymap(const QString& key) {
    for (int i = 0; i < keymapBindings_.size(); ++i) {
        if (keymapBindings_.at(i).toMap().value(QStringLiteral("key")).toString() == key) {
            keymapBindings_.removeAt(i);
            recomputeDerivedState();
            emit stateChanged();
            return;
        }
    }
}

bool SettingsBackend::exportBackup(const QString& fileUrl, const QVariantMap& components) {
    const QString path = resolveLocalFilePath(fileUrl);
    if (path.isEmpty()) {
        return false;
    }

    const bool wantConfig = components.value(QStringLiteral("config"), false).toBool();
    const bool wantRules  = components.value(QStringLiteral("rules"), false).toBool();
    const bool wantMacros = components.value(QStringLiteral("macros"), false).toBool();
    const bool wantKeys   = components.value(QStringLiteral("keymaps"), false).toBool();
    const bool wantDict   = components.value(QStringLiteral("dictionary"), false).toBool();

    if (!wantConfig && !wantRules && !wantMacros && !wantKeys && !wantDict) {
        emit toastRequested(translate("Select at least one component to export"));
        return false;
    }

    const bool wantsDbus = wantConfig || wantRules || wantMacros || wantKeys;
    if (wantsDbus && !dbusClient_.isConnected()) {
        emit toastRequested(translate("Fcitx5 is not running. Cannot export settings."));
        return false;
    }

    const QString lPath = localDictPath();
    if (!wantsDbus && wantDict && !QFileInfo::exists(lPath)) {
        emit toastRequested(translate("No custom dictionary file to export."));
        return false;
    }

    QJsonObject root;
    QJsonArray  included;

    if (wantConfig) {
        const LotusDBusClient::ConfigData cfg = dbusClient_.getConfig();
        if (!cfg.ok) {
            emit toastRequested(translate("Fcitx5 is not running. Cannot export settings."));
            return false;
        }
        root.insert(QStringLiteral("config"), QJsonObject::fromVariantMap(cfg.values));
        included.append(QStringLiteral("config"));
    }
    if (wantRules) {
        const QVariantList list = dbusClient_.getSubConfigList(QStringLiteral("app_rules"), QStringLiteral("Rules"));
        root.insert(QStringLiteral("rules"), QJsonArray::fromVariantList(list));
        included.append(QStringLiteral("rules"));
    }
    if (wantMacros) {
        const QVariantList list = dbusClient_.getSubConfigList(QStringLiteral("lotus-macro"), QStringLiteral("Macro"));
        root.insert(QStringLiteral("macros"), QJsonArray::fromVariantList(list));
        included.append(QStringLiteral("macros"));
    }
    if (wantKeys) {
        const QVariantList list = dbusClient_.getSubConfigList(QStringLiteral("custom_keymap"), QStringLiteral("CustomKeymap"));
        root.insert(QStringLiteral("keymaps"), QJsonArray::fromVariantList(list));
        included.append(QStringLiteral("keymaps"));
    }
    if (wantDict && QFileInfo::exists(lPath)) {
        QStringList words;
        if (!readDictFile(lPath, words)) {
            emit toastRequested(translate("Export failed: cannot read custom dictionary."));
            return false;
        }
        QJsonArray arr;
        for (const QString& w : std::as_const(words)) {
            arr.append(w);
        }
        root.insert(QStringLiteral("dictionary"), arr);
        included.append(QStringLiteral("dictionary"));
    }

    QJsonObject meta;
    meta.insert(QStringLiteral("version"), 2);
    meta.insert(QStringLiteral("timestamp"), QDateTime::currentDateTime().toString(Qt::ISODate));
    meta.insert(QStringLiteral("app"), QStringLiteral("fcitx5-lotus"));
    meta.insert(QStringLiteral("components"), included);
    root.insert(QStringLiteral("meta"), meta);

    const QByteArray jsonBytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (!writeRawTextAtomic(path, jsonBytes)) {
        emit toastRequested(translate("Failed to export backup."));
        return false;
    }

    emit toastRequested(translate("Backup exported"));
    return true;
}

QVariantMap SettingsBackend::inspectBackup(const QString& fileUrl) const {
    QVariantMap result{
        {QStringLiteral("valid"), false},         {QStringLiteral("error"), QString()},           {QStringLiteral("version"), 0},
        {QStringLiteral("timestamp"), QString()}, {QStringLiteral("components"), QVariantList()}, {QStringLiteral("hasConfig"), false},
        {QStringLiteral("hasRules"), false},      {QStringLiteral("hasMacros"), false},           {QStringLiteral("hasKeymaps"), false},
        {QStringLiteral("hasDictionary"), false},
    };

    const QString path = resolveLocalFilePath(fileUrl);
    if (path.isEmpty()) {
        result.insert(QStringLiteral("error"), translate("Invalid backup file"));
        return result;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.insert(QStringLiteral("error"), translate("Failed to read backup: %1").arg(file.errorString()));
        return result;
    }

    QJsonParseError     parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        result.insert(QStringLiteral("error"), translate("Invalid backup file"));
        return result;
    }

    const QJsonObject obj  = doc.object();
    const QJsonObject meta = obj.value(QStringLiteral("meta")).toObject();
    result.insert(QStringLiteral("version"), meta.value(QStringLiteral("version")).toInt(0));
    result.insert(QStringLiteral("timestamp"), meta.value(QStringLiteral("timestamp")).toString());

    const bool   hasConfig = obj.value(QStringLiteral("config")).isObject();
    const bool   hasRules  = obj.value(QStringLiteral("rules")).isArray();
    const bool   hasMacros = obj.value(QStringLiteral("macros")).isArray();
    const bool   hasKeys   = obj.value(QStringLiteral("keymaps")).isArray();
    const bool   hasDict   = obj.value(QStringLiteral("dictionary")).isArray() || obj.value(QStringLiteral("dictionary")).isString();

    QVariantList found;
    if (hasConfig)
        found.append(QStringLiteral("config"));
    if (hasRules)
        found.append(QStringLiteral("rules"));
    if (hasMacros)
        found.append(QStringLiteral("macros"));
    if (hasKeys)
        found.append(QStringLiteral("keymaps"));
    if (hasDict)
        found.append(QStringLiteral("dictionary"));

    result.insert(QStringLiteral("hasConfig"), hasConfig);
    result.insert(QStringLiteral("hasRules"), hasRules);
    result.insert(QStringLiteral("hasMacros"), hasMacros);
    result.insert(QStringLiteral("hasKeymaps"), hasKeys);
    result.insert(QStringLiteral("hasDictionary"), hasDict);
    result.insert(QStringLiteral("components"), found);
    result.insert(QStringLiteral("valid"), !found.isEmpty());
    if (found.isEmpty()) {
        result.insert(QStringLiteral("error"), translate("No matching components found in backup"));
    }
    return result;
}

bool SettingsBackend::importBackup(const QString& fileUrl, const QVariantMap& components) {
    const QString path = resolveLocalFilePath(fileUrl);
    if (path.isEmpty()) {
        emit toastRequested(translate("Invalid backup file"));
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        emit toastRequested(translate("Failed to read backup: %1").arg(file.errorString()));
        return false;
    }

    QJsonParseError     parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        emit toastRequested(translate("Invalid backup file"));
        return false;
    }

    const QJsonObject obj       = doc.object();
    const bool        needsDbus = (components.value(QStringLiteral("config"), false).toBool() && obj.value(QStringLiteral("config")).isObject()) ||
        (components.value(QStringLiteral("rules"), false).toBool() && obj.value(QStringLiteral("rules")).isArray()) ||
        (components.value(QStringLiteral("macros"), false).toBool() && obj.value(QStringLiteral("macros")).isArray()) ||
        (components.value(QStringLiteral("keymaps"), false).toBool() && obj.value(QStringLiteral("keymaps")).isArray());
    if (needsDbus && !dbusClient_.isConnected()) {
        fcitxOffline_ = true;
        emit stateChanged();
        emit toastRequested(translate("Failed to save settings. Please check if Fcitx5 is running."));
        return false;
    }

    QStringList failed;
    bool        dictRestored   = false;
    bool        configReloaded = false;

    if (components.value(QStringLiteral("config"), false).toBool() && obj.value(QStringLiteral("config")).isObject()) {
        const QVariantMap cfgMap = obj.value(QStringLiteral("config")).toObject().toVariantMap();
        if (dbusClient_.setConfig(cfgMap)) {
            configReloaded = true;
        } else {
            failed.append(translate("General & Typing"));
        }
    }

    if (components.value(QStringLiteral("rules"), false).toBool() && obj.value(QStringLiteral("rules")).isArray()) {
        const QVariantList list = obj.value(QStringLiteral("rules")).toArray().toVariantList();
        if (!dbusClient_.setSubConfigList(QStringLiteral("app_rules"), QStringLiteral("Rules"), list)) {
            failed.append(translate("Application Rules"));
        }
    }

    if (components.value(QStringLiteral("macros"), false).toBool() && obj.value(QStringLiteral("macros")).isArray()) {
        const QVariantList list = obj.value(QStringLiteral("macros")).toArray().toVariantList();
        if (!dbusClient_.setSubConfigList(QStringLiteral("lotus-macro"), QStringLiteral("Macro"), list)) {
            failed.append(translate("Macros"));
        }
    }

    if (components.value(QStringLiteral("keymaps"), false).toBool() && obj.value(QStringLiteral("keymaps")).isArray()) {
        const QVariantList list = obj.value(QStringLiteral("keymaps")).toArray().toVariantList();
        if (!dbusClient_.setSubConfigList(QStringLiteral("custom_keymap"), QStringLiteral("CustomKeymap"), list)) {
            failed.append(translate("Custom Keymap"));
        }
    }

    if (components.value(QStringLiteral("dictionary"), false).toBool() && obj.contains(QStringLiteral("dictionary"))) {
        const QJsonValue dictVal = obj.value(QStringLiteral("dictionary"));
        bool             ok      = false;
        if (dictVal.isArray()) {
            QStringList      words;
            const QJsonArray arr = dictVal.toArray();
            words.reserve(arr.size());
            for (const auto& v : arr) {
                const QString w = v.toString().trimmed();
                if (!w.isEmpty()) {
                    words.append(w);
                }
            }
            ok = writeDictFileAtomic(localDictPath(), words);
        } else if (dictVal.isString()) {
            ok = writeRawTextAtomic(localDictPath(), dictVal.toString().toUtf8());
        }
        if (ok) {
            dictRestored = true;
        } else {
            failed.append(translate("Custom Dictionary"));
        }
    }

    if (dictRestored && !configReloaded) {
        const LotusDBusClient::ConfigData cfg = dbusClient_.getConfig();
        if (cfg.ok) {
            dbusClient_.setConfig(cfg.values);
        }
    }

    if (!failed.isEmpty()) {
        emit toastRequested(translate("Failed to restore: %1").arg(failed.join(QStringLiteral(", "))));
        return false;
    }

    loadAll();
    emit stateChanged();
    emit toastRequested(translate("Backup restored"));
    return true;
}

void SettingsBackend::exportDebugInfo(const QString& fileUrl) {
    const QString path = resolveLocalFilePath(fileUrl);
    if (path.isEmpty()) {
        emit toastRequested(translate("Failed to export debug info"));
        emit debugExportFinished(false, translate("Failed to export debug info"));
        return;
    }

    QVariantMap                       configSnapshot;
    bool                              hasConfig = false;
    const LotusDBusClient::ConfigData cfg       = dbusClient_.getConfig();
    if (cfg.ok) {
        configSnapshot = cfg.values;
        hasConfig      = true;
    }
    const bool                        offline = fcitxOffline_;
    const QString                     version = appVersion();
    const std::shared_ptr<AliveGuard> guard   = aliveGuard_;

    QThreadPool::globalInstance()->start(QRunnable::create([this, guard, path, version, configSnapshot, hasConfig, offline] {
        const QString          report = collectDebugReport(version, configSnapshot, hasConfig, offline);
        const bool             ok     = writeRawTextAtomic(path, report.toUtf8());
        const std::scoped_lock lock(guard->mutex);
        if (guard->alive) {
            QMetaObject::invokeMethod(
                this,
                [this, guard, ok] {
                    const std::scoped_lock innerLock(guard->mutex);
                    if (guard->alive) {
                        const QString message = ok ? translate("Debug info exported") : translate("Failed to export debug info");
                        emit          toastRequested(message);
                        emit          debugExportFinished(ok, message);
                    }
                },
                Qt::QueuedConnection);
        }
    }));
}

QString SettingsBackend::formatKeyEvent(int qtKey, int qtModifiers, const QString& text, bool allowSingleKey, bool singleKeyOnly) {
    if (qtKey == Qt::Key_Escape || qtKey == Qt::Key_unknown || qtKey == 0) {
        return {};
    }

    switch (qtKey) {
        case Qt::Key_Control:
        case Qt::Key_Shift:
        case Qt::Key_Alt:
        case Qt::Key_Meta:
        case Qt::Key_AltGr:
        case Qt::Key_Super_L:
        case Qt::Key_Super_R:
        case Qt::Key_Hyper_L:
        case Qt::Key_Hyper_R: return {};
        default: break;
    }

    QString baseKey = qtKeyToFcitxKeysym(qtKey, qtModifiers, text);
    if (baseKey.isEmpty()) {
        return {};
    }

    if (singleKeyOnly) {
        return baseKey;
    }

    const auto         modFlags       = static_cast<unsigned int>(qtModifiers);
    const unsigned int nonShiftMask   = static_cast<unsigned int>(Qt::ControlModifier) | static_cast<unsigned int>(Qt::AltModifier) | static_cast<unsigned int>(Qt::MetaModifier);
    const bool         hasNonShiftMod = (modFlags & nonShiftMask) != 0U;
    if (!allowSingleKey && !hasNonShiftMod && !isSpecialSingleKey(baseKey)) {
        return {};
    }

    const bool isAsciiAlpha = (baseKey.size() == 1 && baseKey.at(0).unicode() < 128 && baseKey.at(0).isLetter());
    if (hasNonShiftMod && isAsciiAlpha) {
        baseKey = baseKey.toUpper();
    }

    QStringList mods;
    if ((modFlags & static_cast<unsigned int>(Qt::ControlModifier)) != 0U) {
        mods.append(QStringLiteral("Control"));
    }
    if ((modFlags & static_cast<unsigned int>(Qt::AltModifier)) != 0U) {
        mods.append(QStringLiteral("Alt"));
    }
    if ((modFlags & static_cast<unsigned int>(Qt::MetaModifier)) != 0U) {
        mods.append(QStringLiteral("Super"));
    }
    if (((modFlags & static_cast<unsigned int>(Qt::ShiftModifier)) != 0U) && ((hasNonShiftMod && isAsciiAlpha) || !hotkeyUiUnshiftMap().contains(baseKey))) {
        mods.append(QStringLiteral("Shift"));
    }

    mods.append(baseKey);
    return mods.join(QLatin1Char('+'));
}

QStringList SettingsBackend::prettyHotkeyParts(const QString& hotkeyStr) {
    if (hotkeyStr.isEmpty()) {
        return {};
    }

    QStringList parts;
    if (hotkeyStr == QLatin1String("+")) {
        parts = {QStringLiteral("+")};
    } else if (hotkeyStr.endsWith(QLatin1String("++"))) {
        parts = hotkeyStr.chopped(2).split(QLatin1Char('+'));
        parts.append(QStringLiteral("+"));
    } else {
        parts = hotkeyStr.split(QLatin1Char('+'));
    }

    QStringList rawMods;
    QString     baseKeyPart;

    for (const QString& part : std::as_const(parts)) {
        if (part.isEmpty()) {
            continue;
        }
        if (part == QLatin1String("Control") || part == QLatin1String("Alt") || part == QLatin1String("Super") || part == QLatin1String("Shift")) {
            if (!rawMods.contains(part)) {
                rawMods.append(part);
            }
        } else {
            baseKeyPart = part;
        }
    }

    if (!baseKeyPart.isEmpty()) {
        if (baseKeyPart.startsWith(QLatin1String("Shift_"))) {
            rawMods.removeAll(QStringLiteral("Shift"));
        } else if (baseKeyPart.startsWith(QLatin1String("Control_")) || baseKeyPart == QLatin1String("Control")) {
            rawMods.removeAll(QStringLiteral("Control"));
        } else if (baseKeyPart.startsWith(QLatin1String("Alt_")) || baseKeyPart == QLatin1String("Alt")) {
            rawMods.removeAll(QStringLiteral("Alt"));
        } else if (baseKeyPart.startsWith(QLatin1String("Super_")) || baseKeyPart.startsWith(QLatin1String("Meta_"))) {
            rawMods.removeAll(QStringLiteral("Super"));
        }
    }

    bool       explicitShiftNeeded = rawMods.contains(QStringLiteral("Shift"));
    const bool hasNonShiftModifier = rawMods.contains(QStringLiteral("Control")) || rawMods.contains(QStringLiteral("Alt")) || rawMods.contains(QStringLiteral("Super"));

    QString    baseKeyLabel;
    if (!baseKeyPart.isEmpty()) {
        if (baseKeyPart.size() == 1 && baseKeyPart.at(0).isUpper()) {
            explicitShiftNeeded = explicitShiftNeeded || !hasNonShiftModifier;
            baseKeyLabel        = std::move(baseKeyPart);
        } else if (hotkeyUiUnshiftMap().contains(baseKeyPart)) {
            explicitShiftNeeded    = true;
            const QString dispBase = hotkeyUiUnshiftMap().value(baseKeyPart);
            baseKeyLabel           = hotkeySymMap().value(dispBase, capitalizeKeyName(dispBase));
        } else {
            baseKeyLabel = hotkeySymMap().value(baseKeyPart, capitalizeKeyName(baseKeyPart));
        }
    }

    QStringList prettyParts;
    if (rawMods.contains(QStringLiteral("Control"))) {
        prettyParts.append(QStringLiteral("Ctrl"));
    }
    if (rawMods.contains(QStringLiteral("Alt"))) {
        prettyParts.append(QStringLiteral("Alt"));
    }
    if (rawMods.contains(QStringLiteral("Super"))) {
        prettyParts.append(QStringLiteral("Super"));
    }
    if (explicitShiftNeeded) {
        prettyParts.append(QStringLiteral("Shift"));
    }
    if (!baseKeyLabel.isEmpty()) {
        prettyParts.append(baseKeyLabel);
    }
    return prettyParts;
}
