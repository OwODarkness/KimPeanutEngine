#ifndef KPENGINE_RUNTIME_LOGGER_H
#define KPENGINE_RUNTIME_LOGGER_H

#include <array>
#include <chrono>
#include <cstdio>
#include <format>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "log_entry.h"

#define KP_LOG(LOG_NAME, LEVEL, MESSAGE, ...) \
kpengine::program::Logger::GetLogger().Log(LOG_NAME, LEVEL, __LINE__, __FILE__, MESSAGE, ##__VA_ARGS__)

#define KP_LOGF(LOG_NAME, LEVEL, FORMAT, ...) \
kpengine::program::Logger::GetLogger().LogFormat(LOG_NAME, LEVEL, __LINE__, __FILE__, FORMAT, ##__VA_ARGS__)

#define LOG_LEVEL_DEBUG kpengine::program::LogLevel::Debug
#define LOG_LEVEL_INFO kpengine::program::LogLevel::Info
#define LOG_LEVEL_WARNING kpengine::program::LogLevel::Warning
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
        ~Logger();

    public:
        Logger(const Logger &) = delete;
        Logger &operator=(const Logger &) = delete;
        static Logger &GetLogger();
        void Tick();
        void Log(std::string_view log_name, LogLevel level, int line,
                 std::string_view file, std::string_view message);

        // Legacy printf-style entry point. Prefer LogFormat for new code.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        void Log(std::string_view log_name, LogLevel level, int line,
                 std::string_view file, const char *format, Args &&...args);

        // C++20 formatting entry point. The current MSVC standard library
        // provides vformat but does not expose std::format_string.
        template <typename... Args>
        void LogFormat(std::string_view log_name, LogLevel level, int line,
                       std::string_view file, std::string_view format,
                       Args &&...args);

        static std::string FetchStringFromLog(const LogEntry &log);
        // Returns a stable copy. The logger owns the live buffer and may mutate it
        // concurrently with readers.
        std::vector<LogEntry> Get() const;
        // Explicitly named alias for callers that want to document the copy.
        std::vector<LogEntry> GetSnapshot() const;

    private:
        void WriteLog(std::string_view name, LogLevel level, std::string_view message,
                      int line, std::string_view file);
        void FlushToFile();
        bool CreateLogFile();
        void Reset();

    private:
        mutable std::mutex log_mutex;

        std::vector<LogEntry> logs_;
        size_t last_flushed_index_{};
        std::ofstream file_{};
        std::chrono::steady_clock::time_point last_flush_time_;
        const float flush_interval;
        const size_t flush_size_threshold;
        const size_t max_buf_size;
        const size_t max_log_file_size;

        bool request_immediate_flush;
    };

    template <typename... Args>
        requires(sizeof...(Args) > 0)
    void Logger::Log(std::string_view log_name, LogLevel level, int line,
                     std::string_view file, const char *format, Args &&...args)
    {
        std::array<char, 512> small_buffer{};
        const int size = std::snprintf(small_buffer.data(), small_buffer.size(),
                                       format, std::forward<Args>(args)...);
        if (size < 0)
        {
            Log(log_name, level, line, file, "<printf formatting failed>");
            return;
        }

        if (static_cast<size_t>(size) < small_buffer.size())
        {
            WriteLog(log_name, level,
                     std::string_view(small_buffer.data(), static_cast<size_t>(size)), line, file);
            return;
        }

        std::vector<char> buffer(static_cast<size_t>(size) + 1);
        std::snprintf(buffer.data(), buffer.size(), format, std::forward<Args>(args)...);
        WriteLog(log_name, level,
                 std::string_view(buffer.data(), static_cast<size_t>(size)), line, file);
    }

    template <typename... Args>
    void Logger::LogFormat(std::string_view log_name, LogLevel level, int line,
                           std::string_view file, std::string_view format,
                           Args &&...args)
    {
        try
        {
            WriteLog(log_name, level,
                     std::vformat(format, std::make_format_args(args...)), line, file);
        }
        catch (const std::format_error &)
        {
            // Logging must not turn a malformed diagnostic into an engine failure.
            Log(log_name, level, line, file, std::string_view(format));
        }
    }
}

#endif
