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

#include <memory>

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

    [[nodiscard]] bool   isDark() const;
    [[nodiscard]] bool   isLightMode() const;
    [[nodiscard]] QColor surface() const;
    [[nodiscard]] QColor surfaceText() const;
    [[nodiscard]] QColor surfaceVariant() const;
    [[nodiscard]] QColor surfaceVariantText() const;
    [[nodiscard]] QColor surfaceContainer() const;
    [[nodiscard]] QColor surfaceContainerLow() const;
    [[nodiscard]] QColor surfaceContainerHigh() const;
    [[nodiscard]] QColor surfaceContainerHighest() const;
    [[nodiscard]] QColor primary() const;
    [[nodiscard]] QColor primaryText() const;
    [[nodiscard]] QColor primaryContainer() const;
    [[nodiscard]] QColor onPrimary() const;
    [[nodiscard]] QColor onPrimaryContainer() const;
    [[nodiscard]] QColor secondary() const;
    [[nodiscard]] QColor secondaryContainer() const;
    [[nodiscard]] QColor onSecondaryContainer() const;
    [[nodiscard]] QColor outline() const;
    [[nodiscard]] QColor outlineVariant() const;
    [[nodiscard]] QColor error() const;
    [[nodiscard]] QColor errorContainer() const;
    [[nodiscard]] QColor onError() const;
    [[nodiscard]] QColor onErrorContainer() const;
    [[nodiscard]] QColor warning() const;
    [[nodiscard]] QColor accent() const;

  signals:
    void themeChanged();

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    Q_SLOT void                         onPortalSettingChanged(const QString& ns, const QString& key, const QDBusVariant& value);
    Q_SLOT void                         onQt6ctFileChanged(const QString& path);

    void                                queryPortalColorScheme();
    void                                setupQt6ctWatcher();
    void                                reloadFromQt6ct();
    void                                updateFromPalette(const QPalette& pal);

    std::unique_ptr<QFileSystemWatcher> qt6ctWatcher_;
    QString                             qt6ctConfigPath_;
    QString                             qt6ctSchemePath_;
    int                                 portalColorScheme_ = -1; // -1: follow palette, 1: prefer dark, 2: prefer light
    bool                                isDark_            = false;
    QColor                              surface_;
    QColor                              surfaceText_;
    QColor                              surfaceVariant_;
    QColor                              surfaceVariantText_;
    QColor                              surfaceContainer_;
    QColor                              surfaceContainerLow_;
    QColor                              surfaceContainerHigh_;
    QColor                              surfaceContainerHighest_;
    QColor                              primary_;
    QColor                              primaryText_;
    QColor                              primaryContainer_;
    QColor                              onPrimary_;
    QColor                              onPrimaryContainer_;
    QColor                              secondary_;
    QColor                              secondaryContainer_;
    QColor                              onSecondaryContainer_;
    QColor                              outline_;
    QColor                              outlineVariant_;
    QColor                              error_;
    QColor                              errorContainer_;
    QColor                              onError_;
    QColor                              onErrorContainer_;
    QColor                              warning_;
    QColor                              accent_;
};

#endif // LOTUS_SETTINGS_THEME_BRIDGE_H
