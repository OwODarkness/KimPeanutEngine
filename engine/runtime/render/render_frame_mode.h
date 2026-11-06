#ifndef KPENGINE_RUNTIME_RENDER_RENDER_FRAME_MODE_H
#define KPENGINE_RUNTIME_RENDER_RENDER_FRAME_MODE_H

namespace kpengine::render
{
    struct RenderFrameModeInput
    {
        bool path_trace_requested = false;
        bool path_trace_available = false;
        bool scene_has_tlas = false;
        bool scene_has_instances = false;
        bool scene_within_capacity = false;
        bool temporal_aa_requested = true;
        bool screen_space_ao_requested = true;
        bool height_fog_requested = false;
    };

    struct RenderFrameMode
    {
        bool path_tracing = false;
        bool temporal_aa = false;
        bool screen_space_ao = false;
        bool height_fog = false;
    };

    constexpr RenderFrameMode ResolveRenderFrameMode(RenderFrameModeInput input) noexcept
    {
        const bool path_tracing = input.path_trace_requested && input.path_trace_available &&
            input.scene_has_tlas && input.scene_has_instances && input.scene_within_capacity;
        return {path_tracing, input.temporal_aa_requested && !path_tracing,
                input.screen_space_ao_requested && !path_tracing,
                input.height_fog_requested && !path_tracing};
    }
}

#endif
