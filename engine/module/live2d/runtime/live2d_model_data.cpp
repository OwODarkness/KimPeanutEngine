#include "live2d_model_data.h"

#include <cmath>
#include <limits>
#include <utility>

namespace kpengine::live2d
{
    namespace
    {
        bool Fail(std::string &diagnostic, std::string message)
        {
            diagnostic = std::move(message);
            return false;
        }

        bool IsFinite(const float value) noexcept
        {
            return std::isfinite(value) != 0;
        }

        bool IsFinite(const Live2DColor &color) noexcept
        {
            return IsFinite(color.r) && IsFinite(color.g) &&
                   IsFinite(color.b) && IsFinite(color.a);
        }

        bool IsValidBlendMode(const Live2DBlendMode mode) noexcept
        {
            return mode == Live2DBlendMode::Normal ||
                   mode == Live2DBlendMode::Additive ||
                   mode == Live2DBlendMode::Multiplicative;
        }

        bool AddOverflow(const std::size_t first, const std::size_t second,
                         std::size_t &result) noexcept
        {
            if (second > std::numeric_limits<std::size_t>::max() - first)
            {
                return true;
            }
            result = first + second;
            return false;
        }
    }

    bool ValidateLive2DStaticModelData(const Live2DStaticModelData &data,
                                       std::string &diagnostic)
    {
        diagnostic.clear();
        if (data.topology_revision == 0u)
        {
            return Fail(diagnostic, "Live2D static data has no topology revision");
        }
        if (!IsFinite(data.canvas.size_in_pixels.x) ||
            !IsFinite(data.canvas.size_in_pixels.y) ||
            !IsFinite(data.canvas.origin_in_pixels.x) ||
            !IsFinite(data.canvas.origin_in_pixels.y) ||
            !IsFinite(data.canvas.pixels_per_unit) ||
            data.canvas.size_in_pixels.x <= 0.0f ||
            data.canvas.size_in_pixels.y <= 0.0f ||
            data.canvas.pixels_per_unit <= 0.0f)
        {
            return Fail(diagnostic, "Live2D canvas information is invalid");
        }
        if (data.feature_report.drawable_count != data.drawables.size())
        {
            return Fail(diagnostic,
                        "Live2D feature report drawable count does not match static data");
        }

        std::size_t expected_vertex_count = 0u;
        std::size_t expected_index_count = 0u;
        for (std::size_t drawable_index = 0u;
             drawable_index < data.drawables.size(); ++drawable_index)
        {
            const Live2DDrawableStatic &drawable =
                data.drawables[drawable_index];
            if (AddOverflow(expected_vertex_count, drawable.vertex_count,
                            expected_vertex_count) ||
                AddOverflow(expected_index_count, drawable.index_count,
                            expected_index_count))
            {
                return Fail(diagnostic,
                            "Live2D static drawable range size overflow");
            }
            const std::size_t expected_vertex_offset =
                expected_vertex_count - drawable.vertex_count;
            const std::size_t expected_first_index =
                expected_index_count - drawable.index_count;
            if (drawable.vertex_offset != expected_vertex_offset ||
                drawable.first_index != expected_first_index)
            {
                return Fail(diagnostic,
                            "Live2D drawable range offset is inconsistent at drawable " +
                                std::to_string(drawable_index));
            }
            if (drawable.texture_index >= data.texture_count)
            {
                return Fail(diagnostic,
                            "Live2D drawable texture index is out of range at drawable " +
                                std::to_string(drawable_index));
            }
            if (drawable.mask_context_index != kLive2DNoMaskContext &&
                drawable.mask_context_index >= data.mask_contexts.size())
            {
                return Fail(diagnostic,
                            "Live2D mask context index is out of range at drawable " +
                                std::to_string(drawable_index));
            }
            if (drawable.mask_context_index != kLive2DNoMaskContext &&
                data.mask_contexts[drawable.mask_context_index]
                        .source_drawable_indices !=
                    drawable.mask_source_drawable_indices)
            {
                return Fail(diagnostic,
                            "Live2D drawable mask sources do not match its mask context");
            }
            for (const std::uint32_t source :
                 drawable.mask_source_drawable_indices)
            {
                if (source >= data.drawables.size())
                {
                    return Fail(diagnostic,
                                "Live2D mask source drawable index is out of range");
                }
            }
        }

        if (expected_vertex_count != data.uvs.size() ||
            expected_index_count != data.indices.size())
        {
            return Fail(diagnostic,
                        "Live2D concatenated UV or index count does not match drawable ranges");
        }
        if (expected_vertex_count >
                std::numeric_limits<std::size_t>::max() /
                    sizeof(Live2DVector2) ||
            data.maximum_position_bytes !=
                expected_vertex_count * sizeof(Live2DVector2))
        {
            return Fail(diagnostic,
                        "Live2D maximum position byte count is inconsistent");
        }
        if (data.feature_report.active_mask_context_count !=
            data.mask_contexts.size())
        {
            return Fail(diagnostic,
                        "Live2D feature report mask context count does not match static data");
        }

        for (std::size_t vertex_index = 0u; vertex_index < data.uvs.size();
             ++vertex_index)
        {
            if (!IsFinite(data.uvs[vertex_index].x) ||
                !IsFinite(data.uvs[vertex_index].y))
            {
                return Fail(diagnostic,
                            "Live2D UV contains a non-finite value at vertex " +
                                std::to_string(vertex_index));
            }
        }

        for (std::size_t drawable_index = 0u;
             drawable_index < data.drawables.size(); ++drawable_index)
        {
            const Live2DDrawableStatic &drawable =
                data.drawables[drawable_index];
            for (std::size_t local_index = 0u;
                 local_index < drawable.index_count; ++local_index)
            {
                const std::size_t index =
                    static_cast<std::size_t>(drawable.first_index) + local_index;
                if (index >= data.indices.size() ||
                    data.indices[index] >= drawable.vertex_count)
                {
                    return Fail(diagnostic,
                                "Live2D local index is out of range at drawable " +
                                    std::to_string(drawable_index));
                }
            }
        }

        if (data.feature_report.offscreen_object_count != 0u)
        {
            return Fail(diagnostic,
                        "Live2D static data unsupported: offscreen object count is " +
                            std::to_string(data.feature_report.offscreen_object_count));
        }
        if (data.feature_report.blend_group_count != 0u)
        {
            return Fail(diagnostic,
                        "Live2D static data unsupported: blend group count is " +
                            std::to_string(data.feature_report.blend_group_count));
        }
        if (data.feature_report.invalid_index_count != 0u)
        {
            return Fail(diagnostic,
                        "Live2D static data invalid: index count is " +
                            std::to_string(data.feature_report.invalid_index_count));
        }
        if (data.feature_report.unknown_blend_mode_count != 0u)
        {
            return Fail(diagnostic,
                        "Live2D static data unsupported: unknown blend mode count is " +
                            std::to_string(data.feature_report.unknown_blend_mode_count));
        }
        if (data.feature_report.topology_changed)
        {
            return Fail(diagnostic,
                        "Live2D static data unsupported: topology changed");
        }
        if (data.feature_report.active_mask_context_count >
            kLive2DMaxActiveMaskContexts)
        {
            return Fail(diagnostic,
                        "Live2D static data unsupported: mask context count exceeds V1 capacity");
        }
        return true;
    }

    bool ValidateLive2DFrameSnapshot(const Live2DStaticModelData &static_data,
                                     const Live2DFrameSnapshot &snapshot,
                                     std::string &diagnostic)
    {
        diagnostic.clear();
        if (!ValidateLive2DStaticModelData(static_data, diagnostic))
        {
            return false;
        }
        if (snapshot.topology_revision != static_data.topology_revision)
        {
            return Fail(diagnostic,
                        "Live2D frame snapshot topology revision does not match static data");
        }
        if (snapshot.frame_sequence == 0u)
        {
            return Fail(diagnostic,
                        "Live2D frame snapshot has no frame sequence");
        }
        if (snapshot.positions.size() >
                std::numeric_limits<std::size_t>::max() /
                    sizeof(Live2DVector2) ||
            snapshot.positions.size() * sizeof(Live2DVector2) !=
                static_data.maximum_position_bytes)
        {
            return Fail(diagnostic,
                        "Live2D frame position count does not match static data");
        }
        if (snapshot.drawables.size() != static_data.drawables.size())
        {
            return Fail(diagnostic,
                        "Live2D frame drawable state count does not match static data");
        }
        for (std::size_t vertex_index = 0u;
             vertex_index < snapshot.positions.size(); ++vertex_index)
        {
            if (!IsFinite(snapshot.positions[vertex_index].x) ||
                !IsFinite(snapshot.positions[vertex_index].y))
            {
                return Fail(diagnostic,
                            "Live2D frame position contains a non-finite value at vertex " +
                                std::to_string(vertex_index));
            }
        }
        for (std::size_t drawable_index = 0u;
             drawable_index < snapshot.drawables.size(); ++drawable_index)
        {
            const Live2DDrawableState &state = snapshot.drawables[drawable_index];
            if (state.render_order < 0)
            {
                return Fail(diagnostic,
                            "Live2D drawable render order is negative at drawable " +
                                std::to_string(drawable_index));
            }
            if (!IsFinite(state.opacity))
            {
                return Fail(diagnostic,
                            "Live2D drawable opacity is non-finite at drawable " +
                                std::to_string(drawable_index));
            }
            if (!IsFinite(state.multiply_color))
            {
                return Fail(diagnostic,
                            "Live2D drawable multiply color is non-finite at drawable " +
                                std::to_string(drawable_index));
            }
            if (!IsFinite(state.screen_color))
            {
                return Fail(diagnostic,
                            "Live2D drawable screen color is non-finite at drawable " +
                                std::to_string(drawable_index));
            }
            if (!IsValidBlendMode(state.blend_mode))
            {
                return Fail(diagnostic,
                            "Live2D drawable blend mode is invalid at drawable " +
                                std::to_string(drawable_index));
            }
        }
        return true;
    }
}
