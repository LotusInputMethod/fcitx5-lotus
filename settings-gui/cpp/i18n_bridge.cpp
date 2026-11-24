/*
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "i18n_bridge.h"

#include <QLocale>
#include <clocale>
#include <libintl.h>

#ifndef LOCALE_DIR
#define LOCALE_DIR "/usr/share/locale"
#endif

namespace {
    constexpr const char* kTextDomain = "fcitx5-lotus";
} // namespace

I18nBridge::I18nBridge(QObject* parent) : QObject(parent) {
    std::setlocale(LC_CTYPE, "");
    std::setlocale(LC_MESSAGES, "");
    bindtextdomain(kTextDomain, LOCALE_DIR);
    bind_textdomain_codeset(kTextDomain, "UTF-8");
    textdomain(kTextDomain);
    isRtl_ = (QLocale().textDirection() == Qt::RightToLeft);
}

QString I18nBridge::tr(const QString& text) const {
    if (text.isEmpty()) {
        return text;
    }
    const QByteArray utf8       = text.toUtf8();
    const char*      translated = dgettext(kTextDomain, utf8.constData());
    return QString::fromUtf8(translated);
}

bool I18nBridge::isRtl() const {
    return isRtl_;
}
