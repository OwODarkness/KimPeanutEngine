#ifndef KPENGINE_RUNTIME_LOG_SYSTEM_H
#define KPENGINE_RUNTIME_LOG_SYSTEM_H

#include <string>
#include <vector>



namespace kpengine{

    class LogSystem{
    public:
        void AddLog(std::string msg);
        const std::vector<std::string>& GetLogs() const{return logs;}
    private:
        std::vector<std::string> logs;
    };
}

#endif