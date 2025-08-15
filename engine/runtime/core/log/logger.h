#ifndef KPENGINE_RUNTIME_LOGGER_H
#define KPENGINE_RUNTIME_LOGGER_H

#include <string>
#include <mutex>
#include <format>
#include <chrono>
#include <vector>
#include <cstdio>
#include <fstream>
#include "runtime/runtime_global_context.h"
#include "log_entry.h"

#define KP_LOG(LOG_NAME, LEVEL, MESSAGE, ...) \
    kpengine::program::Logger::GetLogger()->Log(LOG_NAME, LEVEL, __LINE__, __FILE__, MESSAGE, ##__VA_ARGS__)

#define LOG_LEVEL_DISPLAY kpengine::program::LogLevel::Info
#define LOG_LEVEL_WARNNING kpengine::program::LogLevel::Warning
#define LOG_LEVEL_ERROR kpengine::program::LogLevel::Error
#define LOG_LEVEL_FATAL kpengine::program::LogLevel::Fatal

// debug flag
#define KPENGINE_DEBUG

namespace kpengine::program
{

    class Logger
    {
    private:
        Logger();

    public:
        ~Logger();
        static Logger *GetLogger();
        void ExtractTipColorFromLogLevel(float (&color)[4], LogLevel level);
        void Tick();
        template <typename... Args>
        void Log(const std::string &log_name, LogLevel level, int line, const std::string &file, const std::string &msg, Args &&...args);
        static std::string FetchStringFromLog(const LogEntry& log);
    private:
        void WriteLog(const std::string &name, LogLevel level, const std::string msg, int line, const std::string &file, std::chrono::system_clock::time_point timestamp);
        void FlushToFile();
        bool CreateLogFile();
        void Reset();
    private:
        static Logger *log_single_instance_;
        static std::mutex log_mutex;

        std::vector<LogEntry> logs_;
        size_t last_flushed_index_{};
        std::ofstream file_;
        std::chrono::steady_clock::time_point last_flush_time_;
        const float flush_interval;
        const size_t flush_size_threshold;
        const size_t max_buf_size ;
        const size_t max_log_file_size;
        
        bool request_immediate_flush;
    };

    template <typename... Args>
    void Logger::Log(const std::string &log_name, LogLevel level, int line, const std::string &file, const std::string &msg, Args &&...args)
    {
        std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
        int size = std::snprintf(nullptr, 0, msg.c_str(), std::forward<Args>(args)...) + 1;
        std::vector<char> buffer(size);
        std::snprintf(buffer.data(), buffer.size(), msg.c_str(), std::forward<Args>(args)...);
        std::string message = std::string(buffer.data(), buffer.size() - 1);

        WriteLog(log_name, level, message, line, file, now);
    }
}

#endif