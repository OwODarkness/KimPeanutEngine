#ifndef KPENGINE_RUNTIME_RENDER_PIPELINE_CACHE_KEY_H
#define KPENGINE_RUNTIME_RENDER_PIPELINE_CACHE_KEY_H

#include <cstddef>
#include <cstdint>
#include <functional>

#include "base/type.h"

namespace kpengine::render
{
    // Render owns this identity: the RHI only sees the resulting PipelineDesc.
    struct PipelineCacheKey
    {
        uint64_t program_id = 0;
        GraphicsAPIType api = GraphicsAPIType::GRAPHICS_API_UNKNOW;
        uint64_t state_layout_signature = 0;

        bool operator==(const PipelineCacheKey &other) const noexcept
        {
            return program_id == other.program_id && api == other.api &&
                   state_layout_signature == other.state_layout_signature;
        }
    };

    struct PipelineCacheKeyHash
    {
        size_t operator()(const PipelineCacheKey &key) const noexcept
        {
            const size_t program_hash = std::hash<uint64_t>{}(key.program_id);
            const size_t api_hash = std::hash<uint32_t>{}(static_cast<uint32_t>(key.api));
            const size_t state_hash = std::hash<uint64_t>{}(key.state_layout_signature);
            return program_hash ^ (api_hash << 1) ^ (state_hash << 2);
        }
    };
}

#endif
