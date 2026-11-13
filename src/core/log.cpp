#include <nexus/core/strings.hpp>
#include <nexus/core/log.hpp>

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <cstring>
#include <iostream>
#include <sstream>
#include <vector>

namespace nexus {

const char* log_level_name(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info: return "INFO";
        case LogLevel::Warn: return "WARN";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Fatal: return "FATAL";
        case LogLevel::Off: return "OFF";
    }
    return "?";
}

bool parse_log_level(const std::string& text, LogLevel& out) {
    const std::string lower = [&] {
        std::string s = text;
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }();
    if (lower == "trace") out = LogLevel::Trace;
    else if (lower == "debug") out = LogLevel::Debug;
    else if (lower == "info") out = LogLevel::Info;
    else if (lower == "warn" || lower == "warning") out = LogLevel::Warn;
    else if (lower == "error") out = LogLevel::Error;
    else if (lower == "fatal") out = LogLevel::Fatal;
    else if (lower == "off") out = LogLevel::Off;
    else return false;
    return true;
}

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

Logger::~Logger() {
    close_log_file();
}

void Logger::set_level(LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    level_ = level;
}

LogLevel Logger::level() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return level_;
}

bool Logger::set_log_file(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    file_.open(path, std::ios::out | std::ios::app);
    if (!file_.is_open()) {
        path_.clear();
        return false;
    }
    path_ = path;
    return true;
}

void Logger::close_log_file() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_.is_open()) file_.close();
}

std::string Logger::log_path() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return path_;
}

void Logger::log(LogLevel level, const char* file, int line, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (level < level_ || level_ == LogLevel::Off) return;

    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream ss;
    ss << std::put_time(&tm, "%H:%M:%S") << " [" << log_level_name(level) << "] ";
    const char* base = file;
    if (base) {
        const char* slash = std::strrchr(base, '/');
        if (slash) base = slash + 1;
    }
    if (base) ss << base << ":" << line << " ";
    ss << message;

    std::cerr << ss.str() << std::endl;
    if (file_.is_open()) {
        file_ << ss.str() << std::endl;
        file_.flush();
    }
}

void log_message(LogLevel level, const char* file, int line, const std::string& message) {
    Logger::instance().log(level, file, line, message);
}

void log_messagef(LogLevel level, const char* file, int line, const char* fmt, ...) {
    char stack[2048];
    va_list args;
    va_start(args, fmt);
    va_list copy;
    va_copy(copy, args);
    const int needed = std::vsnprintf(stack, sizeof(stack), fmt, args);
    va_end(args);
    std::string message;
    if (needed < 0) {
        message = "log formatting error";
    } else if (static_cast<size_t>(needed) < sizeof(stack)) {
        message.assign(stack, static_cast<size_t>(needed));
    } else {
        std::vector<char> heap(static_cast<size_t>(needed) + 1);
        std::vsnprintf(heap.data(), heap.size(), fmt, copy);
        message.assign(heap.data(), static_cast<size_t>(needed));
    }
    va_end(copy);
    Logger::instance().log(level, file, line, message);
}

} // namespace nexus
