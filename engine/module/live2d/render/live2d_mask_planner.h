#ifndef KPENGINE_LIVE2D_MASK_PLANNER_H
#define KPENGINE_LIVE2D_MASK_PLANNER_H

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "runtime/live2d_model_data.h"

namespace kpengine::live2d
{
    struct Live2DMaskAtlasRegion final
    {
        std::uint32_t channel = 0u;
        std::uint32_t region = 0u;
        std::uint32_t x = 0u;
        std::uint32_t y = 0u;
        std::uint32_t width = 0u;
        std::uint32_t height = 0u;
        Live2DVector2 bounds_min{};
        Live2DVector2 bounds_max{};

        // Matrices use column-vector, column-major-compatible layout. The
        // atlas sample matrix returns normalized top-left-origin UVs. The
        // mask matrix returns clip-space coordinates for the atlas pass.
        std::array<float, 16> model_to_mask{};
        std::array<float, 16> model_to_atlas_sample{};
    };

    struct Live2DMaskAtlasContext final
    {
        // This is the original static context index used for deterministic
        // assignment. Equal source sets are represented by one planned context.
        std::uint32_t source_context_index = 0u;
        std::vector<std::uint32_t> source_drawable_indices;
        std::vector<std::uint32_t> consumer_drawable_indices;
        Live2DMaskAtlasRegion region{};
    };

    struct Live2DMaskAtlasPlan final
    {
        std::vector<Live2DMaskAtlasContext> contexts;
        // Maps each drawable to a planned context, or kLive2DNoMaskContext.
        std::vector<std::uint32_t> drawable_context_indices;
    };

    struct Live2DMaskAtlasPlanResult final
    {
        bool succeeded = false;
        std::string diagnostic;
        Live2DMaskAtlasPlan plan;
    };

    class Live2DMaskAtlasPlanner final
    {
    public:
        static Live2DMaskAtlasPlanResult Plan(
            const Live2DStaticModelData &static_data,
            const Live2DFrameSnapshot &snapshot);
    };
}

#endif
