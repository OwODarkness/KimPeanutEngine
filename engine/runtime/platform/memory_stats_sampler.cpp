#include "platform/memory_stats_sampler.h"
#include "platform/win/windows_memory_stats_sampler.h"

namespace kpengine
{
    std::unique_ptr<MemoryStatsSampler> MemoryStatsSampler::CreateMemoryStatsSampler(PlatformType platform_type)
    {
        switch (platform_type)
        {
        case PlatformType::PLATFORM_WINDOWS:
            return std::make_unique<WindowsMemoryStatsSampler>();
            break;
        default:
            return nullptr;
            break;
        }
        return nullptr;
    }
}
