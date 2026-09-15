#include "panel_render_planner.h"

#include <cstddef>
#include <utility>
#include <vector>

namespace kpengine::panel
{
    namespace
    {
        std::vector<std::byte> ToBytes(const PanelDrawConstants &constants)
        {
            const auto *begin = reinterpret_cast<const std::byte *>(&constants);
            return std::vector<std::byte>(begin, begin + sizeof(constants));
        }
    }

    PanelRenderPlanResult PanelRenderPlanner::Plan(const PanelRenderProxy &proxy,
                                                   const PanelRenderPlanOptions &options)
    {
        PanelRenderPlanResult result;

        // Checked here as well as by the generic validator so the diagnostic
        // names the panel rather than reporting a generic invalid draw.
        if (proxy.output_width == 0u || proxy.output_height == 0u)
        {
            result.diagnostic = "panel plan requires a non-zero output extent";
            return result;
        }
        if (!proxy.output_target.IsValid())
        {
            result.diagnostic = "panel plan requires an offscreen output target";
            return result;
        }
        if (!proxy.pipeline.IsValid())
        {
            result.diagnostic = "panel plan requires a valid pipeline";
            return result;
        }
        if (!proxy.dot_mask.IsValid() || !proxy.sampler.IsValid())
        {
            result.diagnostic = "panel plan requires a sampled dot mask";
            return result;
        }
        if (!proxy.quad_vertices.IsValid() || !proxy.quad_indices.IsValid() ||
            proxy.index_count == 0u)
        {
            result.diagnostic = "panel plan requires quad geometry";
            return result;
        }

        PanelDrawConstants constants{};
        constants.dot_color = options.dot_color;
        constants.background_color = options.background_color;

        render::SubmissionDraw draw{};
        draw.pipeline = proxy.pipeline;
        draw.geometry.vertices = {{0u, proxy.quad_vertices, 0u}};
        draw.geometry.indices = {proxy.quad_indices, 0u,
                                 graphics::IndexElementType::UInt16};

        render::SubmissionUniformData uniform{};
        uniform.set = 0u;
        uniform.binding = kPanelConstantsBinding;
        uniform.bytes = ToBytes(constants);
        draw.uniforms.push_back(std::move(uniform));

        render::SubmissionSampledTexture texture{};
        texture.set = 0u;
        texture.binding = kPanelDotMaskBinding;
        texture.texture = proxy.dot_mask;
        texture.sampler = proxy.sampler;
        draw.textures.push_back(texture);

        draw.viewport.x = 0.0f;
        draw.viewport.y = 0.0f;
        draw.viewport.width = static_cast<float>(proxy.output_width);
        draw.viewport.height = static_cast<float>(proxy.output_height);
        draw.viewport.min_depth = 0.0f;
        draw.viewport.max_depth = 1.0f;
        draw.index_count = proxy.index_count;
        draw.first_index = 0u;
        draw.vertex_offset = 0;

        // No scissor. Scissor state is sticky within a target, so a later stage
        // that adds one here must give the pass's other draws an explicit
        // full-viewport scissor.
        render::SubmissionPass pass{};
        pass.target = proxy.output_target;
        pass.presentation = false;
        pass.draws.push_back(std::move(draw));
        result.work.passes.push_back(std::move(pass));

        const render::RenderSubmissionValidation validation =
            render::ValidateRenderSubmission(result.work);
        if (!validation.valid)
        {
            result.work = {};
            result.diagnostic = "panel render submission is invalid: " +
                                validation.diagnostic;
            return result;
        }

        result.succeeded = true;
        return result;
    }
}
