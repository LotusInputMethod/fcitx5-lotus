/*
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "dbus_client.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QDebug>
#include <QMetaType>
#include <algorithm>

namespace {
    const QString kService        = QStringLiteral("org.fcitx.Fcitx5");
    const QString kObjectPath     = QStringLiteral("/controller");
    const QString kInterface      = QStringLiteral("org.fcitx.Fcitx.Controller1");
    const QString kAddonConfig    = QStringLiteral("fcitx://config/addon/lotus");
    constexpr int kIfaceTimeoutMs = 2000;

    QVariant      unwrapDbusValue(const QVariant& value);

    QVariant      readDbusElement(const QDBusArgument& arg) {
        switch (arg.currentType()) {
            case QDBusArgument::BasicType: return arg.asVariant();
            case QDBusArgument::VariantType: {
                QDBusVariant inner;
                arg >> inner;
                return unwrapDbusValue(inner.variant());
            }
            case QDBusArgument::MapType: {
                QVariantMap map;
                arg.beginMap();
                while (!arg.atEnd()) {
                    arg.beginMapEntry();
                    const QVariant k = readDbusElement(arg);
                    const QVariant v = readDbusElement(arg);
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
                    list.append(readDbusElement(arg));
                }
                arg.endArray();
                return list;
            }
            case QDBusArgument::StructureType: {
                QVariantList fields;
                arg.beginStructure();
                while (!arg.atEnd()) {
                    fields.append(readDbusElement(arg));
                }
                arg.endStructure();
                return fields;
            }
            default: return {};
        }
    }

    QVariant unwrapDbusValue(const QVariant& value) {
        if (value.metaType() == QMetaType::fromType<QDBusVariant>()) {
            return unwrapDbusValue(value.value<QDBusVariant>().variant());
        }
        if (value.metaType() == QMetaType::fromType<QDBusArgument>()) {
            const auto arg = value.value<QDBusArgument>();
            return readDbusElement(arg);
        }
        if (value.metaType() == QMetaType::fromType<QVariantMap>()) {
            const QVariantMap raw = value.toMap();
            QVariantMap       clean;
            for (auto it = raw.cbegin(); it != raw.cend(); ++it) {
                clean.insert(it.key(), unwrapDbusValue(it.value()));
            }
            return clean;
        }
        if (value.metaType() == QMetaType::fromType<QVariantList>()) {
            const QVariantList raw = value.toList();
            QVariantList       clean;
            clean.reserve(raw.size());
            for (const QVariant& item : raw) {
                clean.append(unwrapDbusValue(item));
            }
            return clean;
        }
        return value;
    }

    QVariant prepareDbusValue(const QVariant& value) {
        const QMetaType type = value.metaType();
        if (type == QMetaType::fromType<QVariantMap>()) {
            const QVariantMap raw = value.toMap();
            QVariantMap       out;
            for (auto it = raw.cbegin(); it != raw.cend(); ++it) {
                out.insert(it.key(), prepareDbusValue(it.value()));
            }
            return out;
        }
        if (type == QMetaType::fromType<QVariantList>()) {
            const QVariantList raw = value.toList();
            QVariantList       out;
            out.reserve(raw.size());
            for (const QVariant& item : raw) {
                out.append(prepareDbusValue(item));
            }
            return out;
        }
        if (type == QMetaType::fromType<QStringList>()) {
            const QStringList raw = value.toStringList();
            QVariantList      out;
            out.reserve(raw.size());
            for (const QString& item : raw) {
                out.append(item);
            }
            return out;
        }
        if (type == QMetaType::fromType<bool>()) {
            return value.toBool();
        }
        if (type == QMetaType::fromType<int>() || type == QMetaType::fromType<long long>() || type == QMetaType::fromType<unsigned int>() ||
            type == QMetaType::fromType<unsigned long long>()) {
            return static_cast<int>(value.toLongLong());
        }
        if (type == QMetaType::fromType<double>() || type == QMetaType::fromType<float>()) {
            return value.toDouble();
        }
        if (!value.isValid() || value.isNull()) {
            return QString();
        }
        return value.toString();
    }
} // namespace

LotusDBusClient::LotusDBusClient() {
    reconnect();
}

bool LotusDBusClient::isConnected() const {
    return connected_ && iface_ && iface_->isValid();
}

bool LotusDBusClient::reconnect() {
    iface_.reset();
    connected_ = false;

    const QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        return false;
    }
    const QDBusConnectionInterface* busIface = bus.interface();
    if ((busIface == nullptr) || !busIface->isServiceRegistered(kService).value()) {
        return false;
    }

    auto iface = std::make_unique<QDBusInterface>(kService, kObjectPath, kInterface, bus);
    if (!iface->isValid()) {
        return false;
    }
    iface->setTimeout(kIfaceTimeoutMs);
    iface_     = std::move(iface);
    connected_ = true;
    return true;
}

void LotusDBusClient::markDisconnectedIfServiceLost(const QDBusError& err) {
    switch (err.type()) {
        case QDBusError::ServiceUnknown:
        case QDBusError::Disconnected:
        case QDBusError::UnknownObject:
        case QDBusError::UnknownInterface:
        case QDBusError::NoReply:
        case QDBusError::Timeout:
            connected_ = false;
            iface_.reset();
            break;
        default: break;
    }
}

LotusDBusClient::ConfigData LotusDBusClient::fetchRawConfig(const QString& uri) {
    ConfigData result;
    if (!isConnected()) {
        return result;
    }

    const QDBusMessage reply = iface_->call(QStringLiteral("GetConfig"), uri);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        const QDBusError err(reply);
        qWarning() << "Fcitx5 GetConfig error for" << uri << ":" << err.message();
        markDisconnectedIfServiceLost(err);
        return result;
    }
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return result;
    }

    result.values = unwrapDbusValue(reply.arguments().at(0)).toMap();
    if (reply.arguments().size() >= 2) {
        result.metadata = unwrapDbusValue(reply.arguments().at(1)).toList();
    }
    result.ok = true;
    return result;
}

bool LotusDBusClient::sendRawConfig(const QString& uri, const QVariantMap& payload) {
    if (!isConnected()) {
        return false;
    }

    const QVariant     prepared = prepareDbusValue(payload);
    const QDBusVariant wrapped(prepared);
    const QDBusMessage reply = iface_->call(QStringLiteral("SetConfig"), uri, QVariant::fromValue(wrapped));
    if (reply.type() == QDBusMessage::ErrorMessage) {
        const QDBusError err(reply);
        qWarning() << "Fcitx5 SetConfig error for" << uri << ":" << err.message();
        markDisconnectedIfServiceLost(err);
        return false;
    }
    return reply.type() == QDBusMessage::ReplyMessage;
}

LotusDBusClient::ConfigData LotusDBusClient::getConfig() {
    return fetchRawConfig(kAddonConfig);
}

bool LotusDBusClient::setConfig(const QVariantMap& values) {
    return sendRawConfig(kAddonConfig, values);
}

bool LotusDBusClient::saveValues(const QVariantMap& updates) {
    ConfigData current = getConfig();
    if (!current.ok) {
        return false;
    }
    for (auto it = updates.cbegin(); it != updates.cend(); ++it) {
        current.values.insert(it.key(), it.value());
    }
    return setConfig(current.values);
}

QVariantList LotusDBusClient::getSubConfigList(const QString& subPath, const QString& rootKey) {
    const ConfigData data = fetchRawConfig(kAddonConfig + QLatin1Char('/') + subPath);
    if (!data.ok) {
        return {};
    }

    const QVariant rootVal = data.values.value(rootKey);
    if (rootVal.metaType() == QMetaType::fromType<QVariantList>()) {
        return rootVal.toList();
    }
    if (rootVal.metaType() == QMetaType::fromType<QVariantMap>()) {
        const QVariantMap dict = rootVal.toMap();
        QStringList       keys = dict.keys();
        std::ranges::sort(keys, [](const QString& a, const QString& b) {
            bool      okA = false;
            bool      okB = false;
            const int nA  = a.toInt(&okA);
            const int nB  = b.toInt(&okB);
            if (okA != okB) {
                return okA;
            }
            if (okA) {
                return nA < nB;
            }
            return a < b;
        });
        QVariantList out;
        out.reserve(keys.size());
        for (const QString& k : std::as_const(keys)) {
            out.append(dict.value(k));
        }
        return out;
    }
    return {};
}

bool LotusDBusClient::setSubConfigList(const QString& subPath, const QString& rootKey, const QVariantList& items) {
    QVariantMap indexed;
    for (int i = 0; i < items.size(); ++i) {
        indexed.insert(QString::number(i), items.at(i));
    }
    QVariantMap payload;
    payload.insert(rootKey, indexed);
    return sendRawConfig(kAddonConfig + QLatin1Char('/') + subPath, payload);
}
