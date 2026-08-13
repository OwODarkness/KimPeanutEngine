#ifndef KPENGINE_RUNTIME_PLATFORM_MEMORY_STATS_SAMPLER_H
#define KPENGINE_RUNTIME_PLATFORM_MEMORY_STATS_SAMPLER_H

#include <memory>
#include "base/type.h"

namespace kpengine
{
    // Snapshot of process + system memory. Memory measurement is a platform concern
    // (an OS query), so it lives behind this seam — the engine is platform-agnostic
    // and the editor only displays what the sampler reports.
    struct MemoryStats
    {
        double process_mb = 0.0;
        double system_total_mb = 0.0;
        double system_available_mb = 0.0;
    };

    // Platform seam for memory measurement, mirroring WindowSystem: an interface, a
    // per-platform impl under platform/<name>/, and a factory chosen by PlatformType.
    // RuntimeContext owns the instance; the editor reaches it through EditorContext.
    class MemoryStatsSampler
    {
    public:
        virtual ~MemoryStatsSampler() = default;
        virtual MemoryStats Sample() const = 0;

        static std::unique_ptr<MemoryStatsSampler> CreateMemoryStatsSampler(PlatformType platform_type);
    };
}

#endif // KPENGINE_RUNTIME_PLATFORM_MEMORY_STATS_SAMPLER_H
