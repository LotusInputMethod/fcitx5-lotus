/*
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef LOTUS_SETTINGS_DBUS_CLIENT_H
#define LOTUS_SETTINGS_DBUS_CLIENT_H

#include <QDBusInterface>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <memory>

/**
 * @brief Native Qt6 D-Bus client for org.fcitx.Fcitx5 Controller1 configuration calls.
 */
class LotusDBusClient {
  public:
    struct ConfigData {
        bool         ok = false;
        QVariantMap  values;
        QVariantList metadata;
    };

    LotusDBusClient();

    [[nodiscard]] bool isConnected() const;
    bool               reconnect();

    ConfigData         getConfig();
    bool               setConfig(const QVariantMap& values);
    bool               saveValues(const QVariantMap& updates);
    QVariantList       getSubConfigList(const QString& subPath, const QString& rootKey);
    bool               setSubConfigList(const QString& subPath, const QString& rootKey, const QVariantList& items);

  private:
    ConfigData                      fetchRawConfig(const QString& uri);
    bool                            sendRawConfig(const QString& uri, const QVariantMap& payload);
    void                            markDisconnectedIfServiceLost(const QDBusError& err);

    std::unique_ptr<QDBusInterface> iface_;
    bool                            connected_ = false;
};

#endif // LOTUS_SETTINGS_DBUS_CLIENT_H
