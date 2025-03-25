#ifndef KPENGINE_RUNTIME_LOGGER_H
#define KPENGINE_RUNTIME_LOGGER_H

#include <string>
#include <mutex>
#include <format>
#include <iostream>
#include <chrono>
#include <iomanip>
#include <ctime>
#include <vector>
#include <cstdio>
#include <magic_enum/magic_enum.hpp>

#include "runtime/runtime_global_context.h"
#include "runtime/core/system/log_system.h"

#define KP_LOG(LOG_NAME, LEVEL, MESSAGE, ...) \
    kpengine::program::Logger::GetLogger()->Log(LOG_NAME, LEVEL, MESSAGE, ##__VA_ARGS__)

#define LOG_LEVEL_DISPLAY kpengine::program::LogLevel::DISPLAY
#define LOG_LEVEL_WARNNING kpengine::program::LogLevel::WARNNING
#define LOG_LEVEL_ERROR kpengine::program::LogLevel::ERROR
#define LOG_LEVEL_FATAL kpengine::program::LogLevel::FATAL

namespace kpengine
{
    namespace program
    {

        enum class LogLevel : uint8_t
        {
            DISPLAY = 0,
            WARNNING,
            ERROR,
            FATAL
        };

        class Logger
        {
        private:
            Logger();

        public:
            static Logger *GetLogger();
            void ExtractTipColorFromLogLevel(float (&color)[4],  LogLevel level);
            template <typename... Args>
            void Log(std::string_view log_name, LogLevel level, const std::string &msg, Args &&...args)
            {
                std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
                std::time_t time = std::chrono::system_clock::to_time_t(now);

                std::tm local_time;

                errno_t err = localtime_s(&local_time, &time);
                if (err == 0)
                {
                    int size = std::snprintf(nullptr, 0, msg.c_str(), std::forward<Args>(args)...) + 1;
                    std::vector<char> buffer(size);
                     std::snprintf(buffer.data(), buffer.size(), msg.c_str(), std::forward<Args>(args)...);
                    std::string message =  std::string(buffer.data(), buffer.size() - 1);

                    std::stringstream ss ;
                    ss<<std::put_time(&local_time, "%Y-%m-%d %H:%M:%S");
                    std::string s = "[" + ss.str() + "] " + std::string(log_name) + ": " +
                     "[" + std::string(magic_enum::enum_name(level)) +"] " + message;
                    float msg_color[4];
                    ExtractTipColorFromLogLevel(msg_color, level);
                    runtime::global_runtime_context.log_system_->AddLog(s, msg_color);
#ifdef KPENGINE_DEBUG
                    std::cout << s <<  std::endl;
#endif
                }
            }
        private:
            static Logger *log_single_instance_;
            static std::mutex log_mutex;
        };
    }
}

#endif