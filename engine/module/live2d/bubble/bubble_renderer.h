#ifndef KPENGINE_LIVE2D_BUBBLE_RENDERER_H
#define KPENGINE_LIVE2D_BUBBLE_RENDERER_H

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "bubble_layout.h"
#include "bubble_render_contract.h"
#include "core/base/graphics_type.h"
#include "graphics/backend/common/api.h"
#include "render/render_submission.h"

namespace kpengine::panel
{
    class DotMatrix;
}

namespace kpengine::graphics
{
    class RenderBackend;
}

namespace kpengine::live2d
{
    // Owns the bubble's GPU resources and produces the draws for it. It does not
    // own a render target and does not begin a pass: a pass of its own would
    // re-apply the target's clear and erase the model, so its draws are appended
    // into the pass the model is drawn in.
    class BubbleRenderer final
    {
    public:
        BubbleRenderer() = default;
        ~BubbleRenderer();

        BubbleRenderer(const BubbleRenderer &) = delete;
        BubbleRenderer &operator=(const BubbleRenderer &) = delete;

        const char *GetName() const noexcept { return "Bubble"; }

        // The colour format has to be the one the model's pass draws into:
        // a pipeline's attachment format is baked when it is created.
        bool Initialize(graphics::RenderBackend &backend, TextureFormat color_format,
                        std::string &diagnostic);

        // Uploads the text's ink mask, one texel per dot, replacing any previous
        // one. Recreate rather than update, because the backend has no in-place
        // upload; the previous texture is released only after WaitIdle, since
        // submitted work may still reference it.
        bool UploadText(const panel::DotMatrix &ink, std::string &diagnostic);
        bool HasText() const noexcept { return text_mask_.IsValid(); }

        // Appends the bubble's draw to `out`, for the caller to insert into the
        // model's pass. The viewport is the pass's, not the bubble's: placement is
        // a matrix, because the two backends measure a viewport's y from opposite
        // ends and a sub-rect placed that way would be mirrored between them.
        bool BuildDraws(const BubbleLayout &layout, const BubblePlacement &placement,
                        const BubbleAppearance &appearance,
                        const graphics::Viewport &viewport,
                        std::vector<render::SubmissionDraw> &out,
                        std::string &diagnostic) const;

        // Counts the GPU handles this owns. Zero is the shutdown contract.
        std::uint32_t GetLiveGpuHandleCount() const noexcept;

        void Cleanup() noexcept;

    private:
        bool CreatePipeline(TextureFormat color_format, std::string &diagnostic);
        bool CreateQuadGeometry(std::string &diagnostic);
        void DestroyText() noexcept;
        void DestroyQuadGeometry() noexcept;
        void DestroyPipeline() noexcept;

        graphics::RenderBackend *backend_ = nullptr;
        graphics::PipelineHandle pipeline_{};
        graphics::SamplerHandle sampler_{};
        graphics::BufferHandle quad_vertices_{};
        graphics::BufferHandle quad_indices_{};
        graphics::TextureHandle text_mask_{};
        // What the shader needs to read the text out of the mask: where it sits
        // in it, and how many dots it is made of. Measured when the mask is
        // uploaded, because that is where the mask's extent is known.
        BubbleMaskInfo text_mask_info_{};
        bool initialized_ = false;
    };
}

#endif
