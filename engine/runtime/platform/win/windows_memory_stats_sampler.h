#ifndef KPENGINE_RUNTIME_PLATFORM_WINDOWS_MEMORY_STATS_SAMPLER_H
#define KPENGINE_RUNTIME_PLATFORM_WINDOWS_MEMORY_STATS_SAMPLER_H

#include "platform/memory_stats_sampler.h"

namespace kpengine
{
    // Win32 implementation: working set via PSAPI, system totals via GlobalMemoryStatusEx.
    class WindowsMemoryStatsSampler : public MemoryStatsSampler
    {
    public:
        MemoryStats Sample() const override;
    };
}

#endif // KPENGINE_RUNTIME_PLATFORM_WINDOWS_MEMORY_STATS_SAMPLER_H
