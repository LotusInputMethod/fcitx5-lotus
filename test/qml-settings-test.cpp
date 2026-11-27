// SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file qml-settings-test.cpp
 * @brief Unit tests for QML SettingsBackend, ThemeBridge, and I18nBridge.
 */

#include "lotus-config.h"

#include "i18n_bridge.h"
#include "settings_backend.h"
#include "test-utils.h"
#include "theme_bridge.h"

#include <QApplication>
#include <QByteArray>
#include <QColor>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusVariant>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QObject>
#include <QPalette>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlError>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <cstdio>
#include <exception>
#include <future>
#include <memory>
#include <string_view>
#include <utility>

namespace {

    bool check(const char* name, bool condition, const std::source_location& location = std::source_location::current()) {
        if (!condition) {
            reportFailure(name, "holds", "did not hold", "settings-gui C++ backend/bridge behavior must hold", location);
        }
        return condition;
    }

    QVariant unwrapMockDbusValue(const QVariant& value);

    QVariant readMockDbusElement(const QDBusArgument& arg) {
        switch (arg.currentType()) {
            case QDBusArgument::BasicType: return arg.asVariant();
            case QDBusArgument::VariantType: {
                QDBusVariant inner;
                arg >> inner;
                return unwrapMockDbusValue(inner.variant());
            }
            case QDBusArgument::MapType: {
                QVariantMap map;
                arg.beginMap();
                while (!arg.atEnd()) {
                    arg.beginMapEntry();
                    const QVariant k = readMockDbusElement(arg);
                    const QVariant v = readMockDbusElement(arg);
                    arg.endMapEntry();
                    map.insert(k.toString(), v);
                }
                arg.endMap();
                return map;
            }
            case QDBusArgument::ArrayType: {
                QVariantList list;
                arg.beginArray();
                while (!arg.atEnd()) {
                    list.append(readMockDbusElement(arg));
                }
                arg.endArray();
                return list;
            }
            case QDBusArgument::StructureType: {
                QVariantList fields;
                arg.beginStructure();
                while (!arg.atEnd()) {
                    fields.append(readMockDbusElement(arg));
                }
                arg.endStructure();
                return fields;
            }
            default: return {};
        }
    }

    QVariant unwrapMockDbusValue(const QVariant& value) {
        if (value.metaType() == QMetaType::fromType<QDBusVariant>()) {
            return unwrapMockDbusValue(value.value<QDBusVariant>().variant());
        }
        if (value.metaType() == QMetaType::fromType<QDBusArgument>()) {
            const auto arg = value.value<QDBusArgument>();
            return readMockDbusElement(arg);
        }
        if (value.metaType() == QMetaType::fromType<QVariantMap>()) {
            const QVariantMap raw = value.toMap();
            QVariantMap       clean;
            for (auto it = raw.cbegin(); it != raw.cend(); ++it) {
                clean.insert(it.key(), unwrapMockDbusValue(it.value()));
            }
            return clean;
        }
        if (value.metaType() == QMetaType::fromType<QVariantList>()) {
            const QVariantList raw = value.toList();
            QVariantList       clean;
            clean.reserve(raw.size());
            for (const QVariant& item : raw) {
                clean.append(unwrapMockDbusValue(item));
            }
            return clean;
        }
        return value;
    }

    QVariantList lotusTestMetadata() {
        auto makeItem = [](const QString& key, const QString& type, const QString& title, const QVariant& def, const QVariantMap& extra) {
            return QVariantList{key, type, title, def, extra};
        };
        auto enumItem = [&makeItem](const QString& key, const QString& title, const QVariant& def, const QVariantMap& labels) {
            return makeItem(key, QStringLiteral("Enum"), title, def, QVariantMap{{QStringLiteral("Enum"), labels}, {QStringLiteral("EnumI18n"), labels}});
        };

        const QVariantMap inputMethods = {
            {QStringLiteral("0"), QStringLiteral("Telex")},
            {QStringLiteral("1"), QStringLiteral("VNI")},
            {QStringLiteral("2"), QStringLiteral("Telex + VNI")},
            {QStringLiteral("3"), QStringLiteral("Telex + VNI + VIQR")},
            {QStringLiteral("4"), QStringLiteral("VIQR")},
            {QStringLiteral("5"), QStringLiteral("Microsoft layout")},
            {QStringLiteral("6"), QStringLiteral("VNI Bàn phím tiếng Pháp")},
            {QStringLiteral("7"), QStringLiteral("Custom")},
        };
        const QVariantMap modes = {
            {QStringLiteral("0"), QStringLiteral("OFF")},
            {QStringLiteral("1"), QStringLiteral("Uinput (Smooth)")},
            {QStringLiteral("2"), QStringLiteral("Uinput (Super Smooth)")},
            {QStringLiteral("3"), QStringLiteral("Surrounding Text")},
            {QStringLiteral("4"), QStringLiteral("Preedit")},
            {QStringLiteral("5"), QStringLiteral("Emoji Picker")},
        };
        const QVariantMap charsets = {
            {QStringLiteral("0"), QStringLiteral("Unicode")},
            {QStringLiteral("1"), QStringLiteral("BKHCM 1")},
            {QStringLiteral("2"), QStringLiteral("BKHCM 2")},
            {QStringLiteral("3"), QStringLiteral("NCR Decimal")},
            {QStringLiteral("4"), QStringLiteral("NCR Hex")},
            {QStringLiteral("5"), QStringLiteral("TCVN3 (ABC)")},
            {QStringLiteral("6"), QStringLiteral("UTF-8")},
            {QStringLiteral("7"), QStringLiteral("Unicode C string Decimal")},
            {QStringLiteral("8"), QStringLiteral("Unicode C string Hex")},
            {QStringLiteral("9"), QStringLiteral("Unicode tổ hợp")},
            {QStringLiteral("10"), QStringLiteral("VIQR")},
            {QStringLiteral("11"), QStringLiteral("VISCII")},
            {QStringLiteral("12"), QStringLiteral("VNI Windows")},
            {QStringLiteral("13"), QStringLiteral("VPS")},
            {QStringLiteral("14"), QStringLiteral("Vietware Full")},
            {QStringLiteral("15"), QStringLiteral("Vietware X")},
            {QStringLiteral("16"), QStringLiteral("Windows 1258 codepage")},
        };
        const QVariantMap transformThree = {
            {QStringLiteral("0"), QStringLiteral("Disabled")},
            {QStringLiteral("1"), QStringLiteral("Non-Start")},
            {QStringLiteral("2"), QStringLiteral("Everywhere")},
        };
        const QVariantMap iconThemes = {
            {QStringLiteral("0"), QStringLiteral("Auto")},
            {QStringLiteral("1"), QStringLiteral("Light")},
            {QStringLiteral("2"), QStringLiteral("Dark")},
        };
        const QVariantMap skipModifiers = {
            {QStringLiteral("0"), QStringLiteral("Disabled")},
            {QStringLiteral("1"), QStringLiteral("Shift")},
            {QStringLiteral("2"), QStringLiteral("Ctrl")},
            {QStringLiteral("3"), QStringLiteral("Alt")},
        };

        QVariantList items = {
            enumItem(QStringLiteral("InputMethod"), QStringLiteral("Input Method"), QStringLiteral("Telex"), inputMethods),
            enumItem(QStringLiteral("Mode"), QStringLiteral("Input Mode"), QStringLiteral("Preedit"), modes),
            enumItem(QStringLiteral("OutputCharset"), QStringLiteral("Output Charset"), QStringLiteral("Unicode"), charsets),
            enumItem(QStringLiteral("W2U"), QStringLiteral("Type w as ư"), QStringLiteral("Non-Start"), transformThree),
            enumItem(QStringLiteral("BracketTransform"), QStringLiteral("Bracket Keys"), QStringLiteral("Disabled"), transformThree),
            makeItem(QStringLiteral("UseLotusIcons"), QStringLiteral("Boolean"), QStringLiteral("Use Lotus Icons"), false, {}),
            enumItem(QStringLiteral("IconTheme"), QStringLiteral("Icon Theme"), QStringLiteral("Auto"), iconThemes),
            makeItem(QStringLiteral("PreferVirtualKeyboard"), QStringLiteral("Boolean"), QStringLiteral("System Keyboard Injection"), true, {}),
            makeItem(QStringLiteral("SpellCheck"), QStringLiteral("Boolean"), QStringLiteral("Spell Check"), true, {}),
            makeItem(QStringLiteral("AutoNonVnRestore"), QStringLiteral("Boolean"), QStringLiteral("Auto Restore"), true, {}),
            makeItem(QStringLiteral("DdFreeStyle"), QStringLiteral("Boolean"), QStringLiteral("Free dd to đ"), true, {}),
            makeItem(QStringLiteral("ModernStyle"), QStringLiteral("Boolean"), QStringLiteral("Modern Tone Style"), true, {}),
            makeItem(QStringLiteral("FreeMarking"), QStringLiteral("Boolean"), QStringLiteral("Free Tone Marking"), true, {}),
            makeItem(QStringLiteral("AutoCapitalizeAfterPunctuation"), QStringLiteral("Boolean"), QStringLiteral("Auto Capitalize"), false, {}),
            makeItem(QStringLiteral("DoubleSpaceToPeriod"), QStringLiteral("Boolean"), QStringLiteral("Double Space Period"), false, {}),
            makeItem(QStringLiteral("DoubleHyphenToEmDash"), QStringLiteral("Boolean"), QStringLiteral("Double Hyphen Dash"), false, {}),
            makeItem(QStringLiteral("ModeMenuKey"), QStringLiteral("List|Key"), QStringLiteral("Mode Menu Hotkey"), QStringLiteral("grave"), {}),
            makeItem(QStringLiteral("CycleModeKey"), QStringLiteral("List|Key"), QStringLiteral("Cycle Mode Hotkey"), QString(), {}),
            makeItem(QStringLiteral("EnableMacro"), QStringLiteral("Boolean"), QStringLiteral("Enable Macros"), true, {}),
            makeItem(QStringLiteral("CapitalizeMacro"), QStringLiteral("Boolean"), QStringLiteral("Match Case"), true, {}),
            makeItem(QStringLiteral("EnableMacroInOffMode"), QStringLiteral("Boolean"), QStringLiteral("Off-Mode Macros"), false, {}),
            enumItem(QStringLiteral("MacroSkipTriggerModifier"), QStringLiteral("Skip Modifier"), QStringLiteral("Disabled"), skipModifiers),
            makeItem(QStringLiteral("TimeFormat"), QStringLiteral("String"), QStringLiteral("Time Format"), QStringLiteral("%H:%M"), {}),
            makeItem(QStringLiteral("DateFormat"), QStringLiteral("String"), QStringLiteral("Date Format"), QStringLiteral("%d/%m/%Y"), {}),
            makeItem(QStringLiteral("EnableDictionary"), QStringLiteral("Boolean"), QStringLiteral("Enable Dictionary"), false, {}),
            makeItem(QStringLiteral("EnableCustomKeymap"), QStringLiteral("Boolean"), QStringLiteral("Custom Keymap"), false, {}),
            makeItem(QStringLiteral("ModeOrder"), QStringLiteral("String"), QStringLiteral("Mode Order"),
                     QStringLiteral("Smooth,SurroundingText,Preedit,Emoji,Off,SuperSmooth,Default"), {}),
            makeItem(QStringLiteral("DefaultCommitInterval"), QStringLiteral("Integer"), QStringLiteral("Commit Interval (ms)"), 20, {}),
            makeItem(QStringLiteral("DefaultBackspaceInterval"), QStringLiteral("Integer"), QStringLiteral("Backspace Interval (ms)"), 5, {}),
            makeItem(QStringLiteral("DefaultPostDelay"), QStringLiteral("Integer"), QStringLiteral("Post Delay (ms)"), 10, {}),
            makeItem(QStringLiteral("ShowModeSmooth"), QStringLiteral("Boolean"), QStringLiteral("Show Mode"), true, {}),
            makeItem(QStringLiteral("ShortcutSmooth"), QStringLiteral("String"), QStringLiteral("Shortcut for Smooth"), QStringLiteral("1"), {}),
            makeItem(QStringLiteral("ShowModeSuperSmooth"), QStringLiteral("Boolean"), QStringLiteral("Show Mode"), true, {}),
            makeItem(QStringLiteral("ShortcutSuperSmooth"), QStringLiteral("String"), QStringLiteral("Shortcut for SuperSmooth"), QStringLiteral("a"), {}),
            makeItem(QStringLiteral("ShowModeSurroundingText"), QStringLiteral("Boolean"), QStringLiteral("Show Mode"), true, {}),
            makeItem(QStringLiteral("ShortcutSurroundingText"), QStringLiteral("String"), QStringLiteral("Shortcut for SurroundingText"), QStringLiteral("4"), {}),
            makeItem(QStringLiteral("ShowModePreedit"), QStringLiteral("Boolean"), QStringLiteral("Show Mode"), true, {}),
            makeItem(QStringLiteral("ShortcutPreedit"), QStringLiteral("String"), QStringLiteral("Shortcut for Preedit"), QStringLiteral("q"), {}),
            makeItem(QStringLiteral("ShowModeEmoji"), QStringLiteral("Boolean"), QStringLiteral("Show Mode"), true, {}),
            makeItem(QStringLiteral("ShortcutEmoji"), QStringLiteral("String"), QStringLiteral("Shortcut for Emoji"), QStringLiteral("w"), {}),
            makeItem(QStringLiteral("ShowModeOff"), QStringLiteral("Boolean"), QStringLiteral("Show Mode"), true, {}),
            makeItem(QStringLiteral("ShortcutOff"), QStringLiteral("String"), QStringLiteral("Shortcut for Off"), QStringLiteral("e"), {}),
            makeItem(QStringLiteral("ShowModeDefault"), QStringLiteral("Boolean"), QStringLiteral("Show Mode"), true, {}),
            makeItem(QStringLiteral("ShortcutDefault"), QStringLiteral("String"), QStringLiteral("Shortcut for Default"), QStringLiteral("r"), {}),
        };
        return {QVariantList{QStringLiteral("Lotus"), items}};
    }

    class MockFcitxController final : public QObject {
        Q_OBJECT
        Q_CLASSINFO("D-Bus Interface", "org.fcitx.Fcitx.Controller1")

      public:
        explicit MockFcitxController(QObject* parent = nullptr) : QObject(parent) {
            store_.insert(QStringLiteral("fcitx://config/addon/lotus"), QVariantMap());
            store_.insert(QStringLiteral("fcitx://config/addon/lotus/app_rules"), QVariantMap());
            store_.insert(QStringLiteral("fcitx://config/addon/lotus/lotus-macro"), QVariantMap());
            store_.insert(QStringLiteral("fcitx://config/addon/lotus/custom_keymap"), QVariantMap());
        }

        void resetUri(const QString& uri) {
            const QMutexLocker locker(&mutex_);
            store_.insert(uri, QVariantMap());
        }

        [[nodiscard]] int setCount(const QString& uri) const {
            const QMutexLocker locker(&mutex_);
            return setCounts_.value(uri, 0);
        }

      private:
        mutable QMutex              mutex_;
        QHash<QString, QVariantMap> store_;
        QHash<QString, int>         setCounts_;

      public Q_SLOTS:
        QVariantMap GetConfig(const QString& uri, QVariantList& metadata) {
            metadata = lotusTestMetadata();
            const QMutexLocker locker(&mutex_);
            return store_.value(uri);
        }

        void SetConfig(const QString& uri, const QDBusVariant& value) {
            const QVariantMap  unwrapped = unwrapMockDbusValue(value.variant()).toMap();
            const QMutexLocker locker(&mutex_);
            store_.insert(uri, unwrapped);
            setCounts_[uri] = setCounts_.value(uri, 0) + 1;
        }
    };

    class PrivateDbusDaemon final {
      public:
        PrivateDbusDaemon() {
            proc_.start(QStringLiteral("dbus-daemon"),
                        {QStringLiteral("--config-file=") + QStringLiteral(LOTUS_TEST_DBUS_CONFIG), QStringLiteral("--print-address=1"), QStringLiteral("--nofork")});
            if (!proc_.waitForStarted(5000)) {
                return;
            }
            if (!proc_.waitForReadyRead(5000)) {
                return;
            }
            const QByteArray raw = proc_.readAllStandardOutput();
            address_             = QString::fromUtf8(raw).trimmed().section(QLatin1Char('\n'), 0, 0).trimmed();
        }

        ~PrivateDbusDaemon() {
            if (proc_.state() != QProcess::NotRunning) {
                proc_.terminate();
                if (!proc_.waitForFinished(2000)) {
                    proc_.kill();
                    proc_.waitForFinished(2000);
                }
            }
        }

        PrivateDbusDaemon(const PrivateDbusDaemon&)            = delete;
        PrivateDbusDaemon& operator=(const PrivateDbusDaemon&) = delete;
        PrivateDbusDaemon(PrivateDbusDaemon&&)                 = delete;
        PrivateDbusDaemon& operator=(PrivateDbusDaemon&&)      = delete;

        [[nodiscard]] bool valid() const {
            return !address_.isEmpty() && proc_.state() == QProcess::Running;
        }

        [[nodiscard]] const QString& address() const {
            return address_;
        }

      private:
        QProcess proc_;
        QString  address_;
    };

    class MockServerRunner final : public QThread {
      public:
        explicit MockServerRunner(QString address) : address_(std::move(address)) {
            start();
            started_ = readyPromise_.get_future().get();
        }

        ~MockServerRunner() override {
            quit();
            wait(3000);
        }

        MockServerRunner(const MockServerRunner&)            = delete;
        MockServerRunner& operator=(const MockServerRunner&) = delete;
        MockServerRunner(MockServerRunner&&)                 = delete;
        MockServerRunner&  operator=(MockServerRunner&&)     = delete;

        [[nodiscard]] bool valid() const {
            return started_;
        }

        [[nodiscard]] MockFcitxController* controller() const {
            return controller_.get();
        }

      protected:
        void run() override {
            QDBusConnection bus = QDBusConnection::connectToBus(address_, QStringLiteral("mock-fcitx-conn"));
            if (!bus.isConnected()) {
                readyPromise_.set_value(false);
                return;
            }
            controller_ = std::make_unique<MockFcitxController>();
            if (!bus.registerService(QStringLiteral("org.fcitx.Fcitx5")) ||
                !bus.registerObject(QStringLiteral("/controller"), controller_.get(), QDBusConnection::ExportAllSlots)) {
                controller_.reset();
                QDBusConnection::disconnectFromBus(QStringLiteral("mock-fcitx-conn"));
                readyPromise_.set_value(false);
                return;
            }
            readyPromise_.set_value(true);
            exec();
            bus.unregisterObject(QStringLiteral("/controller"));
            bus.unregisterService(QStringLiteral("org.fcitx.Fcitx5"));
            controller_.reset();
            QDBusConnection::disconnectFromBus(QStringLiteral("mock-fcitx-conn"));
        }

      private:
        QString                              address_;
        std::promise<bool>                   readyPromise_;
        bool                                 started_ = false;
        std::unique_ptr<MockFcitxController> controller_;
    };

    void testAppModeOptions(SettingsBackend& backend) {
        const QVariantList options = backend.getAppModeOptions();
        check("app mode options count == 6", options.size() == 6);
        const QList<int> expected = {0, 1, 3, 4, 5, 6};
        for (qsizetype i = 0; i < options.size() && i < expected.size(); ++i) {
            check("app mode option maps to the same LotusMode ordinal", options.at(i).toMap().value(QStringLiteral("value")).toInt() == expected.at(i));
        }
    }

    void testDebugExportOnThreadPool(SettingsBackend& backend, const QString& tempDirPath) {
        const QString path = QDir(tempDirPath).filePath(QStringLiteral("debug-export.log"));
        QEventLoop    loop;
        bool          ok = false;
        QObject::connect(&backend, &SettingsBackend::debugExportFinished, &loop, [&](bool result, const QString&) {
            ok = result;
            loop.quit();
        });
        QTimer::singleShot(25000, &loop, &QEventLoop::quit);
        backend.exportDebugInfo(QUrl::fromLocalFile(path).toString());
        loop.exec();
        check("debug export finishes on thread pool", ok);
        check("debug export writes a non-empty file", QFileInfo(path).size() > 0);
    }

    void testFactoryDefaults(const SettingsBackend& backend) {
        const fcitx::lotusConfig cfg;
        const QVariantMap        vals = backend.values();

        check("factory default Mode == Preedit",
              vals.value(QStringLiteral("Mode")).toString() == QString::fromStdString(fcitx::LotusModeToString(*cfg.mode)) &&
                  vals.value(QStringLiteral("Mode")).toString() == QLatin1String("Preedit"));
        check("factory default InputMethod == Telex",
              vals.value(QStringLiteral("InputMethod")).toString() == QString::fromStdString(fcitx::InputMethodIdToString(*cfg.inputMethod)));
        check("factory default OutputCharset == Unicode",
              vals.value(QStringLiteral("OutputCharset")).toString() == QString::fromStdString(fcitx::CharsetToString(*cfg.outputCharset)));
        check("factory default W2U == Non-Start", vals.value(QStringLiteral("W2U")).toString() == QString::fromStdString(fcitx::W2uModeToString(*cfg.w2u)));
        check("factory default BracketTransform == Disabled",
              vals.value(QStringLiteral("BracketTransform")).toString() == QString::fromStdString(fcitx::BracketTransformModeToString(*cfg.bracketTransform)));
        check("factory default UseLotusIcons == false", vals.value(QStringLiteral("UseLotusIcons")).toBool() == *cfg.useLotusIcons && !*cfg.useLotusIcons);
        check("factory default IconTheme == Auto", vals.value(QStringLiteral("IconTheme")).toString() == QString::fromStdString(fcitx::IconThemeToString(*cfg.iconTheme)));
        check("factory default PreferVirtualKeyboard", vals.value(QStringLiteral("PreferVirtualKeyboard")).toBool() == *cfg.preferVirtualKeyboard);
        check("factory default SpellCheck", vals.value(QStringLiteral("SpellCheck")).toBool() == *cfg.spellCheck);
        check("factory default AutoNonVnRestore", vals.value(QStringLiteral("AutoNonVnRestore")).toBool() == *cfg.autoNonVnRestore);
        check("factory default DdFreeStyle", vals.value(QStringLiteral("DdFreeStyle")).toBool() == *cfg.ddFreeStyle);
        check("factory default ModernStyle == true", vals.value(QStringLiteral("ModernStyle")).toBool() == *cfg.modernStyle && *cfg.modernStyle);
        check("factory default FreeMarking", vals.value(QStringLiteral("FreeMarking")).toBool() == *cfg.freeMarking);
        check("factory default AutoCapitalizeAfterPunctuation", vals.value(QStringLiteral("AutoCapitalizeAfterPunctuation")).toBool() == *cfg.autoCapitalizeAfterPunctuation);
        check("factory default DoubleSpaceToPeriod", vals.value(QStringLiteral("DoubleSpaceToPeriod")).toBool() == *cfg.doubleSpaceToPeriod);
        check("factory default DoubleHyphenToEmDash", vals.value(QStringLiteral("DoubleHyphenToEmDash")).toBool() == *cfg.doubleHyphenToEmDash);
        check("factory default ModeMenuKey == grave",
              !cfg.modeMenuKey.value().empty() && vals.value(QStringLiteral("ModeMenuKey")).toString() == QString::fromStdString(cfg.modeMenuKey.value().front().toString()) &&
                  vals.value(QStringLiteral("ModeMenuKey")).toString() == QLatin1String("grave"));
        check("factory default CycleModeKey == empty", cfg.cycleModeKey.value().empty() && vals.value(QStringLiteral("CycleModeKey")).toString().isEmpty());
        check("factory default EnableMacro", vals.value(QStringLiteral("EnableMacro")).toBool() == *cfg.enableMacro);
        check("factory default CapitalizeMacro", vals.value(QStringLiteral("CapitalizeMacro")).toBool() == *cfg.capitalizeMacro);
        check("factory default EnableMacroInOffMode", vals.value(QStringLiteral("EnableMacroInOffMode")).toBool() == *cfg.enableMacroInOffMode);
        check("factory default MacroSkipTriggerModifier",
              vals.value(QStringLiteral("MacroSkipTriggerModifier")).toString() == QString::fromStdString(fcitx::MacroSkipTriggerModifierToString(*cfg.macroSkipTriggerModifier)));
        check("factory default TimeFormat", vals.value(QStringLiteral("TimeFormat")).toString() == QString::fromStdString(*cfg.timeFormat));
        check("factory default DateFormat", vals.value(QStringLiteral("DateFormat")).toString() == QString::fromStdString(*cfg.dateFormat));
        check("factory default EnableDictionary == false", vals.value(QStringLiteral("EnableDictionary")).toBool() == *cfg.enableDictionary && !*cfg.enableDictionary);
        check("factory default EnableCustomKeymap", vals.value(QStringLiteral("EnableCustomKeymap")).toBool() == *cfg.enableCustomKeymap);
        check("factory default ModeOrder", vals.value(QStringLiteral("ModeOrder")).toString() == QString::fromStdString(*cfg.modeOrder));
        check("factory default DefaultCommitInterval", vals.value(QStringLiteral("DefaultCommitInterval")).toInt() == *cfg.defaultCommitInterval);
        check("factory default DefaultBackspaceInterval", vals.value(QStringLiteral("DefaultBackspaceInterval")).toInt() == *cfg.defaultBackspaceInterval);
        check("factory default DefaultPostDelay", vals.value(QStringLiteral("DefaultPostDelay")).toInt() == *cfg.defaultPostDelay);
    }

    void testKeyAndHotkeyFormatting() {
        check("formatKeyEvent ignores Escape", SettingsBackend::formatKeyEvent(Qt::Key_Escape, Qt::NoModifier, QString(), true).isEmpty());
        check("formatKeyEvent ignores bare Control", SettingsBackend::formatKeyEvent(Qt::Key_Control, Qt::ControlModifier, QString(), true).isEmpty());
        check("formatKeyEvent ignores bare Shift", SettingsBackend::formatKeyEvent(Qt::Key_Shift, Qt::ShiftModifier, QString(), true).isEmpty());
        check("formatKeyEvent singleKeyOnly digit", SettingsBackend::formatKeyEvent(Qt::Key_1, Qt::NoModifier, QStringLiteral("1"), false, true) == QLatin1String("1"));
        check("formatKeyEvent singleKeyOnly alpha", SettingsBackend::formatKeyEvent(Qt::Key_A, Qt::NoModifier, QStringLiteral("a"), false, true) == QLatin1String("a"));
        check("formatKeyEvent rejects plain letter when allowSingleKey=false", SettingsBackend::formatKeyEvent(Qt::Key_A, Qt::NoModifier, QStringLiteral("a"), false).isEmpty());
        check("formatKeyEvent accepts plain letter when allowSingleKey=true",
              SettingsBackend::formatKeyEvent(Qt::Key_A, Qt::NoModifier, QStringLiteral("a"), true) == QLatin1String("a"));
        check("formatKeyEvent allows grave as special single key",
              SettingsBackend::formatKeyEvent(Qt::Key_QuoteLeft, Qt::NoModifier, QStringLiteral("`"), false) == QLatin1String("grave"));
        check("formatKeyEvent allows F1 as special single key", SettingsBackend::formatKeyEvent(Qt::Key_F1, Qt::NoModifier, QString(), false) == QLatin1String("F1"));
        check("formatKeyEvent Control+Shift+S",
              SettingsBackend::formatKeyEvent(Qt::Key_S, Qt::ControlModifier | Qt::ShiftModifier, QString(), false) == QLatin1String("Control+Shift+S"));
        check("formatKeyEvent Control+Alt+grave",
              SettingsBackend::formatKeyEvent(Qt::Key_QuoteLeft, Qt::ControlModifier | Qt::AltModifier, QString(), false) == QLatin1String("Control+Alt+grave"));
        check("formatKeyEvent shifted asciitilde omits redundant Shift",
              SettingsBackend::formatKeyEvent(Qt::Key_AsciiTilde, Qt::ShiftModifier, QStringLiteral("~"), true) == QLatin1String("asciitilde"));

        check("prettyHotkeyParts empty", SettingsBackend::prettyHotkeyParts(QString()).isEmpty());
        check("prettyHotkeyParts grave", SettingsBackend::prettyHotkeyParts(QStringLiteral("grave")) == QStringList{QStringLiteral("`")});
        check("prettyHotkeyParts Control+Shift+S",
              SettingsBackend::prettyHotkeyParts(QStringLiteral("Control+Shift+S")) == (QStringList{QStringLiteral("Ctrl"), QStringLiteral("Shift"), QStringLiteral("S")}));
        check("prettyHotkeyParts Control+Alt+Super+space",
              SettingsBackend::prettyHotkeyParts(QStringLiteral("Control+Alt+Super+space")) ==
                  (QStringList{QStringLiteral("Ctrl"), QStringLiteral("Alt"), QStringLiteral("Super"), QStringLiteral("Space")}));
        check("prettyHotkeyParts asciitilde adds Shift",
              SettingsBackend::prettyHotkeyParts(QStringLiteral("asciitilde")) == (QStringList{QStringLiteral("Shift"), QStringLiteral("`")}));
        check("prettyHotkeyParts plus literal", SettingsBackend::prettyHotkeyParts(QStringLiteral("+")) == QStringList{QStringLiteral("+")});
        check("prettyHotkeyParts Control++", SettingsBackend::prettyHotkeyParts(QStringLiteral("Control++")) == (QStringList{QStringLiteral("Ctrl"), QStringLiteral("+")}));
        check("prettyHotkeyParts Shift_L", SettingsBackend::prettyHotkeyParts(QStringLiteral("Shift_L")) == QStringList{QStringLiteral("L-Shift")});
    }

    void testDuplicateShortcutValidation(SettingsBackend& backend) {
        check("initially no validation errors", !backend.hasValidationErrors() && backend.validationMessage().isEmpty());

        backend.setModeShortcut(QStringLiteral("Preedit"), QStringLiteral("1"));
        check("duplicate shortcut sets hasValidationErrors", backend.hasValidationErrors());
        check("duplicate shortcut message mentions key", backend.validationMessage().contains(QLatin1String("1")));

        int conflictCount = 0;
        for (const QVariant& itemVar : backend.modeList()) {
            const QVariantMap item = itemVar.toMap();
            if (item.value(QStringLiteral("conflict")).toBool()) {
                ++conflictCount;
            }
        }
        check("two visible modes flagged with conflict", conflictCount == 2);
        check("applyAll blocked while duplicate shortcut exists", !backend.applyAll());

        backend.setModeVisible(QStringLiteral("Preedit"), false);
        check("hiding one conflicting mode resolves duplicate shortcut", !backend.hasValidationErrors());

        backend.cancelAll();
        check("cancelAll restores valid shortcuts and clears unsaved flag", !backend.hasUnsavedChanges() && !backend.hasValidationErrors());
    }

    void testMacros(SettingsBackend& backend) {
        check("isValidMacroKey accepts ASCII alphanumeric", SettingsBackend::isValidMacroKey(QStringLiteral("kg2")));
        check("isValidMacroKey accepts Unicode letters", SettingsBackend::isValidMacroKey(QStringLiteral("từ1")));
        check("isValidMacroKey rejects empty", !SettingsBackend::isValidMacroKey(QString()));
        check("isValidMacroKey rejects space", !SettingsBackend::isValidMacroKey(QStringLiteral("a b")));
        check("isValidMacroKey rejects hyphen", !SettingsBackend::isValidMacroKey(QStringLiteral("a-b")));
        check("isValidMacroKey rejects underscore", !SettingsBackend::isValidMacroKey(QStringLiteral("a_b")));
        check("isValidMacroKey rejects punctuation", !SettingsBackend::isValidMacroKey(QStringLiteral("a!")));

        backend.upsertMacro(QString(), QStringLiteral("zz"), QStringLiteral("last"));
        backend.upsertMacro(QString(), QStringLiteral("aa"), QStringLiteral("first"));
        backend.upsertMacro(QString(), QStringLiteral("Bb"), QStringLiteral("middle"));

        const QVariantList sorted = backend.macros();
        check("macros sorted alphabetically count == 3", sorted.size() == 3);
        if (sorted.size() == 3) {
            check("macro[0] == aa", sorted.at(0).toMap().value(QStringLiteral("key")).toString() == QLatin1String("aa"));
            check("macro[1] == Bb", sorted.at(1).toMap().value(QStringLiteral("key")).toString() == QLatin1String("Bb"));
            check("macro[2] == zz", sorted.at(2).toMap().value(QStringLiteral("key")).toString() == QLatin1String("zz"));
        }

        backend.upsertMacro(QStringLiteral("aa"), QStringLiteral("cc"), QStringLiteral("updated"));
        const QVariantList renamed = backend.macros();
        check("macro rename preserves count == 3", renamed.size() == 3);
        if (renamed.size() == 3) {
            check("macro rename re-sorts Bb before cc",
                  renamed.at(0).toMap().value(QStringLiteral("key")).toString() == QLatin1String("Bb") &&
                      renamed.at(1).toMap().value(QStringLiteral("key")).toString() == QLatin1String("cc"));
        }

        backend.upsertMacro(QString(), QStringLiteral("bad key"), QStringLiteral("val"));
        check("invalid macro sets hasValidationErrors", backend.hasValidationErrors());
        backend.removeMacro(QStringLiteral("bad key"));
        check("removing invalid macro clears validation error", !backend.hasValidationErrors());

        backend.cancelAll();
        check("cancelAll clears macro edits", backend.macros().isEmpty() && !backend.hasUnsavedChanges());
    }

    void testDictionary(SettingsBackend& backend) {
        check("isValidWord accepts plain word", SettingsBackend::isValidWord(QStringLiteral("lotus")));
        check("isValidWord accepts trimmed word", SettingsBackend::isValidWord(QStringLiteral("  tiếng  ")));
        check("isValidWord rejects empty", !SettingsBackend::isValidWord(QString()));
        check("isValidWord rejects whitespace", !SettingsBackend::isValidWord(QStringLiteral("   ")));
        check("isValidWord rejects spaces", !SettingsBackend::isValidWord(QStringLiteral("two words")));
        check("isValidWord rejects tabs", !SettingsBackend::isValidWord(QStringLiteral("a\tb")));

        const int initialCount = backend.totalWordCount();
        backend.addWord(QStringLiteral("customlotusword"));
        check("addWord increments totalWordCount", backend.totalWordCount() == initialCount + 1);
        backend.addWord(QStringLiteral("customlotusword"));
        check("addWord ignores duplicate", backend.totalWordCount() == initialCount + 1);
        check("filterWords finds added word", backend.filterWords(QStringLiteral("customlotusword"), 10).contains(QStringLiteral("customlotusword")));
        check("countMatchingWords >= 1", backend.countMatchingWords(QStringLiteral("customlotusword")) == 1);

        backend.removeWord(QStringLiteral("customlotusword"));
        check("removeWord restores totalWordCount", backend.totalWordCount() == initialCount);
        backend.cancelAll();
    }

    void testKeymapPresetsAndActions(SettingsBackend& backend) {
        const QVariantList presets = backend.keymapPresets();
        const QVariantList actions = backend.bambooActions();

        QSet<QString>      validActionCodes;
        for (const QVariant& actVar : actions) {
            validActionCodes.insert(actVar.toMap().value(QStringLiteral("code")).toString());
        }

        check("keymap presets are available", !presets.isEmpty());
        const QString firstName = presets.constFirst().toMap().value(QStringLiteral("name")).toString();
        backend.applyKeymapPreset(firstName);
        const QVariantList bindings = backend.keymapBindings();
        check("applyKeymapPreset populates non-empty bindings", !bindings.isEmpty());
        for (const QVariant& bindVar : bindings) {
            check("preset binding references valid bambooAction", validActionCodes.contains(bindVar.toMap().value(QStringLiteral("value")).toString()));
        }

        backend.upsertKeymap(QStringLiteral("q"), QStringLiteral("DauSac"));
        check("upsertKeymap marks unsaved changes", backend.hasUnsavedChanges());
        backend.removeKeymap(QStringLiteral("q"));
        backend.cancelAll();
        check("cancelAll clears keymap edits", backend.keymapBindings().isEmpty() && !backend.hasUnsavedChanges());
    }

    void testSectionResetApplyAndBackupV2(SettingsBackend& backend, MockFcitxController* mockCtrl, const QString& tempDirPath) {
        backend.retryConnection();
        check("retryConnection connects to mock Fcitx5 service", !backend.fcitxOffline());

        backend.setValue(QStringLiteral("ModernStyle"), false);
        backend.setValue(QStringLiteral("Mode"), QStringLiteral("Uinput (Smooth)"));
        backend.addAppRule(QStringLiteral("kitty"));
        backend.updateAppRule(QStringLiteral("kitty"), 4, 15, 6, 12);
        backend.upsertMacro(QString(), QStringLiteral("btw"), QStringLiteral("by the way"));
        backend.applyKeymapPreset(QStringLiteral("VNI"));
        backend.addWord(QStringLiteral("lotusbackuptest"));
        check("staged changes set hasUnsavedChanges", backend.hasUnsavedChanges());

        check("applyAll succeeds against live mock D-Bus", backend.applyAll());
        check("applyAll clears hasUnsavedChanges", !backend.hasUnsavedChanges());
        check("mock server received main config", mockCtrl->setCount(QStringLiteral("fcitx://config/addon/lotus")) >= 1);
        check("mock server received app_rules", mockCtrl->setCount(QStringLiteral("fcitx://config/addon/lotus/app_rules")) >= 1);
        check("mock server received lotus-macro", mockCtrl->setCount(QStringLiteral("fcitx://config/addon/lotus/lotus-macro")) >= 1);
        check("mock server received custom_keymap", mockCtrl->setCount(QStringLiteral("fcitx://config/addon/lotus/custom_keymap")) >= 1);

        check("tone_punctuation section modified after ModernStyle=false", backend.isSectionModifiedFromDefault(QStringLiteral("tone_punctuation")));
        check("getSectionResetChanges returns change entry", !backend.getSectionResetChanges(QStringLiteral("tone_punctuation")).isEmpty());
        check("resetSection tone_punctuation succeeds", backend.resetSection(QStringLiteral("tone_punctuation")));
        check("resetSection restores ModernStyle == true", backend.values().value(QStringLiteral("ModernStyle")).toBool());
        check("tone_punctuation no longer modified from default", !backend.isSectionModifiedFromDefault(QStringLiteral("tone_punctuation")));

        const QString     backupPath    = QDir(tempDirPath).filePath(QStringLiteral("lotus-backup-v2.json"));
        const QString     backupUrl     = QUrl::fromLocalFile(backupPath).toString();
        const QVariantMap allComponents = {
            {QStringLiteral("config"), true},  {QStringLiteral("rules"), true},      {QStringLiteral("macros"), true},
            {QStringLiteral("keymaps"), true}, {QStringLiteral("dictionary"), true},
        };

        check("exportBackup v2 succeeds", backend.exportBackup(backupUrl, allComponents));
        QFile backupFile(backupPath);
        check("exported backup file exists and opens", backupFile.open(QIODevice::ReadOnly));
        const QJsonDocument doc = QJsonDocument::fromJson(backupFile.readAll());
        backupFile.close();
        check("backup JSON is an object", doc.isObject());
        const QJsonObject root = doc.object();
        const QJsonObject meta = root.value(QStringLiteral("meta")).toObject();
        check("backup meta.version == 2", meta.value(QStringLiteral("version")).toInt() == 2);
        check("backup meta.app == fcitx5-lotus", meta.value(QStringLiteral("app")).toString() == QLatin1String("fcitx5-lotus"));
        check("backup meta.components has 5 entries", meta.value(QStringLiteral("components")).toArray().size() == 5);
        check("backup contains all 5 sections",
              root.value(QStringLiteral("config")).isObject() && root.value(QStringLiteral("rules")).isArray() && root.value(QStringLiteral("macros")).isArray() &&
                  root.value(QStringLiteral("keymaps")).isArray() && root.value(QStringLiteral("dictionary")).isArray());

        const QVariantMap inspected = backend.inspectBackup(backupUrl);
        check("inspectBackup marks valid", inspected.value(QStringLiteral("valid")).toBool());
        check("inspectBackup version == 2", inspected.value(QStringLiteral("version")).toInt() == 2);
        check("inspectBackup flags all 5 components",
              inspected.value(QStringLiteral("hasConfig")).toBool() && inspected.value(QStringLiteral("hasRules")).toBool() &&
                  inspected.value(QStringLiteral("hasMacros")).toBool() && inspected.value(QStringLiteral("hasKeymaps")).toBool() &&
                  inspected.value(QStringLiteral("hasDictionary")).toBool());
        check("inspectBackup rejects nonexistent file", !backend.inspectBackup(QStringLiteral("/nonexistent/lotus-missing.json")).value(QStringLiteral("valid")).toBool());

        // Clear all state on mock server & local dictionary, then restore via importBackup.
        check("resetSection app_rules succeeds", backend.resetSection(QStringLiteral("app_rules")));
        check("resetSection macro_list succeeds", backend.resetSection(QStringLiteral("macro_list")));
        check("resetSection keymap_bindings succeeds", backend.resetSection(QStringLiteral("keymap_bindings")));
        check("resetSection dict_words succeeds", backend.resetSection(QStringLiteral("dict_words")));
        check("resetSection stages changes without writing", backend.hasUnsavedChanges());
        backend.cancelAll();
        mockCtrl->resetUri(QStringLiteral("fcitx://config/addon/lotus"));
        backend.retryConnection();

        check("importBackup v2 succeeds", backend.importBackup(backupUrl, allComponents));
        check("importBackup restored Mode == Uinput (Smooth)", backend.values().value(QStringLiteral("Mode")).toString() == QLatin1String("Uinput (Smooth)"));
        check("importBackup restored app rule kitty",
              backend.appRules().size() == 1 && backend.appRules().first().toMap().value(QStringLiteral("app")).toString() == QLatin1String("kitty"));
        check("importBackup restored macro btw", backend.macros().size() == 1 && backend.macros().first().toMap().value(QStringLiteral("key")).toString() == QLatin1String("btw"));
        check("importBackup restored VNI keymap bindings", backend.keymapBindings().size() == 10);
        check("importBackup restored custom dictionary word", !backend.filterWords(QStringLiteral("lotusbackuptest"), 10).isEmpty());
    }

    void testThemeAndI18nBridges() {
        QPalette lightPal;
        lightPal.setColor(QPalette::Window, QColor(QStringLiteral("#f5f5f5")));
        lightPal.setColor(QPalette::WindowText, QColor(QStringLiteral("#111111")));
        lightPal.setColor(QPalette::Base, QColor(QStringLiteral("#ffffff")));
        lightPal.setColor(QPalette::Highlight, QColor(QStringLiteral("#1a73e8")));
        lightPal.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#ffffff")));
        QApplication::setPalette(lightPal);

        ThemeBridge theme;
        int         themeChangeCount = 0;
        QObject::connect(&theme, &ThemeBridge::themeChanged, [&themeChangeCount] { ++themeChangeCount; });

        check("ThemeBridge light mode isDark == false", !theme.isDark() && theme.isLightMode());
        check("ThemeBridge surface matches Window color", theme.surface() == QColor(QStringLiteral("#f5f5f5")));
        check("ThemeBridge primary matches Highlight color", theme.primary() == QColor(QStringLiteral("#1a73e8")));
        check("ThemeBridge error and warning are valid colors", theme.error().isValid() && theme.warning().isValid() && theme.outline().isValid());

        QPalette darkPal;
        darkPal.setColor(QPalette::Window, QColor(QStringLiteral("#1e1e1e")));
        darkPal.setColor(QPalette::WindowText, QColor(QStringLiteral("#eeeeee")));
        darkPal.setColor(QPalette::Base, QColor(QStringLiteral("#252525")));
        darkPal.setColor(QPalette::Highlight, QColor(QStringLiteral("#8ab4f8")));
        darkPal.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#111111")));
        QApplication::setPalette(darkPal);
        QApplication::processEvents();

        check("ThemeBridge emits themeChanged on palette update", themeChangeCount >= 1);
        check("ThemeBridge dark mode isDark == true", theme.isDark() && !theme.isLightMode());
        check("ThemeBridge dark surface updated", theme.surface() == QColor(QStringLiteral("#1e1e1e")));

        qputenv("LC_ALL", "C");
        I18nBridge i18n;
        check("I18nBridge empty string stays empty", I18nBridge::lookup(QString()).isEmpty());
        check("I18nBridge translates non-empty key", !I18nBridge::lookup(QStringLiteral("General")).isEmpty());
        check("I18nBridge C locale is not RTL", !i18n.isRtl());
    }

    void testQmlLoadsFromQrc(I18nBridge& i18n, ThemeBridge& theme, SettingsBackend& backend) {
        QQmlApplicationEngine engine;
        engine.addImportPath(QStringLiteral("qrc:/lotus/qml"));
        engine.rootContext()->setContextProperty(QStringLiteral("I18n"), &i18n);
        engine.rootContext()->setContextProperty(QStringLiteral("Theme"), &theme);
        engine.rootContext()->setContextProperty(QStringLiteral("Backend"), &backend);
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/lotus/qml/Main.qml")));
        if (!component.isReady()) {
            for (const QQmlError& error : component.errors()) {
                qWarning() << error.toString();
            }
        }
        check("QML Main.qml compiles from qrc", component.isReady());
    }

} // namespace

int main(int argc, char* argv[]) {
    try {
        if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
            qputenv("QT_QPA_PLATFORM", "offscreen");
        }
        qunsetenv("QT_QPA_PLATFORMTHEME");
        qunsetenv("QT_QPA_PLATFORMTHEME_QT6");
        qunsetenv("QT_IM_MODULE");
        qunsetenv("QT_IM_MODULES");

        const QTemporaryDir tempDir;
        if (!check("temporary test directory created", tempDir.isValid())) {
            return 1;
        }
        qputenv("XDG_DATA_HOME", tempDir.path().toUtf8());

        const PrivateDbusDaemon dbusDaemon;
        if (!check("private dbus-daemon started", dbusDaemon.valid())) {
            return 1;
        }
        qputenv("DBUS_SESSION_BUS_ADDRESS", dbusDaemon.address().toUtf8());

        QApplication app(argc, argv);

        testThemeAndI18nBridges();

        I18nBridge      i18n;
        SettingsBackend backend(&i18n);
        check("SettingsBackend starts offline when org.fcitx.Fcitx5 is absent", backend.fcitxOffline());

        const MockServerRunner mockServer(dbusDaemon.address());
        if (!check("mock Fcitx5 D-Bus server registered", mockServer.valid() && mockServer.controller() != nullptr)) {
            return 1;
        }
        backend.retryConnection();
        check("retryConnection brings backend online", !backend.fcitxOffline());

        ThemeBridge themeBridge;
        testQmlLoadsFromQrc(i18n, themeBridge, backend);

        testFactoryDefaults(backend);
        testAppModeOptions(backend);
        testDebugExportOnThreadPool(backend, tempDir.path());
        testKeyAndHotkeyFormatting();
        testDuplicateShortcutValidation(backend);
        testMacros(backend);
        testDictionary(backend);
        testKeymapPresetsAndActions(backend);

        testSectionResetApplyAndBackupV2(backend, mockServer.controller(), tempDir.path());

        return checkExitCode("qml settings test: all checks passed");
    } catch (const std::exception& error) {
        reportFailure("unexpected exception", "no exception escapes the test", error.what(), "an escaped exception would terminate settings-gui tests");
        return checkExitCode();
    }
}

#include "qml-settings-test.moc"
