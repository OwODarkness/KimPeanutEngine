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


    }
}