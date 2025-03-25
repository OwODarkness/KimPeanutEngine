#include "logger.h"



namespace kpengine
{
    namespace program
    {
        //static member variables initialize
        Logger *Logger::log_single_instance_ = nullptr;
        std::mutex Logger::log_mutex;

        Logger::Logger() {}

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

    }
}