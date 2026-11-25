/*
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "theme_bridge.h"

#include <QApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
#include <QGuiApplication>
#include <QStyleHints>
#endif
#include <algorithm>

namespace {
    QColor blendColor(const QColor& a, const QColor& b, qreal t) {
        const float clamped = static_cast<float>(std::clamp(t, 0.0, 1.0));
        const float inv     = 1.0F - clamped;
        return QColor::fromRgbF((a.redF() * inv) + (b.redF() * clamped), (a.greenF() * inv) + (b.greenF() * clamped), (a.blueF() * inv) + (b.blueF() * clamped),
                                (a.alphaF() * inv) + (b.alphaF() * clamped));
    }

    int parsePortalColorScheme(const QVariant& raw) {
        QVariant v = raw;
        if (v.canConvert<QDBusVariant>()) {
            v = v.value<QDBusVariant>().variant();
        }
        bool      ok  = false;
        const int val = v.toInt(&ok);
        if (!ok || (val != 1 && val != 2)) {
            return -1;
        }
        return val;
    }

    QColor parseRgbTriplet(const QString& value) {
        const QStringList parts = value.split(QLatin1Char(','), Qt::SkipEmptyParts);
        if (parts.size() < 3) {
            return {};
        }
        bool      okR = false;
        bool      okG = false;
        bool      okB = false;
        const int r   = parts.at(0).trimmed().toInt(&okR);
        const int g   = parts.at(1).trimmed().toInt(&okG);
        const int b   = parts.at(2).trimmed().toInt(&okB);
        if (!okR || !okG || !okB) {
            return {};
        }
        return {std::clamp(r, 0, 255), std::clamp(g, 0, 255), std::clamp(b, 0, 255)};
    }

    bool loadKdeColorScheme(const QString& path, QPalette& outPal) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return false;
        }
        QString     section;
        QColor      winBg;
        QColor      winFg;
        QColor      viewBg;
        QColor      selBg;
        QColor      selFg;
        QTextStream in(&file);
        while (!in.atEnd()) {
            const QString line = in.readLine().trimmed();
            if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char(';'))) {
                continue;
            }
            if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
                section = line.mid(1, line.size() - 2);
                continue;
            }
            const qsizetype eq = line.indexOf(QLatin1Char('='));
            if (eq <= 0) {
                continue;
            }
            const QString key = line.left(eq).trimmed();
            const QString val = line.mid(eq + 1).trimmed();
            if (section == QLatin1String("Colors:Window")) {
                if (key == QLatin1String("BackgroundNormal")) {
                    winBg = parseRgbTriplet(val);
                } else if (key == QLatin1String("ForegroundNormal")) {
                    winFg = parseRgbTriplet(val);
                }
            } else if (section == QLatin1String("Colors:View")) {
                if (key == QLatin1String("BackgroundNormal")) {
                    viewBg = parseRgbTriplet(val);
                }
            } else if (section == QLatin1String("Colors:Selection")) {
                if (key == QLatin1String("BackgroundNormal")) {
                    selBg = parseRgbTriplet(val);
                } else if (key == QLatin1String("ForegroundNormal")) {
                    selFg = parseRgbTriplet(val);
                }
            }
        }
        if (!winBg.isValid() || !winFg.isValid()) {
            return false;
        }
        outPal.setColor(QPalette::Window, winBg);
        outPal.setColor(QPalette::WindowText, winFg);
        outPal.setColor(QPalette::Base, viewBg.isValid() ? viewBg : winBg);
        if (selBg.isValid()) {
            outPal.setColor(QPalette::Highlight, selBg);
        }
        if (selFg.isValid()) {
            outPal.setColor(QPalette::HighlightedText, selFg);
        }
        return true;
    }
} // namespace

ThemeBridge::ThemeBridge(QObject* parent) : QObject(parent) {
    updateFromPalette(QApplication::palette());
    if (QCoreApplication* app = QCoreApplication::instance()) {
        app->installEventFilter(this);
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (qobject_cast<QGuiApplication*>(QCoreApplication::instance()) != nullptr) {
        if (QStyleHints* hints = QGuiApplication::styleHints()) {
            connect(hints, &QStyleHints::colorSchemeChanged, this, [this](Qt::ColorScheme scheme) {
                if (scheme == Qt::ColorScheme::Dark) {
                    portalColorScheme_ = 1;
                } else if (scheme == Qt::ColorScheme::Light) {
                    portalColorScheme_ = 2;
                } else {
                    portalColorScheme_ = -1;
                }
                updateFromPalette(QApplication::palette());
            });
        }
    }
#endif

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (bus.isConnected()) {
        bus.connect(QStringLiteral("org.freedesktop.portal.Desktop"), QStringLiteral("/org/freedesktop/portal/desktop"), QStringLiteral("org.freedesktop.portal.Settings"),
                    QStringLiteral("SettingChanged"), this, SLOT(onPortalSettingChanged(QString, QString, QDBusVariant)));
        queryPortalColorScheme();
    }

    setupQt6ctWatcher();
}

void ThemeBridge::setupQt6ctWatcher() {
    const QString pt  = qEnvironmentVariable("QT_QPA_PLATFORMTHEME");
    const QString pt6 = qEnvironmentVariable("QT_QPA_PLATFORMTHEME_QT6");
    if (pt != QLatin1String("qt6ct") && pt6 != QLatin1String("qt6ct")) {
        return;
    }
    const QString cfgBase = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    if (cfgBase.isEmpty()) {
        return;
    }
    qt6ctConfigPath_ = cfgBase + QStringLiteral("/qt6ct/qt6ct.conf");
    if (!QFileInfo::exists(qt6ctConfigPath_)) {
        return;
    }
    qt6ctWatcher_ = std::make_unique<QFileSystemWatcher>();
    qt6ctWatcher_->addPath(qt6ctConfigPath_);
    connect(qt6ctWatcher_.get(), &QFileSystemWatcher::fileChanged, this, &ThemeBridge::onQt6ctFileChanged);
    reloadFromQt6ct();
}

void ThemeBridge::onQt6ctFileChanged(const QString& path) {
    QTimer::singleShot(50, this, [this, path] {
        if (qt6ctWatcher_ && QFileInfo::exists(path) && !qt6ctWatcher_->files().contains(path)) {
            qt6ctWatcher_->addPath(path);
        }
        reloadFromQt6ct();
    });
}

void ThemeBridge::reloadFromQt6ct() {
    if (qt6ctConfigPath_.isEmpty() || !QFileInfo::exists(qt6ctConfigPath_)) {
        return;
    }
    const QSettings settings(qt6ctConfigPath_, QSettings::IniFormat);
    const QString   schemePath = settings.value(QStringLiteral("Appearance/color_scheme_path")).toString();
    if (schemePath.isEmpty() || !QFileInfo::exists(schemePath)) {
        if (qt6ctWatcher_ && !qt6ctSchemePath_.isEmpty()) {
            qt6ctWatcher_->removePath(qt6ctSchemePath_);
        }
        qt6ctSchemePath_.clear();
        updateFromPalette(QApplication::palette());
        return;
    }
    if (qt6ctWatcher_) {
        if (!qt6ctSchemePath_.isEmpty() && qt6ctSchemePath_ != schemePath) {
            qt6ctWatcher_->removePath(qt6ctSchemePath_);
        }
        if (!qt6ctWatcher_->files().contains(schemePath)) {
            qt6ctWatcher_->addPath(schemePath);
        }
    }
    qt6ctSchemePath_ = schemePath;

    QPalette pal = QApplication::palette();
    if (loadKdeColorScheme(schemePath, pal)) {
        updateFromPalette(pal);
    }
}

void ThemeBridge::queryPortalColorScheme() {
    QDBusMessage msg = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.portal.Desktop"), QStringLiteral("/org/freedesktop/portal/desktop"),
                                                      QStringLiteral("org.freedesktop.portal.Settings"), QStringLiteral("ReadOne"));
    msg << QStringLiteral("org.freedesktop.appearance") << QStringLiteral("color-scheme");
    auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg),
                                                this); // NOLINT(cppcoreguidelines-owning-memory): parent owns it; finished handler calls deleteLater()
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        QDBusPendingReply<QVariant> reply = *w;
        if (reply.isError()) {
            return;
        }
        const int scheme = parsePortalColorScheme(reply.value());
        if (scheme != portalColorScheme_) {
            portalColorScheme_ = scheme;
            updateFromPalette(QApplication::palette());
        }
    });
}

void ThemeBridge::onPortalSettingChanged(const QString& ns, const QString& key, const QDBusVariant& value) {
    if (ns != QLatin1String("org.freedesktop.appearance") || key != QLatin1String("color-scheme")) {
        return;
    }
    const int scheme = parsePortalColorScheme(QVariant::fromValue(value));
    if (scheme != portalColorScheme_) {
        portalColorScheme_ = scheme;
        updateFromPalette(QApplication::palette());
    }
}

bool ThemeBridge::eventFilter(QObject* watched, QEvent* event) {
    if (event != nullptr && event->type() == QEvent::ApplicationPaletteChange) {
        if (!qt6ctSchemePath_.isEmpty()) {
            reloadFromQt6ct();
        } else {
            updateFromPalette(QApplication::palette());
        }
    }
    return QObject::eventFilter(watched, event);
}

void ThemeBridge::updateFromPalette(const QPalette& pal) {
    QColor       windowColor = pal.color(QPalette::Window);
    QColor       textColor   = pal.color(QPalette::WindowText);
    QColor       baseColor   = pal.color(QPalette::Base);
    const QColor highlight   = pal.color(QPalette::Highlight);
    const QColor highText    = pal.color(QPalette::HighlightedText);

    const bool   paletteDark = windowColor.lightnessF() < 0.5;
    const bool   hasQt6ct    = !qt6ctSchemePath_.isEmpty() && QFileInfo::exists(qt6ctSchemePath_);
    bool         dark        = paletteDark;
    if (!hasQt6ct) {
        if (portalColorScheme_ == 1) {
            dark = true;
        } else if (portalColorScheme_ == 2) {
            dark = false;
        }
    }

    if (dark != paletteDark) {
        if (dark) {
            windowColor = QColor(QStringLiteral("#141618"));
            textColor   = QColor(QStringLiteral("#eceef1"));
            baseColor   = QColor(QStringLiteral("#1b1d20"));
        } else {
            windowColor = QColor(QStringLiteral("#f6f7f9"));
            textColor   = QColor(QStringLiteral("#1b1c1f"));
            baseColor   = QColor(QStringLiteral("#ffffff"));
        }
    }

    isDark_             = dark;
    surface_            = windowColor;
    surfaceText_        = textColor;
    surfaceVariantText_ = blendColor(textColor, windowColor, 0.35);

    if (dark) {
        surfaceVariant_          = blendColor(windowColor, Qt::white, 0.08);
        surfaceContainerLow_     = blendColor(windowColor, Qt::white, 0.03);
        surfaceContainer_        = blendColor(windowColor, Qt::white, 0.055);
        surfaceContainerHigh_    = blendColor(windowColor, Qt::white, 0.105);
        surfaceContainerHighest_ = blendColor(windowColor, Qt::white, 0.155);
        primaryContainer_        = blendColor(highlight, windowColor, 0.68);
        onPrimaryContainer_      = blendColor(highlight, Qt::white, 0.85);
        secondaryContainer_      = blendColor(windowColor, Qt::white, 0.12);
        outline_                 = blendColor(windowColor, textColor, 0.22);
        outlineVariant_          = blendColor(windowColor, textColor, 0.12);
        error_                   = QColor(QStringLiteral("#f28b82"));
        onError_                 = QColor(QStringLiteral("#601410"));
        errorContainer_          = QColor(QStringLiteral("#8c1d18"));
        onErrorContainer_        = QColor(QStringLiteral("#f9dedc"));
        warning_                 = QColor(QStringLiteral("#fdd663"));
    } else {
        surfaceVariant_          = blendColor(windowColor, textColor, 0.06);
        surfaceContainerLow_     = blendColor(windowColor, Qt::white, 0.35);
        surfaceContainer_        = (baseColor.lightnessF() > windowColor.lightnessF()) ? baseColor : blendColor(windowColor, Qt::white, 0.65);
        surfaceContainerHigh_    = blendColor(windowColor, textColor, 0.055);
        surfaceContainerHighest_ = blendColor(windowColor, textColor, 0.095);
        primaryContainer_        = blendColor(highlight, Qt::white, 0.78);
        onPrimaryContainer_      = blendColor(highlight, Qt::black, 0.65);
        secondaryContainer_      = blendColor(highlight, Qt::white, 0.86);
        outline_                 = blendColor(windowColor, textColor, 0.20);
        outlineVariant_          = blendColor(windowColor, textColor, 0.11);
        error_                   = QColor(QStringLiteral("#c62828"));
        onError_                 = QColor(QStringLiteral("#ffffff"));
        errorContainer_          = QColor(QStringLiteral("#f9dedc"));
        onErrorContainer_        = QColor(QStringLiteral("#410e0b"));
        warning_                 = QColor(QStringLiteral("#b26a00"));
    }

    primary_              = highlight;
    primaryText_          = highText;
    onPrimary_            = highText;
    secondary_            = blendColor(highlight, textColor, 0.35);
    onSecondaryContainer_ = textColor;
    accent_               = highlight;

    emit themeChanged();
}

bool ThemeBridge::isDark() const {
    return isDark_;
}

bool ThemeBridge::isLightMode() const {
    return !isDark_;
}

QColor ThemeBridge::surface() const {
    return surface_;
}

QColor ThemeBridge::surfaceText() const {
    return surfaceText_;
}

QColor ThemeBridge::surfaceVariant() const {
    return surfaceVariant_;
}

QColor ThemeBridge::surfaceVariantText() const {
    return surfaceVariantText_;
}

QColor ThemeBridge::surfaceContainer() const {
    return surfaceContainer_;
}

QColor ThemeBridge::surfaceContainerLow() const {
    return surfaceContainerLow_;
}

QColor ThemeBridge::surfaceContainerHigh() const {
    return surfaceContainerHigh_;
}

QColor ThemeBridge::surfaceContainerHighest() const {
    return surfaceContainerHighest_;
}

QColor ThemeBridge::primary() const {
    return primary_;
}

QColor ThemeBridge::primaryText() const {
    return primaryText_;
}

QColor ThemeBridge::primaryContainer() const {
    return primaryContainer_;
}

QColor ThemeBridge::onPrimary() const {
    return onPrimary_;
}

QColor ThemeBridge::onPrimaryContainer() const {
    return onPrimaryContainer_;
}

QColor ThemeBridge::secondary() const {
    return secondary_;
}

QColor ThemeBridge::secondaryContainer() const {
    return secondaryContainer_;
}

QColor ThemeBridge::onSecondaryContainer() const {
    return onSecondaryContainer_;
}

QColor ThemeBridge::outline() const {
    return outline_;
}

QColor ThemeBridge::outlineVariant() const {
    return outlineVariant_;
}

QColor ThemeBridge::error() const {
    return error_;
}

QColor ThemeBridge::errorContainer() const {
    return errorContainer_;
}

QColor ThemeBridge::onError() const {
    return onError_;
}

QColor ThemeBridge::onErrorContainer() const {
    return onErrorContainer_;
}

QColor ThemeBridge::warning() const {
    return warning_;
}

QColor ThemeBridge::accent() const {
    return accent_;
}
