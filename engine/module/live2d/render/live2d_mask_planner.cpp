#include "live2d_mask_planner.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace kpengine::live2d
{
    namespace
    {
        constexpr float kBoundsMargin = 0.05f;
        constexpr std::uint32_t kRegionSubdivision = 3u;

        struct Bounds final
        {
            Live2DVector2 min{
                std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max()};
            Live2DVector2 max{
                std::numeric_limits<float>::lowest(),
                std::numeric_limits<float>::lowest()};
            bool has_value = false;
        };

        struct WorkingContext final
        {
            std::uint32_t source_context_index = 0u;
            std::vector<std::uint32_t> source_drawable_indices;
            std::vector<std::uint32_t> consumer_drawable_indices;
        };

        bool IsFinite(const float value) noexcept
        {
            return std::isfinite(value) != 0;
        }

        bool IsFinite(const Live2DVector2 value) noexcept
        {
            return IsFinite(value.x) && IsFinite(value.y);
        }

        void Include(Bounds &bounds, const Live2DVector2 position) noexcept
        {
            bounds.min.x = std::min(bounds.min.x, position.x);
            bounds.min.y = std::min(bounds.min.y, position.y);
            bounds.max.x = std::max(bounds.max.x, position.x);
            bounds.max.y = std::max(bounds.max.y, position.y);
            bounds.has_value = true;
        }

        bool SameSources(const std::vector<std::uint32_t> &left,
                         const std::vector<std::uint32_t> &right) noexcept
        {
            return left == right;
        }

        Live2DMaskAtlasPlanResult Fail(Live2DMaskAtlasPlanResult &result,
                                       std::string diagnostic)
        {
            result.diagnostic = std::move(diagnostic);
            return result;
        }

        std::array<float, 16> MakeIdentity() noexcept
        {
            return {1.0f, 0.0f, 0.0f, 0.0f,
                    0.0f, 1.0f, 0.0f, 0.0f,
                    0.0f, 0.0f, 1.0f, 0.0f,
                    0.0f, 0.0f, 0.0f, 1.0f};
        }

        Live2DMaskAtlasRegion MakeRegion(const std::size_t slot,
                                         const Bounds &bounds)
        {
            const std::uint32_t channel = static_cast<std::uint32_t>(
                slot / kLive2DMaskAtlasMaxRegionsPerChannel);
            const std::uint32_t region = static_cast<std::uint32_t>(
                slot % kLive2DMaskAtlasMaxRegionsPerChannel);
            const std::uint32_t column = region % kRegionSubdivision;
            const std::uint32_t row = region / kRegionSubdivision;
            const std::uint32_t x0 =
                (kLive2DMaskAtlasWidth * column) / kRegionSubdivision;
            const std::uint32_t x1 =
                (kLive2DMaskAtlasWidth * (column + 1u)) / kRegionSubdivision;
            const std::uint32_t y0 =
                (kLive2DMaskAtlasHeight * row) / kRegionSubdivision;
            const std::uint32_t y1 =
                (kLive2DMaskAtlasHeight * (row + 1u)) / kRegionSubdivision;

            const float extent_x = bounds.max.x - bounds.min.x;
            const float extent_y = bounds.max.y - bounds.min.y;
            const float scale_u =
                static_cast<float>(x1 - x0) /
                static_cast<float>(kLive2DMaskAtlasWidth) / extent_x;
            const float scale_v =
                static_cast<float>(y1 - y0) /
                static_cast<float>(kLive2DMaskAtlasHeight) / extent_y;
            const float translate_u =
                static_cast<float>(x0) /
                    static_cast<float>(kLive2DMaskAtlasWidth) -
                bounds.min.x * scale_u;
            const float translate_v =
                static_cast<float>(y0) /
                    static_cast<float>(kLive2DMaskAtlasHeight) -
                bounds.min.y * scale_v;

            Live2DMaskAtlasRegion output{};
            output.channel = channel;
            output.region = region;
            output.x = x0;
            output.y = y0;
            output.width = x1 - x0;
            output.height = y1 - y0;
            output.bounds_min = bounds.min;
            output.bounds_max = bounds.max;
            output.model_to_atlas_sample = MakeIdentity();
            output.model_to_atlas_sample[0] = scale_u;
            output.model_to_atlas_sample[5] = scale_v;
            output.model_to_atlas_sample[12] = translate_u;
            output.model_to_atlas_sample[13] = translate_v;

            output.model_to_mask = MakeIdentity();
            output.model_to_mask[0] = 2.0f * scale_u;
            output.model_to_mask[5] = -2.0f * scale_v;
            output.model_to_mask[12] = 2.0f * translate_u - 1.0f;
            output.model_to_mask[13] = 1.0f - 2.0f * translate_v;
            return output;
        }
    }

    Live2DMaskAtlasPlanResult Live2DMaskAtlasPlanner::Plan(
        const Live2DStaticModelData &static_data,
        const Live2DFrameSnapshot &snapshot)
    {
        Live2DMaskAtlasPlanResult result{};
        if (!ValidateLive2DStaticModelData(static_data, result.diagnostic) ||
            !ValidateLive2DFrameSnapshot(static_data, snapshot,
                                         result.diagnostic))
        {
            return result;
        }

        std::vector<WorkingContext> working;
        result.plan.drawable_context_indices.assign(
            static_data.drawables.size(), kLive2DNoMaskContext);

        for (std::uint32_t drawable_index = 0u;
             drawable_index < static_data.drawables.size(); ++drawable_index)
        {
            const Live2DDrawableStatic &drawable =
                static_data.drawables[drawable_index];
            const Live2DDrawableState &state = snapshot.drawables[drawable_index];
            if (drawable.mask_context_index == kLive2DNoMaskContext)
            {
                continue;
            }
            if (drawable.mask_source_drawable_indices.empty())
            {
                return Fail(result,
                            "Live2D mask context has no source drawables at context " +
                                std::to_string(drawable.mask_context_index));
            }
            if (!state.visible || state.opacity <= 0.0f)
            {
                continue;
            }

            const auto context_it = std::find_if(
                working.begin(), working.end(), [&drawable](const WorkingContext &context) {
                    return SameSources(context.source_drawable_indices,
                                       drawable.mask_source_drawable_indices);
                });
            std::size_t context_index = 0u;
            if (context_it == working.end())
            {
                context_index = working.size();
                working.push_back({drawable.mask_context_index,
                                   drawable.mask_source_drawable_indices, {}});
            }
            else
            {
                context_index = static_cast<std::size_t>(
                    std::distance(working.begin(), context_it));
                working[context_index].source_context_index = std::min(
                    working[context_index].source_context_index,
                    drawable.mask_context_index);
            }
            working[context_index].consumer_drawable_indices.push_back(
                drawable_index);
        }

        std::stable_sort(
            working.begin(), working.end(),
            [](const WorkingContext &left, const WorkingContext &right) {
                return left.source_context_index < right.source_context_index;
            });

        if (working.size() > kLive2DMaxActiveMaskContexts)
        {
            return Fail(result,
                        "Live2D active mask context count exceeds atlas capacity: " +
                            std::to_string(working.size()));
        }

        result.plan.contexts.reserve(working.size());
        for (std::size_t planned_index = 0u; planned_index < working.size();
             ++planned_index)
        {
            const WorkingContext &working_context = working[planned_index];
            Bounds bounds{};
            for (const std::uint32_t consumer_index :
                 working_context.consumer_drawable_indices)
            {
                const Live2DDrawableStatic &consumer =
                    static_data.drawables[consumer_index];
                if (consumer.vertex_count == 0u)
                {
                    return Fail(result,
                                "Live2D mask context has an empty consumer drawable: " +
                                    std::to_string(consumer_index));
                }
                for (std::uint32_t vertex = 0u; vertex < consumer.vertex_count;
                     ++vertex)
                {
                    Include(bounds, snapshot.positions[consumer.vertex_offset + vertex]);
                }
            }

            for (const std::uint32_t source_index :
                 working_context.source_drawable_indices)
            {
                const Live2DDrawableStatic &source =
                    static_data.drawables[source_index];
                if (source.vertex_count == 0u || source.index_count == 0u)
                {
                    return Fail(result,
                                "Live2D mask context has empty source geometry at drawable " +
                                    std::to_string(source_index));
                }
            }
            if (!bounds.has_value || !IsFinite(bounds.min) ||
                !IsFinite(bounds.max))
            {
                return Fail(result,
                            "Live2D mask context has non-finite bounds at context " +
                                std::to_string(working_context.source_context_index));
            }

            const float extent_x = bounds.max.x - bounds.min.x;
            const float extent_y = bounds.max.y - bounds.min.y;
            if (!IsFinite(extent_x) || !IsFinite(extent_y) || extent_x <= 0.0f ||
                extent_y <= 0.0f)
            {
                return Fail(result,
                            "Live2D mask context has zero-area bounds at context " +
                                std::to_string(working_context.source_context_index));
            }

            const float margin_x = extent_x * kBoundsMargin;
            const float margin_y = extent_y * kBoundsMargin;
            bounds.min.x -= margin_x;
            bounds.min.y -= margin_y;
            bounds.max.x += margin_x;
            bounds.max.y += margin_y;

            Live2DMaskAtlasContext context{};
            context.source_context_index = working_context.source_context_index;
            context.source_drawable_indices = working_context.source_drawable_indices;
            context.consumer_drawable_indices =
                working_context.consumer_drawable_indices;
            context.region = MakeRegion(planned_index, bounds);
            result.plan.contexts.push_back(std::move(context));
        }

        for (std::size_t planned_index = 0u;
             planned_index < result.plan.contexts.size(); ++planned_index)
        {
            for (const std::uint32_t drawable_index :
                 result.plan.contexts[planned_index].consumer_drawable_indices)
            {
                result.plan.drawable_context_indices[drawable_index] =
                    static_cast<std::uint32_t>(planned_index);
            }
        }

        result.succeeded = true;
        return result;
    }
}
