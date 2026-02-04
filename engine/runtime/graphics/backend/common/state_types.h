#ifndef KPENGINE_RUNTIME_GRAPHICS_STATE_TYPES_H
#define KPENGINE_RUNTIME_GRAPHICS_STATE_TYPES_H

#include "enum.h"

namespace kpengine::graphics
{
    struct RasterState
    {
        bool depth_clamp_enabled = false;
        bool rasterizer_discard_enabled = false;
        PolygonMode polygon_mode = PolygonMode::POLYGON_MODE_FILL;
        CullMode cull_mode = CullMode::CULL_MODE_BACK;
        FrontFace front_face = FrontFace::FRONT_FACE_CLOCKWISE;
        bool depth_bias_enabled = false;
        float depth_bias_constant = 0.f;
        float depth_bias_slope = 0.f;
        float line_width = 1.f;
    };

    struct MultisampleState
    {
        uint32_t rasterization_samples = 1;
        bool sample_shading_enabled = false;
        float min_sample_shading = 1.0f;
        // uint32_t sampleMask = 0xFFFFFFFF;
        bool alpha_to_coverage_enable = false;
        bool alpha_to_one_enable = false;
    };

    struct BlendAttachmentState
    {
        bool blend_enabled = false;
        BlendFactor src_color_blend_factor = BlendFactor::BLEND_FACTOR_ONE;
        BlendFactor dst_color_blend_factor = BlendFactor::BLEND_FACTOR_ZERO;
        BlendOp color_blend_op = BlendOp::BLEND_OP_ADD;
        BlendFactor src_alpha_blend_factor = BlendFactor::BLEND_FACTOR_ONE;
        BlendFactor dst_alpha_blend_factor = BlendFactor::BLEND_FACTOR_ZERO;
        BlendOp alpha_blend_op = BlendOp::BLEND_OP_ADD;
        uint8_t color_write_mask = 0xF;
    };
}

#endif