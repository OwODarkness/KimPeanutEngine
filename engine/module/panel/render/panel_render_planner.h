#ifndef KPENGINE_MODULE_PANEL_RENDER_PLANNER_H
#define KPENGINE_MODULE_PANEL_RENDER_PLANNER_H

#include <cstdint>
#include <string>

#include "graphics/backend/common/api.h"
#include "render/render_submission.h"
#include "panel_render_contract.h"

namespace kpengine::panel
{
    // Borrowed GPU handles plus the immutable quad topology. The panel owns
    // every handle here; this type only describes them, so it must not outlive
    // the owner and must stay valid until execution returns.
    struct PanelRenderProxy final
    {
        graphics::PipelineHandle pipeline;
        graphics::TextureHandle dot_mask;
        graphics::SamplerHandle sampler;
        graphics::BufferHandle quad_vertices;
        graphics::BufferHandle quad_indices;
        graphics::RenderTargetHandle output_target;
        std::uint32_t output_width = 0u;
        std::uint32_t output_height = 0u;
        std::uint32_t index_count = 0u;
    };

    struct PanelRenderPlanOptions final
    {
        std::array<float, 4> dot_color{1.0f, 1.0f, 1.0f, 1.0f};
        std::array<float, 4> background_color{0.0f, 0.0f, 0.0f, 1.0f};
    };

    struct PanelRenderPlanResult final
    {
        bool succeeded = false;
        std::string diagnostic;
        // Only meaningful when succeeded. A failed plan publishes nothing, so a
        // caller can never record half a panel.
        render::RenderSubmission work;
    };

    class PanelRenderPlanner final
    {
    public:
        // Builds the generic submission for one panel frame: a single pass into
        // the panel's offscreen target containing a single quad that samples the
        // dot mask. It never touches a CommandRecorder and never calls the
        // backend, so it is testable with no GPU and no device.
        //
        // The dot-to-pixel expansion is not arithmetic here. The mask is one
        // texel per dot and the sampler is NEAREST, so drawing the quad larger
        // than the mask is what makes each dot a crisp block.
        static PanelRenderPlanResult Plan(const PanelRenderProxy &proxy,
                                          const PanelRenderPlanOptions &options = {});
    };
}

#endif
