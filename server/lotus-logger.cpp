/*
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */
#ifdef LOTUS_DEBUG
#include "lotus-logger.h"

#include <cstddef>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <iostream>
#include <filesystem>
#include <utility>

#include <sys/stat.h>

LotusLogger::LotusLogger(std::string log_file, size_t max_size, LogLevel level, size_t max_files) : log_file_(std::move(log_file)), max_size_(max_size), max_files_(max_files) {
    level_.store(level);

    std::filesystem::path path(log_file_);
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }

    file_.open(log_file_, std::ios_base::app | std::ios_base::out);

    if (!file_.is_open()) {
        std::cerr << "[ERROR] Failed to open log file: " << log_file_ << '\n';
    }
    struct stat st{};
    if (stat(log_file_.c_str(), &st) == 0) {
        current_size_ = st.st_size;
    } else {
        current_size_ = 0;
    }
}

LotusLogger::~LotusLogger() {
    if (file_.is_open()) {
        file_.close();
    }
}

void LotusLogger::setLevel(LogLevel level) {
    level_.store(level);
}

bool LotusLogger::isEnabled(LogLevel level) const {
    return level >= level_.load();
}

void LotusLogger::log(LogLevel level, const std::string& message) {
    if (!isEnabled(level) || !file_.is_open()) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    rotateIfNeeded();

    std::string entry = getTimestamp() + " [" + levelToString(level) + "] " + message + "\n";

    file_ << entry;
    if (level >= LogLevel::WARN) {
        file_.flush();
    }

    current_size_ += entry.size();
}

void LotusLogger::rotateIfNeeded() {
    if (current_size_ > max_size_) {
        rotate();
    }
}

void LotusLogger::rotate() {
    if (!file_.is_open()) {
        return;
    }
    file_.close();

    for (size_t i = max_files_; i-- > 0;) {
        std::string old_file = (i == 0) ? log_file_ : log_file_ + "." + std::to_string(i);
        std::string new_file = log_file_ + "." + std::to_string(i + 1);

        if (std::filesystem::exists(old_file)) {
            if (i == max_files_ - 1) {
                std::filesystem::remove(new_file);
            }
            std::filesystem::rename(old_file, new_file);
        }
    }

    file_.open(log_file_, std::ios_base::app | std::ios_base::out);
}

std::string LotusLogger::getTimestamp() {
    auto now    = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers)
    auto      ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    struct tm buf{};
    localtime_r(&time_t, &buf);

    std::stringstream ss;
    ss << std::put_time(&buf, "%Y-%m-%d %H:%M:%S");
    ss << "." << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

std::string LotusLogger::levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO: return "INFO";
        case LogLevel::WARN: return "WARN";
        case LogLevel::ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}
#endif // LOTUS_DEBUG
