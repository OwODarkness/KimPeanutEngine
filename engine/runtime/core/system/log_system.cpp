#include "log_system.h"
#include <iostream>
#include "runtime/core/log/logger.h"
namespace kpengine
{
    void LogSystem::AddLog(const std::string &msg, const float msg_color[4])
    {
        logs_.emplace_back(msg_color, msg);
    }

    void LogSystem::Tick(float delta_time)
    {
        program::Logger::GetLogger()->Tick();
    }

}