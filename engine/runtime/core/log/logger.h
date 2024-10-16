#ifndef KPENGINE_PROGRAM_LOGGER_H
#define KPENGINE_PROGRAM_LOGGER_H


#define KP_LOG(LOG_NAME, LEVEL, MESSAGE) kpengine::program::Logger::GetLogger()->Log(LOG_NAME, LEVEL, MESSAGE)

#define LOG_LEVEL_DISPLAY kpengine::program::LogLevel::DISPLAY
#define LOG_LEVEL_WARNNING kpengine::program::LogLevel::WARNNING
#define LOG_LEVEL_ERROR kpengine::program::LogLevel::ERROR
#define LOG_LEVEL_FATAL kpengine::program::LogLevel::FATAL


#include <string>

namespace kpengine{
    namespace program{

    enum class LogLevel{
        DISPLAY = 0,
        WARNNING,
        ERROR,
        FATAL
    };

    class Logger{
    private:
        Logger();                                       

    public:
        static Logger* GetLogger();    
        void Log(std::string_view log_name, LogLevel level, std::string_view msg);

    private:
        static Logger* log_single_instance_;

    };
}
}



#endif