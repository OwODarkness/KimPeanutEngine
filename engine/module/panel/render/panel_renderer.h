#ifndef KPENGINE_MODULE_PANEL_RENDERER_H
#define KPENGINE_MODULE_PANEL_RENDERER_H

#include <array>
#include <cstdint>
#include <string>

#include "core/base/graphics_type.h"
#include "graphics/backend/common/api.h"
#include "panel_render_planner.h"

namespace kpengine::panel
{
    class DotMatrix;
}

namespace kpengine::graphics
{
    class CommandRecorder;
    class RenderBackend;
}

namespace kpengine::render
{
    class FrameContext;
}

namespace kpengine::panel
{
    // Owns every GPU handle the panel needs and records its offscreen target
    // through the generic submission API. Nothing here is device-specific: the
    // backend is a common RenderBackend, as it is for the Live2D renderer.
    //
    // Unlike Live2D the geometry is static. The panel's content lives entirely
    // in the dot-mask texture, so no buffer is uploaded per frame.
    class PanelRenderer final
    {
    public:
        PanelRenderer() = default;
        ~PanelRenderer();

        PanelRenderer(const PanelRenderer &) = delete;
        PanelRenderer &operator=(const PanelRenderer &) = delete;

        const char *GetName() const noexcept { return "Panel"; }

        bool Initialize(graphics::RenderBackend &backend, std::uint32_t width,
                        std::uint32_t height, std::string &diagnostic);

        // Recreates the dot mask from the matrix, one texel per dot. A content
        // change recreates rather than updates because the backend exposes no
        // in-place upload; the previous texture is released only after
        // WaitIdle, since submitted work may still reference it.
        bool UploadPanel(const DotMatrix &matrix, std::string &diagnostic);

        // Recreates only the panel-owned color target. Transactional: on
        // failure the previously valid target stays in place and the renderer
        // remains usable.
        bool ResizeOutput(std::uint32_t width, std::uint32_t height,
                          std::string &diagnostic);

        // UploadPanel must have succeeded first; until then there is no dot mask
        // to sample and this reports that rather than drawing nothing.
        bool Record(render::FrameContext &frame_context,
                    graphics::CommandRecorder &recorder,
                    const PanelRenderPlanOptions &options,
                    std::string &diagnostic);

        graphics::RenderTargetHandle GetOutputTarget() const noexcept
        {
            return output_target_;
        }
        graphics::RenderTargetView GetOutputView() const noexcept
        {
            return output_view_;
        }
        std::uint32_t GetOutputWidth() const noexcept { return output_width_; }
        std::uint32_t GetOutputHeight() const noexcept { return output_height_; }
        bool HasDotMask() const noexcept { return dot_mask_.IsValid(); }
        // Lit extent of the current mask, normalized, as min_x/min_y/max_x/max_y.
        // The whole panel until a mask is uploaded, and for a blank one.
        const std::array<float, 4> &GetInkBounds() const noexcept { return ink_bounds_; }

        // Counts the GPU handles this renderer currently owns. Zero is the
        // shutdown contract, computed from the live handle set so it cannot
        // drift from actual ownership.
        std::uint32_t GetLiveGpuHandleCount() const noexcept;

        void Cleanup() noexcept;

    private:
        bool CreatePipeline(std::string &diagnostic);
        bool CreateQuadGeometry(std::string &diagnostic);
        bool CreateOutputTarget(std::uint32_t width, std::uint32_t height,
                                std::string &diagnostic);
        PanelRenderProxy MakeProxy() const noexcept;

        void DestroyDotMask() noexcept;
        void DestroyOutputTarget() noexcept;
        void DestroyQuadGeometry() noexcept;
        void DestroyPipeline() noexcept;

        graphics::RenderBackend *backend_ = nullptr;
        graphics::PipelineHandle pipeline_{};
        graphics::SamplerHandle sampler_{};
        graphics::BufferHandle quad_vertices_{};
        graphics::BufferHandle quad_indices_{};
        graphics::TextureHandle dot_mask_{};
        graphics::RenderTargetHandle output_target_{};
        graphics::RenderTargetView output_view_{};
        // Lit extent of the current mask, normalized. Measured where the mask is
        // measured, because the ramp spans what is drawn and only the mask knows
        // what that is.
        std::array<float, 4> ink_bounds_{0.0f, 0.0f, 1.0f, 1.0f};
        std::uint32_t output_width_ = 0u;
        std::uint32_t output_height_ = 0u;
        TextureFormat output_color_format_ = TextureFormat::TEXTURE_FORMAT_RGBA8_SRGB;
        bool initialized_ = false;
    };
}

#endif
