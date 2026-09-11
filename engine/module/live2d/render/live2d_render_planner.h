#ifndef KPENGINE_LIVE2D_RENDER_PLANNER_H
#define KPENGINE_LIVE2D_RENDER_PLANNER_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "graphics/backend/common/api.h"
#include "graphics/backend/common/command_recorder.h"
#include "render/render_submission.h"
#include "live2d_mask_planner.h"
#include "runtime/live2d_model_data.h"

namespace kpengine::live2d
{
    struct Live2DRenderResourceSet final
    {
        Live2DRenderResourceSet() = default;
        ~Live2DRenderResourceSet() = default;
        Live2DRenderResourceSet(const Live2DRenderResourceSet &) = delete;
        Live2DRenderResourceSet &operator=(const Live2DRenderResourceSet &) = delete;
        Live2DRenderResourceSet(Live2DRenderResourceSet &&) noexcept = default;
        Live2DRenderResourceSet &operator=(Live2DRenderResourceSet &&) noexcept = default;

        graphics::PipelineHandle normal_culled;
        graphics::PipelineHandle normal_unculled;
        graphics::PipelineHandle additive_culled;
        graphics::PipelineHandle additive_unculled;
        graphics::PipelineHandle multiplicative_culled;
        graphics::PipelineHandle multiplicative_unculled;
        graphics::PipelineHandle masked_normal_culled;
        graphics::PipelineHandle masked_normal_unculled;
        graphics::PipelineHandle masked_additive_culled;
        graphics::PipelineHandle masked_additive_unculled;
        graphics::PipelineHandle masked_multiplicative_culled;
        graphics::PipelineHandle masked_multiplicative_unculled;
        graphics::PipelineHandle mask_culled;
        graphics::PipelineHandle mask_unculled;
        graphics::SamplerHandle sampler;

        graphics::PipelineHandle PipelineFor(Live2DBlendMode blend_mode,
                                              bool culling) const noexcept;
        graphics::PipelineHandle MaskedPipelineFor(Live2DBlendMode blend_mode,
                                                   bool culling) const noexcept;
        graphics::PipelineHandle MaskPipelineFor(bool culling) const noexcept;
    };

    // The proxy is a render-owned immutable resource description. GPU handles
    // are borrowed from the active Render resource owner and must outlive a
    // planned submission; this type owns only the ordered CPU-side topology.
    struct Live2DRenderProxy final
    {
        Live2DRenderProxy() = default;
        ~Live2DRenderProxy() = default;
        Live2DRenderProxy(const Live2DRenderProxy &) = delete;
        Live2DRenderProxy &operator=(const Live2DRenderProxy &) = delete;
        Live2DRenderProxy(Live2DRenderProxy &&) noexcept = default;
        Live2DRenderProxy &operator=(Live2DRenderProxy &&) noexcept = default;

        graphics::BufferHandle position_buffer;
        graphics::BufferHandle uv_buffer;
        graphics::BufferHandle index_buffer;
        graphics::RenderTargetHandle output_target;
        graphics::RenderTargetHandle mask_atlas_target;
        graphics::TextureHandle mask_atlas_texture;
        std::vector<graphics::TextureHandle> textures;
        Live2DStaticModelData static_data;
        std::uint32_t output_width = 0u;
        std::uint32_t output_height = 0u;
    };

    struct Live2DRenderPlanOptions final
    {
        std::array<float, 16> model_transform{
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f};
        graphics::Viewport viewport{};
    };

    // This is the backend-neutral constant layout consumed by the future
    // Live2D shader pair. Keeping it here makes planner output testable without
    // exposing a shader or graphics-backend type.
    struct Live2DDrawConstants final
    {
        std::array<float, 16> model_transform{};
        Live2DColor multiply_color{1.0f, 1.0f, 1.0f, 1.0f};
        Live2DColor screen_color{};
        float opacity = 1.0f;
        // GLSL std140 gives the vec3 padding a full 16-byte slot. Keep the
        // following matrix aligned to the same boundary in the CPU payload.
        std::array<float, 4> padding{};
    };

    struct Live2DMaskSourceConstants final
    {
        std::array<float, 16> model_to_mask{};
        std::uint32_t channel = 0u;
        float opacity = 1.0f;
        std::array<float, 2> padding{};
    };

    struct Live2DMaskedDrawConstants final
    {
        Live2DDrawConstants drawable{};
        std::array<float, 16> model_to_atlas_sample{};
        std::uint32_t channel = 0u;
        std::uint32_t inverted = 0u;
        std::array<std::uint32_t, 2> padding{};
    };

    struct Live2DRenderCounters final
    {
        std::uint32_t submitted_draw_count = 0u;
        std::uint32_t skipped_invisible_count = 0u;
        std::uint32_t skipped_empty_count = 0u;
        std::uint32_t submitted_mask_source_draw_count = 0u;
        std::uint32_t active_mask_context_count = 0u;
        std::size_t position_upload_bytes = 0u;
    };

    struct Live2DRenderSubmission final
    {
        render::RenderSubmission work;
        Live2DRenderFeatureReport features{};
        Live2DMaskAtlasPlan mask_plan{};
        Live2DRenderCounters counters{};
        std::uint64_t topology_revision = 0u;
        std::uint64_t frame_sequence = 0u;
    };

    struct Live2DRenderPlanResult final
    {
        bool succeeded = false;
        std::string diagnostic;
        Live2DRenderSubmission submission;
    };

    class Live2DRenderPlanner final
    {
    public:
        static Live2DRenderPlanResult Plan(
            const Live2DRenderResourceSet &resources,
            const Live2DRenderProxy &proxy,
            const Live2DStaticModelData &static_data,
            const Live2DFrameSnapshot &snapshot,
            const Live2DRenderPlanOptions &options = {});
    };
}

#endif
