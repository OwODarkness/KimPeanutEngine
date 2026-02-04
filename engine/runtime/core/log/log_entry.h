#ifndef KPENGINE_RUNTIME_LOG_ENTRY_H
#define KPENGINE_RUNTIME_LOG_ENTRY_H

#include <string>
#include <chrono>

namespace kpengine::program
{

    enum class LogLevel
    {
        Debug,
        Info,
        Warning,
        Error,
        Fatal
    };

    struct LogEntry
    {

        std::string name;
        LogLevel level;
        std::string message;
        int line;
        std::string file;
        std::chrono::system_clock::time_point timestamp;

        LogEntry() : level(LogLevel::Info), line(0),
                     timestamp(std::chrono::system_clock::now()) {}

        LogEntry(std::string_view name, LogLevel level, std::string_view message,
                 int line = 0, std::string_view file = "")
            : name(name), level(level), message(message),
              line(line), file(file),
              timestamp(std::chrono::system_clock::now()) {}
    };
}

#endif