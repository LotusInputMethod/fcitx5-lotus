/*
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

/**
 * @file lotus-logger.h
 * @brief Simple file logger with rotation for fcitx5-lotus-server
 *
 * Features:
 * - Thread-safe logging
 * - Automatic file rotation (max 10MB, keep 5 files)
 * - Timestamp and level formatting
 * - Configurable log levels
 */

#ifndef _LOTUS_LOGGER_H_
#define _LOTUS_LOGGER_H_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <mutex>

enum class LogLevel : std::uint8_t {
    DEBUG,
    INFO,
    WARN,
    ERROR,
    NONE
};

#ifdef LOTUS_DEBUG
class LotusLogger {
  public:
    // Const
    static constexpr size_t DEFAULT_MAX_SIZE  = static_cast<long>(10 * 1024) * 1024; // 10MB
    static constexpr size_t DEFAULT_MAX_FILES = 5;

    /**
     * @brief Instance constructor
     */
    static LotusLogger& instance() {
        static LotusLogger instance_;
        return instance_;
    }

    /**
     * @brief Destructor
     */
    ~LotusLogger();

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

    /**
     * @brief Log a message
     */
    void log(LogLevel level, const std::string& message);

    // Convenience methods
    void debug(const std::string& msg) {
        if (isEnabled(LogLevel::DEBUG))
            log(LogLevel::DEBUG, msg);
    }
    void info(const std::string& msg) {
        if (isEnabled(LogLevel::INFO))
            log(LogLevel::INFO, msg);
    }
    void warn(const std::string& msg) {
        if (isEnabled(LogLevel::WARN))
            log(LogLevel::WARN, msg);
    }
    void error(const std::string& msg) {
        if (isEnabled(LogLevel::ERROR))
            log(LogLevel::ERROR, msg);
    }

  private:
    /**
     * @brief Constructor
     * @param log_file Path to log file
     * @param max_size Maximum file size before rotation (bytes)
     * @param max_files Maximum number of backup files to keep
     * @param level Minimum log level to output
     */
    LotusLogger(std::string log_file = "/tmp/fcitx5-lotus-server.log", size_t max_size = DEFAULT_MAX_SIZE, LogLevel level = LogLevel::INFO, size_t max_files = DEFAULT_MAX_FILES);

    /**
     * @brief Rotate log files if current file exceeds max size
     */
    void rotateIfNeeded();

    /**
     * @brief Perform actual file rotation
     */
    void rotate();

    /**
     * @brief Get current timestamp string
     */
    static std::string getTimestamp();

    /**
     * @brief Get log level string
     */
    static std::string    levelToString(LogLevel level);

    std::string           log_file_;
    size_t                max_size_;
    size_t                max_files_;
    size_t                current_size_;
    std::atomic<LogLevel> level_;
    std::ofstream         file_;
    std::mutex            mutex_;
};
#else
class LotusLogger {
  public:
    static LotusLogger& instance() {
        static LotusLogger i;
        return i;
    }
    void setLevel(LogLevel) {}
    bool isEnabled(LogLevel) const {
        return false;
    }
    void log(LogLevel, const std::string&) {}
    void debug(const std::string&) {}
    void info(const std::string&) {}
    void warn(const std::string&) {}
    void error(const std::string&) {}
};
#endif // LOTUS_DEBUG
#endif // _LOTUS_LOGGER_H_