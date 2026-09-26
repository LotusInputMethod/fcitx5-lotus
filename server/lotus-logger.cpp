/*
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#include "lotus-logger.h"

#include <syslog.h>

LotusLogger& LotusLogger::instance() {
    static LotusLogger instance_;
    return instance_;
}

LotusLogger::LotusLogger(LogLevel level) {
    level_.store(level);
    openlog("fcitx5-lotus-server", LOG_PID, LOG_DAEMON);
}

LotusLogger::~LotusLogger() {
    closelog();
}

void LotusLogger::setLevel(LogLevel level) {
    level_.store(level);
}

bool LotusLogger::isEnabled(LogLevel level) const {
    return level >= level_.load();
}

void LotusLogger::log(LogLevel level, const std::string& message) {
    int priority = LOG_INFO;
    switch (level) {
        case LogLevel::DEBUG: priority = LOG_DEBUG; break;
        case LogLevel::INFO: priority = LOG_INFO; break;
        case LogLevel::WARN: priority = LOG_WARNING; break;
        case LogLevel::ERROR: priority = LOG_ERR; break;
        default: break;
    }
    // Message is data, never a format string.
    syslog(priority, "%s", message.c_str());
}
