#ifndef KPENGINE_RUNTIME_LOG_ENTRY_H
#define KPENGINE_RUNTIME_LOG_ENTRY_H

#include <string>
#include <chrono>

namespace kpengine::program{

    enum class LogLevel{
        Debug,
        Info,
        Warning,
        Error,
        Fatal
    };

    struct LogEntry{
        std::string name;
        LogLevel level;
        std::string message;
        int line;
        std::string file;
        std::chrono::system_clock::time_point timestamp;
    };
}

#endif