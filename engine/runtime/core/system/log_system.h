#ifndef KPENGINE_RUNTIME_LOG_SYSTEM_H
#define KPENGINE_RUNTIME_LOG_SYSTEM_H

#include <string>
#include <vector>


struct LogInfo {
    LogInfo(const float in_color[4], const std::string& in_log_text) {
        std::copy(in_color, in_color + 4, color);
        log_text = in_log_text;
    }
    float color[4];
    std::string log_text;
};

namespace kpengine {

    class LogSystem {
    public:
        void AddLog(const std::string& msg, const float msg_color[4]);
        
        inline const std::vector<LogInfo>& GetLogs() const { return logs_; }
        
    private:
        std::vector<LogInfo> logs_;
    };
}

#endif