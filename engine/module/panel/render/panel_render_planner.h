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
        // Lit extent in normalized panel coordinates, as min_x/min_y/max_x/max_y.
        // It describes the mask, so it belongs with the mask handle rather than
        // with the appearance: the ramp spans what is drawn, and only the mask
        // knows what that is.
        std::array<float, 4> ink_bounds{0.0f, 0.0f, 1.0f, 1.0f};
    };

    // Colours are in display space (sRGB), because that is what a caller picks
    // and what a hex literal means. The planner linearises them, because the
    // output target is sRGB and the hardware encodes on store.
    struct PanelRenderPlanOptions final
    {
        std::array<float, 4> dot_color{1.0f, 1.0f, 1.0f, 1.0f};
        std::array<float, 4> background_color{0.0f, 0.0f, 0.0f, 1.0f};
        // The far end of the colour ramp, unused while gradient_amount is zero.
        std::array<float, 4> accent_color{1.0f, 1.0f, 1.0f, 1.0f};
        // Fraction of a dot left dark on every side. A parameter rather than a
        // constant because the look is a look: it is clamped by the shader, so
        // an out-of-range value degrades rather than corrupting the panel.
        float dot_gap = kPanelDefaultDotGap;
        // How far the ramp runs toward the accent colour: 0 keeps a flat dot
        // colour and is the default, so an unset gradient changes nothing.
        float gradient_amount = 0.0f;
        PanelGradientAxis gradient_axis = PanelGradientAxis::Horizontal;
        // Elapsed seconds and cycles per second. A zero rate is a still ramp.
        // Time is supplied by the caller rather than read from a clock here, so
        // planning stays a pure function of its inputs.
        float elapsed_seconds = 0.0f;
        float cycles_per_second = 0.0f;
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
