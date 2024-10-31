#ifndef KPENGINE_RUNTIME_LOGGER_H
#define KPENGINE_RUNTIME_LOGGER_H

#include <string>
#include <mutex>
#include <format>
#include <iostream>
#include <chrono>
#include <iomanip>
#include <ctime>
#include <cstdio>
#include <magic_enum/magic_enum.hpp>

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

            template <typename... Args>
            void Log(std::string_view log_name, LogLevel level, const std::string &msg, Args &&...args)
            {
                std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
                std::time_t time = std::chrono::system_clock::to_time_t(now);

                std::tm local_time;

                errno_t err = localtime_s(&local_time, &time);
                if (err == 0)
                {

                    std::cout 
                              << "[" << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S") << "] "
                              << log_name << ": "
                              << "[" << magic_enum::enum_name(level) << "] ";
                    std::printf(msg.c_str(), std::forward<Args>(args)...) ;
                    std::cout << std::endl;
                }
            }

        private:
            static Logger *log_single_instance_;
            static std::mutex log_mutex;
        };
    }
}

#endif