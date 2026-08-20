#ifndef KPENGINE_RUNTIME_RENDER_RESOURCE_H
#define KPENGINE_RUNTIME_RENDER_RESOURCE_H

#include <variant>

#include "graphics/backend/common/api.h"

namespace kpengine::render
{
    struct TextureBinding
    {
        graphics::TextureHandle texture;
        graphics::SamplerHandle sampler;
    };

    using RenderResource = std::variant<std::monostate, graphics::PipelineHandle,
                                        graphics::MeshHandle, TextureBinding>;
}

#endif
