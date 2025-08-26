#include "log_system.h"
namespace kpengine
{
    const std::vector<program::LogEntry>& LogSystem::GetLogs() const
    {
        return program::Logger::GetLogger().Get();
    }

    void LogSystem::Tick(float delta_time)
    {
        program::Logger::GetLogger().Tick();
    }


}