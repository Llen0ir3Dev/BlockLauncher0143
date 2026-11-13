#pragma once

#include <cstdarg>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>

namespace nexus {

enum class LogLevel : int {
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warn = 3,
    Error = 4,
    Fatal = 5,
    Off = 6
};

const char* log_level_name(LogLevel level);
bool parse_log_level(const std::string& text, LogLevel& out);

class Logger {
public:
    static Logger& instance();

    void set_level(LogLevel level);
    LogLevel level() const;
    bool set_log_file(const std::string& path);
    void close_log_file();
    void log(LogLevel level, const char* file, int line, const std::string& message);

    std::string log_path() const;

private:
    Logger() = default;
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    mutable std::mutex mutex_;
    LogLevel level_ = LogLevel::Info;
    std::ofstream file_;
    std::string path_;
};

void log_message(LogLevel level, const char* file, int line, const std::string& message);
void log_messagef(LogLevel level, const char* file, int line, const char* fmt, ...);

} // namespace nexus

#define NEXUS_LOG_TRACE(msg) ::nexus::log_message(::nexus::LogLevel::Trace, __FILE__, __LINE__, (msg))
#define NEXUS_LOG_DEBUG(msg) ::nexus::log_message(::nexus::LogLevel::Debug, __FILE__, __LINE__, (msg))
#define NEXUS_LOG_INFO(msg)  ::nexus::log_message(::nexus::LogLevel::Info,  __FILE__, __LINE__, (msg))
#define NEXUS_LOG_WARN(msg)  ::nexus::log_message(::nexus::LogLevel::Warn,  __FILE__, __LINE__, (msg))
#define NEXUS_LOG_ERROR(msg) ::nexus::log_message(::nexus::LogLevel::Error, __FILE__, __LINE__, (msg))
#define NEXUS_LOG_FATAL(msg) ::nexus::log_message(::nexus::LogLevel::Fatal, __FILE__, __LINE__, (msg))
#define NEXUS_LOGF(level, ...) ::nexus::log_messagef(level, __FILE__, __LINE__, __VA_ARGS__)
