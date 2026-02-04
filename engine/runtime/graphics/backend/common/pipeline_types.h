#ifndef KPENGINE_RUNTIME_GRAPHICS_PIPELINE_TYPES_H
#define KPENGINE_RUNTIME_GRAPHICS_PIPELINE_TYPES_H

#include <vector>
#include <cstdint>
#include "api.h"
#include "enum.h"
#include "descriptor_types.h"
#include "state_types.h"
#include "buffer_types.h"

namespace kpengine::graphics
{

    // Cross Graphics API Pipeline DESC, used for pipeline create
    struct PipelineDesc
    {
        class Shader *vert_shader;
        class Shader *frag_shader;
        class Shader *geom_shader;
        std::vector<VertexBindingDesc> binding_descs;
        std::vector<VertexAttributionDesc> attri_descs;
        PrimitiveTopologyType primitive_topology_type = PrimitiveTopologyType::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        RasterState raster_state;
        MultisampleState multisample_state;
        BlendAttachmentState blend_attachment_state;
        std::vector<std::vector<DescriptorBindingDesc>> descriptor_binding_descs; // descs[i] means descs in set i
    };
}

#endif