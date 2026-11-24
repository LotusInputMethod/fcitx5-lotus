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
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
#include <QGuiApplication>
#include <QStyleHints>
#endif
#include <algorithm>

namespace {
    QColor blendColor(const QColor& a, const QColor& b, qreal t) {
        const qreal clamped = std::clamp(t, 0.0, 1.0);
        const qreal inv     = 1.0 - clamped;
        return QColor::fromRgbF(a.redF() * inv + b.redF() * clamped, a.greenF() * inv + b.greenF() * clamped, a.blueF() * inv + b.blueF() * clamped,
                                a.alphaF() * inv + b.alphaF() * clamped);
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
} // namespace

ThemeBridge::ThemeBridge(QObject* parent) : QObject(parent) {
    updateFromPalette(QApplication::palette());
    if (QCoreApplication* app = QCoreApplication::instance()) {
        app->installEventFilter(this);
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (auto* guiApp = qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
        if (QStyleHints* hints = guiApp->styleHints()) {
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
        bus.connect(QStringLiteral("org.freedesktop.portal.Desktop"), QStringLiteral("/org/freedesktop/portal/desktop"),
                    QStringLiteral("org.freedesktop.portal.Settings"), QStringLiteral("SettingChanged"), this,
                    SLOT(onPortalSettingChanged(QString, QString, QDBusVariant)));
        queryPortalColorScheme();
    }
}

void ThemeBridge::queryPortalColorScheme() {
    QDBusMessage msg = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.portal.Desktop"), QStringLiteral("/org/freedesktop/portal/desktop"),
                                                      QStringLiteral("org.freedesktop.portal.Settings"), QStringLiteral("ReadOne"));
    msg << QStringLiteral("org.freedesktop.appearance") << QStringLiteral("color-scheme");
    auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), this);
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
        updateFromPalette(QApplication::palette());
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
    const bool   dark        = (portalColorScheme_ == 1) ? true : ((portalColorScheme_ == 2) ? false : paletteDark);

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
