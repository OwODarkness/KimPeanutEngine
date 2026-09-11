#include "live2d_render_planner.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace kpengine::live2d
{
    namespace
    {
        constexpr std::uint32_t kConstantsBinding = 0u;
        constexpr std::uint32_t kTextureBinding = 1u;
        constexpr std::uint32_t kMaskAtlasBinding = 2u;

        bool IsFinite(const float value) noexcept
        {
            return std::isfinite(value) != 0;
        }

        bool IsFinite(const Live2DColor &color) noexcept
        {
            return IsFinite(color.r) && IsFinite(color.g) && IsFinite(color.b) &&
                   IsFinite(color.a);
        }

        bool IsFinite(const std::array<float, 16> &matrix) noexcept
        {
            return std::all_of(matrix.begin(), matrix.end(),
                               [](const float value) { return IsFinite(value); });
        }

        graphics::Viewport ResolveViewport(const Live2DRenderProxy &proxy,
                                           const Live2DRenderPlanOptions &options)
        {
            graphics::Viewport viewport = options.viewport;
            if (viewport.width <= 0.0f)
            {
                viewport.width = static_cast<float>(proxy.output_width);
            }
            if (viewport.height <= 0.0f)
            {
                viewport.height = static_cast<float>(proxy.output_height);
            }
            return viewport;
        }
    }

    graphics::PipelineHandle Live2DRenderResourceSet::PipelineFor(
        const Live2DBlendMode blend_mode, const bool culling) const noexcept
    {
        switch (blend_mode)
        {
        case Live2DBlendMode::Normal:
            return culling ? normal_culled : normal_unculled;
        case Live2DBlendMode::Additive:
            return culling ? additive_culled : additive_unculled;
        case Live2DBlendMode::Multiplicative:
            return culling ? multiplicative_culled : multiplicative_unculled;
        }
        return {};
    }

    graphics::PipelineHandle Live2DRenderResourceSet::MaskedPipelineFor(
        const Live2DBlendMode blend_mode, const bool culling) const noexcept
    {
        switch (blend_mode)
        {
        case Live2DBlendMode::Normal:
            return culling ? masked_normal_culled : masked_normal_unculled;
        case Live2DBlendMode::Additive:
            return culling ? masked_additive_culled : masked_additive_unculled;
        case Live2DBlendMode::Multiplicative:
            return culling ? masked_multiplicative_culled
                           : masked_multiplicative_unculled;
        }
        return {};
    }

    graphics::PipelineHandle Live2DRenderResourceSet::MaskPipelineFor(
        const bool culling) const noexcept
    {
        return culling ? mask_culled : mask_unculled;
    }

    Live2DRenderPlanResult Live2DRenderPlanner::Plan(
        const Live2DRenderResourceSet &resources,
        const Live2DRenderProxy &proxy,
        const Live2DStaticModelData &static_data,
        const Live2DFrameSnapshot &snapshot,
        const Live2DRenderPlanOptions &options)
    {
        Live2DRenderPlanResult result{};
        std::string diagnostic;
        if (!ValidateLive2DStaticModelData(static_data, diagnostic) ||
            !ValidateLive2DFrameSnapshot(static_data, snapshot, diagnostic))
        {
            result.diagnostic = diagnostic;
            return result;
        }
        if (!ValidateLive2DStaticModelData(proxy.static_data, diagnostic))
        {
            result.diagnostic = diagnostic;
            return result;
        }
        if (proxy.static_data.topology_revision != static_data.topology_revision)
        {
            result.diagnostic = "Live2D render proxy topology does not match the snapshot";
            return result;
        }
        if (!proxy.position_buffer.IsValid() || !proxy.uv_buffer.IsValid() ||
            !proxy.index_buffer.IsValid() ||
            (!proxy.output_target.IsValid() && !proxy.output_to_presentation) ||
            !resources.sampler.IsValid())
        {
            result.diagnostic = "Live2D render proxy has an invalid resource handle";
            return result;
        }
        if (proxy.textures.size() != static_data.texture_count)
        {
            result.diagnostic = "Live2D render proxy texture count does not match static data";
            return result;
        }
        for (const graphics::TextureHandle texture : proxy.textures)
        {
            if (!texture.IsValid())
            {
                result.diagnostic = "Live2D render proxy contains an invalid texture handle";
                return result;
            }
        }

        const Live2DMaskAtlasPlanResult mask_result =
            Live2DMaskAtlasPlanner::Plan(static_data, snapshot);
        if (!mask_result.succeeded)
        {
            result.diagnostic = mask_result.diagnostic;
            return result;
        }
        const bool has_active_masks = !mask_result.plan.contexts.empty();
        if (has_active_masks && (!proxy.mask_atlas_target.IsValid() ||
                                 !proxy.mask_atlas_texture.IsValid()))
        {
            result.diagnostic =
                "Live2D masked render proxy has no mask atlas target or texture";
            return result;
        }

        if (!IsFinite(options.model_transform))
        {
            result.diagnostic = "Live2D render transform contains a non-finite value";
            return result;
        }
        for (const Live2DDrawableState &state : snapshot.drawables)
        {
            if (!IsFinite(state.opacity) || !IsFinite(state.multiply_color) ||
                !IsFinite(state.screen_color))
            {
                result.diagnostic = "Live2D render drawable state contains a non-finite value";
                return result;
            }
        }

        const graphics::Viewport viewport = ResolveViewport(proxy, options);
        if (viewport.width <= 0.0f || viewport.height <= 0.0f ||
            !IsFinite(viewport.x) || !IsFinite(viewport.y) ||
            !IsFinite(viewport.width) || !IsFinite(viewport.height))
        {
            result.diagnostic = "Live2D render output viewport is invalid";
            return result;
        }

        std::vector<std::uint32_t> order(snapshot.drawables.size());
        for (std::uint32_t index = 0u; index < order.size(); ++index)
        {
            order[index] = index;
        }
        std::stable_sort(order.begin(), order.end(),
                         [&snapshot](const std::uint32_t left, const std::uint32_t right)
                         {
                             if (snapshot.drawables[left].render_order !=
                                 snapshot.drawables[right].render_order)
                             {
                                 return snapshot.drawables[left].render_order <
                                        snapshot.drawables[right].render_order;
                             }
                             return left < right;
                         });

        Live2DRenderSubmission submission{};
        submission.features = static_data.feature_report;
        submission.mask_plan = mask_result.plan;
        submission.topology_revision = snapshot.topology_revision;
        submission.frame_sequence = snapshot.frame_sequence;
        submission.counters.active_mask_context_count = static_cast<std::uint32_t>(
            mask_result.plan.contexts.size());
        submission.counters.position_upload_bytes =
            snapshot.positions.size() * sizeof(Live2DVector2);
        submission.work.buffer_writes.push_back({
            proxy.position_buffer, 0u,
            std::vector<std::byte>(submission.counters.position_upload_bytes)});
        std::memcpy(submission.work.buffer_writes.back().bytes.data(),
                    snapshot.positions.data(), submission.counters.position_upload_bytes);

        render::SubmissionPass mask_pass{};
        mask_pass.target = proxy.mask_atlas_target;
        const graphics::Viewport mask_viewport{
            0.0f, 0.0f, static_cast<float>(kLive2DMaskAtlasWidth),
            static_cast<float>(kLive2DMaskAtlasHeight), 0.0f, 1.0f};
        if (has_active_masks)
        {
            for (const Live2DMaskAtlasContext &context :
                 mask_result.plan.contexts)
            {
                const graphics::Scissor scissor{
                    static_cast<std::int32_t>(context.region.x),
                    static_cast<std::int32_t>(context.region.y),
                    context.region.width, context.region.height};
                for (const std::uint32_t source_index :
                     context.source_drawable_indices)
                {
                    const Live2DDrawableStatic &source =
                        static_data.drawables[source_index];
                    const Live2DDrawableState &state =
                        snapshot.drawables[source_index];
                    if (!state.visible || state.opacity <= 0.0f)
                    {
                        continue;
                    }
                    const graphics::PipelineHandle pipeline =
                        resources.MaskPipelineFor(state.culling);
                    if (!pipeline.IsValid())
                    {
                        result.diagnostic =
                            "Live2D render resource set lacks the selected mask pipeline";
                        return result;
                    }

                    Live2DMaskSourceConstants constants{};
                    constants.model_to_mask = context.region.model_to_mask;
                    constants.channel = context.region.channel;
                    constants.opacity = std::clamp(state.opacity, 0.0f, 1.0f);

                    render::SubmissionDraw draw{};
                    draw.pipeline = pipeline;
                    draw.geometry.vertices = {
                        {0u, proxy.position_buffer, 0u},
                        {1u, proxy.uv_buffer, 0u}};
                    draw.geometry.indices = {proxy.index_buffer, 0u,
                                             graphics::IndexElementType::UInt16};
                    draw.uniforms.push_back({
                        kConstantsBinding, kConstantsBinding,
                        std::vector<std::byte>(sizeof(constants))});
                    std::memcpy(draw.uniforms.back().bytes.data(), &constants,
                                sizeof(constants));
                    draw.textures.push_back({0u, kTextureBinding,
                                             proxy.textures[source.texture_index],
                                             resources.sampler});
                    draw.viewport = mask_viewport;
                    draw.scissor = scissor;
                    draw.index_count = source.index_count;
                    draw.first_index = source.first_index;
                    draw.vertex_offset =
                        static_cast<std::int32_t>(source.vertex_offset);
                    mask_pass.draws.push_back(std::move(draw));
                    ++submission.counters.submitted_mask_source_draw_count;
                }
            }
            submission.work.passes.push_back(std::move(mask_pass));
        }

        render::SubmissionPass pass{};
        pass.presentation = proxy.output_to_presentation;
        pass.target = proxy.output_to_presentation ? graphics::RenderTargetHandle{}
                                                    : proxy.output_target;
        for (const std::uint32_t drawable_index : order)
        {
            const Live2DDrawableStatic &drawable = static_data.drawables[drawable_index];
            const Live2DDrawableState &state = snapshot.drawables[drawable_index];
            if (!state.visible || state.opacity <= 0.0f)
            {
                ++submission.counters.skipped_invisible_count;
                continue;
            }
            if (drawable.index_count == 0u || drawable.vertex_count == 0u)
            {
                ++submission.counters.skipped_empty_count;
                continue;
            }
            if (drawable.vertex_offset >
                static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()))
            {
                result.diagnostic = "Live2D drawable vertex offset exceeds signed draw range";
                return result;
            }

            const std::uint32_t planned_context =
                mask_result.plan.drawable_context_indices[drawable_index];
            const bool masked = planned_context != kLive2DNoMaskContext;
            const graphics::PipelineHandle pipeline = masked
                                                         ? resources.MaskedPipelineFor(
                                                               state.blend_mode,
                                                               state.culling)
                                                         : resources.PipelineFor(
                                                               state.blend_mode,
                                                               state.culling);
            if (!pipeline.IsValid())
            {
                result.diagnostic = "Live2D render resource set lacks the selected pipeline";
                return result;
            }

            render::SubmissionDraw draw{};
            draw.pipeline = pipeline;
            draw.geometry.vertices = {
                {0u, proxy.position_buffer, 0u},
                {1u, proxy.uv_buffer, 0u}};
            draw.geometry.indices = {proxy.index_buffer, 0u,
                                     graphics::IndexElementType::UInt16};
            if (masked)
            {
                const Live2DMaskAtlasContext &context =
                    mask_result.plan.contexts[planned_context];
                Live2DMaskedDrawConstants constants{};
                constants.drawable.model_transform = options.model_transform;
                constants.drawable.multiply_color = state.multiply_color;
                constants.drawable.screen_color = state.screen_color;
                constants.drawable.opacity = std::clamp(state.opacity, 0.0f, 1.0f);
                constants.model_to_atlas_sample =
                    context.region.model_to_atlas_sample;
                constants.channel = context.region.channel;
                constants.inverted = state.inverted_mask ? 1u : 0u;
                draw.uniforms.push_back({
                    kConstantsBinding, kConstantsBinding,
                    std::vector<std::byte>(sizeof(constants))});
                std::memcpy(draw.uniforms.back().bytes.data(), &constants,
                            sizeof(constants));
            }
            else
            {
                Live2DDrawConstants constants{};
                constants.model_transform = options.model_transform;
                constants.multiply_color = state.multiply_color;
                constants.screen_color = state.screen_color;
                constants.opacity = std::clamp(state.opacity, 0.0f, 1.0f);
                draw.uniforms.push_back({
                    kConstantsBinding, kConstantsBinding,
                    std::vector<std::byte>(sizeof(constants))});
                std::memcpy(draw.uniforms.back().bytes.data(), &constants,
                            sizeof(constants));
            }
            draw.textures.push_back({0u, kTextureBinding,
                                     proxy.textures[drawable.texture_index],
                                     resources.sampler});
            if (masked)
            {
                draw.textures.push_back({0u, kMaskAtlasBinding,
                                         proxy.mask_atlas_texture,
                                         resources.sampler});
            }
            draw.viewport = viewport;
            draw.index_count = drawable.index_count;
            draw.first_index = drawable.first_index;
            draw.vertex_offset = static_cast<std::int32_t>(drawable.vertex_offset);
            pass.draws.push_back(std::move(draw));
            ++submission.counters.submitted_draw_count;
        }
        submission.work.passes.push_back(std::move(pass));

        const render::RenderSubmissionValidation validation =
            render::ValidateRenderSubmission(submission.work);
        if (!validation.valid)
        {
            result.diagnostic = validation.diagnostic;
            return result;
        }
        result.succeeded = true;
        result.submission = std::move(submission);
        return result;
    }
}
