#ifndef KPENGINE_RUNTIME_GRAPHICS_PIPELINE_DESC_H
#define KPENGINE_RUNTIME_GRAPHICS_PIPELINE_DESC_H

#include <vector>
#include <cstdint>
#include "api.h"
#include "enum.h"

namespace kpengine::graphics{
    struct VertexBindingDesc{
        uint32_t binding;
        uint32_t stride;
        bool per_instance;
    };

    struct VertexAttributionDesc{
        uint32_t location;
        uint32_t binding;
        uint32_t offset;
        VertexFormat format;
    };

    struct PipelineDesc{
        ShaderHandle vert_shader_handle;
        ShaderHandle frag_shader_handle;
        ShaderHandle geom_shader_handle;
        std::vector<VertexBindingDesc> binding_descs;
        std::vector<VertexAttributionDesc> attri_descs;
    };
}


#endif