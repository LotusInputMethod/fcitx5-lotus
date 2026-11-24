/*
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef LOTUS_SETTINGS_THEME_BRIDGE_H
#define LOTUS_SETTINGS_THEME_BRIDGE_H

#include <QColor>
#include <QDBusVariant>
#include <QEvent>
#include <QFileSystemWatcher>
#include <QObject>
#include <QPalette>

/**
 * @brief Exposes reactive QPalette-derived theme tokens to QML.
 */
class ThemeBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool isDark READ isDark NOTIFY themeChanged)
    Q_PROPERTY(bool isLightMode READ isLightMode NOTIFY themeChanged)
    Q_PROPERTY(QColor surface READ surface NOTIFY themeChanged)
    Q_PROPERTY(QColor surfaceText READ surfaceText NOTIFY themeChanged)
    Q_PROPERTY(QColor surfaceVariant READ surfaceVariant NOTIFY themeChanged)
    Q_PROPERTY(QColor surfaceVariantText READ surfaceVariantText NOTIFY themeChanged)
    Q_PROPERTY(QColor surfaceContainer READ surfaceContainer NOTIFY themeChanged)
    Q_PROPERTY(QColor surfaceContainerLow READ surfaceContainerLow NOTIFY themeChanged)
    Q_PROPERTY(QColor surfaceContainerHigh READ surfaceContainerHigh NOTIFY themeChanged)
    Q_PROPERTY(QColor surfaceContainerHighest READ surfaceContainerHighest NOTIFY themeChanged)
    Q_PROPERTY(QColor primary READ primary NOTIFY themeChanged)
    Q_PROPERTY(QColor primaryText READ primaryText NOTIFY themeChanged)
    Q_PROPERTY(QColor primaryContainer READ primaryContainer NOTIFY themeChanged)
    Q_PROPERTY(QColor onPrimary READ onPrimary NOTIFY themeChanged)
    Q_PROPERTY(QColor onPrimaryContainer READ onPrimaryContainer NOTIFY themeChanged)
    Q_PROPERTY(QColor secondary READ secondary NOTIFY themeChanged)
    Q_PROPERTY(QColor secondaryContainer READ secondaryContainer NOTIFY themeChanged)
    Q_PROPERTY(QColor onSecondaryContainer READ onSecondaryContainer NOTIFY themeChanged)
    Q_PROPERTY(QColor outline READ outline NOTIFY themeChanged)
    Q_PROPERTY(QColor outlineVariant READ outlineVariant NOTIFY themeChanged)
    Q_PROPERTY(QColor error READ error NOTIFY themeChanged)
    Q_PROPERTY(QColor errorContainer READ errorContainer NOTIFY themeChanged)
    Q_PROPERTY(QColor onError READ onError NOTIFY themeChanged)
    Q_PROPERTY(QColor onErrorContainer READ onErrorContainer NOTIFY themeChanged)
    Q_PROPERTY(QColor warning READ warning NOTIFY themeChanged)
    Q_PROPERTY(QColor accent READ accent NOTIFY themeChanged)

  public:
    explicit ThemeBridge(QObject* parent = nullptr);

    bool   isDark() const;
    bool   isLightMode() const;
    QColor surface() const;
    QColor surfaceText() const;
    QColor surfaceVariant() const;
    QColor surfaceVariantText() const;
    QColor surfaceContainer() const;
    QColor surfaceContainerLow() const;
    QColor surfaceContainerHigh() const;
    QColor surfaceContainerHighest() const;
    QColor primary() const;
    QColor primaryText() const;
    QColor primaryContainer() const;
    QColor onPrimary() const;
    QColor onPrimaryContainer() const;
    QColor secondary() const;
    QColor secondaryContainer() const;
    QColor onSecondaryContainer() const;
    QColor outline() const;
    QColor outlineVariant() const;
    QColor error() const;
    QColor errorContainer() const;
    QColor onError() const;
    QColor onErrorContainer() const;
    QColor warning() const;
    QColor accent() const;

  signals:
    void themeChanged();

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private slots:
    void onPortalSettingChanged(const QString& ns, const QString& key, const QDBusVariant& value);
    void onQt6ctFileChanged(const QString& path);

  private:
    void                queryPortalColorScheme();
    void                setupQt6ctWatcher();
    void                reloadFromQt6ct();
    void                updateFromPalette(const QPalette& pal);

    QFileSystemWatcher* qt6ctWatcher_ = nullptr;
    QString             qt6ctConfigPath_;
    QString             qt6ctSchemePath_;
    int                 portalColorScheme_ = -1; // -1: follow palette, 1: prefer dark, 2: prefer light
    bool                isDark_            = false;
    QColor              surface_;
    QColor              surfaceText_;
    QColor              surfaceVariant_;
    QColor              surfaceVariantText_;
    QColor              surfaceContainer_;
    QColor              surfaceContainerLow_;
    QColor              surfaceContainerHigh_;
    QColor              surfaceContainerHighest_;
    QColor              primary_;
    QColor              primaryText_;
    QColor              primaryContainer_;
    QColor              onPrimary_;
    QColor              onPrimaryContainer_;
    QColor              secondary_;
    QColor              secondaryContainer_;
    QColor              onSecondaryContainer_;
    QColor              outline_;
    QColor              outlineVariant_;
    QColor              error_;
    QColor              errorContainer_;
    QColor              onError_;
    QColor              onErrorContainer_;
    QColor              warning_;
    QColor              accent_;
};

#endif // LOTUS_SETTINGS_THEME_BRIDGE_H
