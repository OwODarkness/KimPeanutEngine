#include "logger.h"
#include <iostream>
#include <ctime>
#include <filesystem>
#include <magic_enum/magic_enum.hpp>
#include "runtime/core/system/log_system.h"
#include "platform/path/path.h"

namespace kpengine::program
{
    // static member variables initialize
    Logger *Logger::log_single_instance_ = nullptr;
    std::mutex Logger::log_mutex;

    Logger::Logger() : last_flushed_index_(0), flush_interval(2.f), flush_size_threshold(200),
                       max_buf_size(1000),
                       max_log_file_size(5 << 20), request_immediate_flush(false)
    {
    }

    Logger *Logger::GetLogger()
    {

        if (log_single_instance_ == nullptr)
        {
            log_mutex.lock();
            log_single_instance_ = new Logger();
            log_mutex.unlock();
        }
        return log_single_instance_;
    }

    void Logger::ExtractTipColorFromLogLevel(float (&color)[4], LogLevel level)
    {
        switch (level)
        {
        case LOG_LEVEL_DISPLAY:
            color[0] = 1.f;
            color[1] = 1.f;
            color[2] = 1.f;
            color[3] = 1.f;
            break;
        case LOG_LEVEL_WARNNING:
            color[0] = 1.f;
            color[1] = 1.f;
            color[2] = 0.f;
            color[3] = 1.f;
            break;
        case LOG_LEVEL_ERROR:
            color[0] = 0.5f;
            color[1] = 0.2f;
            color[2] = 0.f;
            color[3] = 1.f;
        case LOG_LEVEL_FATAL:
            color[0] = 1.f;
            color[1] = 0.f;
            color[2] = 0.f;
            color[3] = 1.f;
            break;
        default:
            break;
        }
    }

    void Logger::Tick()
    {
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        float interval = std::chrono::duration<float>(now - last_flush_time_).count();
        if (interval >= flush_interval)
        {
            FlushToFile();
            last_flush_time_ = now;
        }

        size_t remain_log_num = logs_.size() - last_flushed_index_;
        if (remain_log_num >= flush_size_threshold)
        {
            FlushToFile();
        }
    }

    bool Logger::CreateLogFile()
    {

        std::filesystem::path log_dir = kpengine::GetLogDirectory();

        // log root direcotry
        if (!std::filesystem::exists(log_dir))
        {
            if (!std::filesystem::create_directory(log_dir))
            {
                std::cerr << "Failed to create directory: " << log_dir << std::endl;
                return false;
            }
        }

        // sub directory
        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm tm;
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif

        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%d");
        std::string date_dir_name = oss.str();

        std::filesystem::path full_path = log_dir / date_dir_name;

        if (!std::filesystem::exists(full_path))
        {
            if (std::filesystem::create_directory(full_path))
            {
                std::cout << "Create directory: " << full_path << std::endl;
            }
            else
            {
                std::cerr << "Failed to create directory: " << log_dir << std::endl;
                return false;
            }
        }

        // file
        std::ostringstream oss1;

        oss1 << std::put_time(&tm, "%Y.%m.%d-%H.%M.%S");
        std::string file_create_time_name = oss1.str();
        std::string file_name = "KimPeanutEngineLog-" + file_create_time_name + ".txt";
        std::filesystem::path log_file = full_path / file_name;
        file_ = std::ofstream(log_file, std::ios::out | std::ios::app);
        if (!file_.is_open())
        {
            std::cerr << "Failed to create log file: " << log_file << "\n";
            return false;
        }
        return true;
    }

    void Logger::Reset()
    {
        logs_.clear();
        last_flushed_index_ = 0;
    }

    void Logger::FlushToFile()
    {
        if (!file_ && !CreateLogFile())
            return;

        size_t count = 0;
        for (size_t i = last_flushed_index_; i < logs_.size(); ++i)
        {
            std::string s = FetchStringFromLog(logs_[i]);

            file_ << s << std::endl;

            // Debug: current put pointer in the stream
            if(file_.tellp() != -1)
            {
                count++;
            }

        }
        last_flushed_index_ += count; // update AFTER writing
    }

    void Logger::WriteLog(const std::string &name, LogLevel level, const std::string msg, int line, const std::string &file, std::chrono::system_clock::time_point timestamp)
    {
        std::time_t time = std::chrono::system_clock::to_time_t(timestamp);
        LogEntry log{.name = name, .level = level, .message = msg, .line = line, .file = file, .timestamp = timestamp};
        if (logs_.size() > max_buf_size)
        {
            FlushToFile();
            Reset();
        }

        std::string log_string = FetchStringFromLog(log);
        float msg_color[4];
        ExtractTipColorFromLogLevel(msg_color, level);
        runtime::global_runtime_context.log_system_->AddLog(log_string, msg_color);

#ifdef KPENGINE_DEBUG
        std::cout << log_string << std::endl;
#endif
        logs_.push_back(std::move(log));
    }

    std::string Logger::FetchStringFromLog(const LogEntry &log)
    {
        std::time_t time = std::chrono::system_clock::to_time_t(log.timestamp);
        std::tm local_time;

        errno_t err = localtime_s(&local_time, &time);
        std::stringstream oss;
        oss << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S");
        std::string log_string =
            "[" + oss.str() + "] " +
            std::string(log.name) +
            ": " + "[" +
            std::string(magic_enum::enum_name(log.level)) + "] " + log.message;
        if (log.level == LogLevel::Warning || log.level == LogLevel::Error || log.level == LogLevel::Fatal)
        {
            log_string = log_string + " from file: " + log.file + " line: " + std::to_string(log.line);
        }
        return log_string;
    }

    Logger::~Logger()
    {
        if (file_ && file_.is_open())
        {
            file_.close();
        }
    }
}