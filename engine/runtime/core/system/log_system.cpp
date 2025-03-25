#include "log_system.h"

namespace kpengine{
    void LogSystem::AddLog(const std::string& msg, const float msg_color[4]) {
        logs_.emplace_back(msg_color, msg);
    }
    
}