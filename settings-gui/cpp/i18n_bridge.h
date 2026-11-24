/*
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef LOTUS_SETTINGS_I18N_BRIDGE_H
#define LOTUS_SETTINGS_I18N_BRIDGE_H

#include <QObject>
#include <QString>

/**
 * @brief Exposes gettext translations and text direction to QML.
 */
class I18nBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool isRtl READ isRtl CONSTANT)

  public:
    explicit I18nBridge(QObject* parent = nullptr);

    /**
     * @brief Translates an English msgid string using the fcitx5-lotus gettext domain.
     */
    Q_INVOKABLE [[nodiscard]] QString tr(const QString& text) const;

    /**
     * @brief Translates a msgid without an object; used by main() and tests.
     */
    [[nodiscard]] static QString lookup(const QString& text);

    /**
     * @brief Returns true when the active locale uses right-to-left layout.
     */
    [[nodiscard]] bool isRtl() const;

  private:
    bool isRtl_ = false;
};

#endif // LOTUS_SETTINGS_I18N_BRIDGE_H
