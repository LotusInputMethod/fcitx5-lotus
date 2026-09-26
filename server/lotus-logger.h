/*
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

/**
 * @file lotus-logger.h
 * @brief Thin syslog() wrapper for fcitx5-lotus-server.
 *
 * Messages go to the local syslog daemon (journald/rsyslog), which adds
 * timestamps and rotates the log.  Facility: LOG_DAEMON, ident:
 * "fcitx5-lotus-server".
 */

#ifndef _LOTUS_LOGGER_H_
#define _LOTUS_LOGGER_H_

#include <atomic>
#include <cstdint>
#include <string>

enum class LogLevel : std::uint8_t {
    DEBUG,
    INFO,
    WARN,
    ERROR,
    NONE
};

class LotusLogger {
  public:
    /**
     * @brief Instance constructor
     */
    static LotusLogger& instance();

    // Rule of five
    LotusLogger(const LotusLogger&)            = delete;
    LotusLogger& operator=(const LotusLogger&) = delete;
    LotusLogger(LotusLogger&&)                 = delete;
    LotusLogger& operator=(LotusLogger&&)      = delete;

    /**
     * @brief Set minimum log level
     */
    void setLevel(LogLevel level);

    /**
     * @brief Check if logging is enabled for given level
     */
    bool isEnabled(LogLevel level) const;

    // Convenience methods
    void debug(const std::string& msg) const {
        if (isEnabled(LogLevel::DEBUG))
            log(LogLevel::DEBUG, msg);
    }
    void info(const std::string& msg) const {
        if (isEnabled(LogLevel::INFO))
            log(LogLevel::INFO, msg);
    }
    void warn(const std::string& msg) const {
        if (isEnabled(LogLevel::WARN))
            log(LogLevel::WARN, msg);
    }
    void error(const std::string& msg) const {
        if (isEnabled(LogLevel::ERROR))
            log(LogLevel::ERROR, msg);
    }

  private:
    /**
     * @brief Constructor
     * @param level Minimum log level to output
     */
    LotusLogger(LogLevel level = LogLevel::INFO);

    /**
     * @brief Destructor
     */
    ~LotusLogger();

    /**
     * @brief Log a message through syslog()
     */
    static void           log(LogLevel level, const std::string& message);

    std::atomic<LogLevel> level_;
};

#endif // _LOTUS_LOGGER_H_
