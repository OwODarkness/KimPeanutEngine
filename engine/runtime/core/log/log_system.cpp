#include "log_system.h"
namespace kpengine
{
    std::vector<program::LogEntry> LogSystem::GetLogs() const
    {
        return program::Logger::GetLogger().Get();
    }

    std::vector<program::LogEntry> LogSystem::GetLogSnapshot() const
    {
        return program::Logger::GetLogger().GetSnapshot();
    }

    void LogSystem::Tick(float delta_time)
    {
        program::Logger::GetLogger().Tick();
    }


}
