#ifndef KPENGINE_LIVE2D_MODEL_DATA_H
#define KPENGINE_LIVE2D_MODEL_DATA_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "../render/live2d_render_contract.h"

namespace kpengine::live2d
{
    inline constexpr std::uint32_t kLive2DNoMaskContext =
        static_cast<std::uint32_t>(-1);

    struct Live2DVector2 final
    {
        float x = 0.0f;
        float y = 0.0f;
    };

    struct Live2DCanvasInfo final
    {
        Live2DVector2 size_in_pixels{};
        Live2DVector2 origin_in_pixels{};
        float pixels_per_unit = 0.0f;
    };

    struct Live2DDrawableStatic final
    {
        std::uint32_t vertex_offset = 0u;
        std::uint32_t vertex_count = 0u;
        std::uint32_t first_index = 0u;
        std::uint32_t index_count = 0u;
        std::uint32_t texture_index = 0u;
        std::uint32_t mask_context_index = kLive2DNoMaskContext;
        std::vector<std::uint32_t> mask_source_drawable_indices;
    };

    struct Live2DMaskContext final
    {
        std::vector<std::uint32_t> source_drawable_indices;
    };

    struct Live2DStaticModelData final
    {
        std::uint64_t topology_revision = 0u;
        Live2DCanvasInfo canvas{};
        std::vector<Live2DVector2> uvs;
        std::vector<std::uint16_t> indices;
        std::vector<Live2DDrawableStatic> drawables;
        std::vector<Live2DMaskContext> mask_contexts;
        std::size_t maximum_position_bytes = 0u;
        std::uint32_t texture_count = 0u;
        Live2DRenderFeatureReport feature_report{};
    };

    struct Live2DDrawableState final
    {
        std::uint32_t constant_flags = 0u;
        std::uint32_t dynamic_flags = 0u;
        std::int32_t render_order = 0;
        float opacity = 0.0f;
        bool visible = false;
        bool culling = false;
        bool inverted_mask = false;
        Live2DBlendMode blend_mode = Live2DBlendMode::Normal;
        Live2DColor multiply_color{1.0f, 1.0f, 1.0f, 1.0f};
        Live2DColor screen_color{};
    };

    struct Live2DFrameSnapshot final
    {
        std::uint64_t topology_revision = 0u;
        std::uint64_t frame_sequence = 0u;
        std::vector<Live2DVector2> positions;
        std::vector<Live2DDrawableState> drawables;
    };

    bool ValidateLive2DStaticModelData(const Live2DStaticModelData &data,
                                       std::string &diagnostic);

    bool ValidateLive2DFrameSnapshot(const Live2DStaticModelData &static_data,
                                     const Live2DFrameSnapshot &snapshot,
                                     std::string &diagnostic);
}

#endif
