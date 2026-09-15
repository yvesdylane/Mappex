#include "Logger.h"
#include <cstdio>

void Logger::log(LogLevel level, const std::string& message) {
    const char* prefix = "";
    FILE* stream = stdout;

    switch (level) {
        case LogLevel::Debug:   prefix = "[DEBUG]"; break;
        case LogLevel::Info:    prefix = "[INFO] "; break;
        case LogLevel::Warning: prefix = "[WARN] "; break;
        case LogLevel::Error:   prefix = "[ERROR]"; stream = stderr; break;
    }

    fprintf(stream, "%s %s\n", prefix, message.c_str());
    fflush(stream);   // preserve order/timeliness even when stdout is a pipe (fully buffered)
}