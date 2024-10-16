#include "logger.h"

#include <iostream>
#include <chrono>
#include <iomanip>
#include <ctime>

#include <magic_enum/magic_enum.hpp>

namespace kpengine
{
    namespace program
    {

        Logger *Logger::log_single_instance_ = nullptr;

        Logger::Logger() {}

        Logger *Logger::GetLogger()
        {
            if (log_single_instance_ == nullptr)
            {
                log_single_instance_ = new Logger();
            }
            return log_single_instance_;
        }

        void Logger::Log(std::string_view log_name, LogLevel level, std::string_view msg)
        {
            std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
            std::time_t time = std::chrono::system_clock::to_time_t(now);

            std::tm local_time;

            errno_t err = localtime_s(&local_time, &time);

            if (err == 0)
            {
                std::cout << log_name << ": " << "[" << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S") << "] ["   << magic_enum::enum_name(level).data() << "] "  << msg << "\n";
            }
        }

    }
}