/*
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "i18n_bridge.h"
#include "settings_backend.h"
#include "theme_bridge.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickImageProvider>
#include <QQuickStyle>
#include <QUrl>

#ifndef LOTUS_VERSION
#define LOTUS_VERSION "dev"
#endif

namespace {
    class ThemeIconProvider final : public QQuickImageProvider {
      public:
        ThemeIconProvider() : QQuickImageProvider(QQuickImageProvider::Pixmap) {}

        QPixmap requestPixmap(const QString& id, QSize* size, const QSize& requestedSize) override {
            const QSize targetSize = (requestedSize.isValid() && !requestedSize.isEmpty()) ? requestedSize : QSize(24, 24);
            QPixmap     pixmap;

            if (!id.isEmpty()) {
                if (QFileInfo::exists(id)) {
                    pixmap.load(id);
                    if (!pixmap.isNull() && pixmap.size() != targetSize) {
                        pixmap = pixmap.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                    }
                } else {
                    QIcon icon = QIcon::fromTheme(id);
                    if (icon.isNull() && id.endsWith(QStringLiteral("-symbolic"))) {
                        icon = QIcon::fromTheme(id.chopped(9));
                    }
                    if (!icon.isNull()) {
                        pixmap = icon.pixmap(targetSize);
                    }
                }
            }

            if (size != nullptr) {
                *size = pixmap.size();
            }
            return pixmap;
        }
    };
} // namespace

int main(int argc, char* argv[]) {
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QApplication    app(argc, argv);

    I18nBridge      i18nBridge;
    ThemeBridge     themeBridge;
    SettingsBackend settingsBackend(&i18nBridge);

    QApplication::setApplicationName(QStringLiteral("fcitx5-lotus-settings"));
    QApplication::setApplicationDisplayName(I18nBridge::tr(QStringLiteral("Lotus Settings")));
    QApplication::setApplicationVersion(QStringLiteral(LOTUS_VERSION));
    QApplication::setOrganizationDomain(QStringLiteral("fcitx.org"));
    QApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("fcitx-lotus")));
    QGuiApplication::setDesktopFileName(QStringLiteral("org.fcitx.Fcitx5.Addon.Lotus.Settings"));

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("theme"), new ThemeIconProvider()); // NOLINT(cppcoreguidelines-owning-memory)

    QUrl             mainUrl(QStringLiteral("qrc:/lotus/qml/Main.qml"));

    const QByteArray qmlDirEnv = qgetenv("LOTUS_QML_DIR");
    if (!qmlDirEnv.isEmpty()) {
        const QDir qmlDir(QString::fromLocal8Bit(qmlDirEnv));
        engine.addImportPath(qmlDir.absolutePath());
        const QString nestedQmlDir = qmlDir.filePath(QStringLiteral("qml"));
        if (QFileInfo::exists(nestedQmlDir)) {
            engine.addImportPath(nestedQmlDir);
        }

        const QString directMain = qmlDir.filePath(QStringLiteral("Main.qml"));
        const QString nestedMain = qmlDir.filePath(QStringLiteral("qml/Main.qml"));
        if (QFileInfo::exists(directMain)) {
            mainUrl = QUrl::fromLocalFile(directMain);
        } else if (QFileInfo::exists(nestedMain)) {
            mainUrl = QUrl::fromLocalFile(nestedMain);
        }
    }

    engine.addImportPath(QStringLiteral("qrc:/lotus/qml"));

    engine.rootContext()->setContextProperty(QStringLiteral("I18n"), &i18nBridge);
    engine.rootContext()->setContextProperty(QStringLiteral("Theme"), &themeBridge);
    engine.rootContext()->setContextProperty(QStringLiteral("Backend"), &settingsBackend);

    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreated, &app,
        [mainUrl](QObject* obj, const QUrl& objUrl) {
            if (obj == nullptr && mainUrl == objUrl) {
                QCoreApplication::exit(-1);
            }
        },
        Qt::QueuedConnection);

    engine.load(mainUrl);
    if (engine.rootObjects().isEmpty()) {
        return -1;
    }

    return QApplication::exec();
}
