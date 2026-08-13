// min/max macros would poison <algorithm> elsewhere in the TU.
#define NOMINMAX
#include <windows.h>
#include <psapi.h>

#include "platform/win/windows_memory_stats_sampler.h"

namespace kpengine
{
    MemoryStats WindowsMemoryStatsSampler::Sample() const
    {
        MemoryStats stats;

        PROCESS_MEMORY_COUNTERS counters{};
        if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
        {
            stats.process_mb = static_cast<double>(counters.WorkingSetSize) / (1024.0 * 1024.0);
        }

        MEMORYSTATUSEX status{};
        status.dwLength = sizeof(status);
        if (GlobalMemoryStatusEx(&status))
        {
            stats.system_total_mb = static_cast<double>(status.ullTotalPhys) / (1024.0 * 1024.0);
            stats.system_available_mb = static_cast<double>(status.ullAvailPhys) / (1024.0 * 1024.0);
        }
        return stats;
    }
}
