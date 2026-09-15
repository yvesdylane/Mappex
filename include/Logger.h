#pragma once
#include <string>

enum class LogLevel { Debug, Info, Warning, Error };

class Logger {
public:
    static void log(LogLevel level, const std::string& message);

    static void debug(const std::string& msg)   { log(LogLevel::Debug, msg); }
    static void info(const std::string& msg)    { log(LogLevel::Info, msg); }
    static void warning(const std::string& msg) { log(LogLevel::Warning, msg); }
    static void error(const std::string& msg)   { log(LogLevel::Error, msg); }
};