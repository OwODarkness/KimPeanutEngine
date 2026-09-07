#ifndef KPENGINE_RUNTIME_GRAPHICS_PROFILE_COUNTERS_H
#define KPENGINE_RUNTIME_GRAPHICS_PROFILE_COUNTERS_H

#include <cstdint>

namespace kpengine::graphics
{
    struct CommandRecorderProfileCounters
    {
        uint64_t pipeline_bind_requests = 0;
        uint64_t pipeline_bind_emitted = 0;
        uint64_t mesh_bind_requests = 0;
        uint64_t mesh_bind_emitted = 0;
        uint64_t resource_binding_bind_requests = 0;
        uint64_t resource_binding_bind_emitted = 0;
        uint64_t draw_calls_emitted = 0;
        double pipeline_validation_cpu_ms = 0.0;
        uint64_t pipeline_validation_calls = 0;
    };

    struct DescriptorProfileCounters
    {
        uint64_t searches = 0;
        uint64_t allocations = 0;
        uint64_t updates = 0;
        double search_cpu_ms = 0.0;
        double allocation_cpu_ms = 0.0;
        double update_cpu_ms = 0.0;
    };
}

#endif
