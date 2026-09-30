#ifndef KPENGINE_LIVE2D_VIEWER_BUBBLE_H
#define KPENGINE_LIVE2D_VIEWER_BUBBLE_H

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/base/graphics_type.h"
#include "render/render_submission.h"

namespace kpengine::graphics
{
    class RenderBackend;
    struct Extent2D;
}

namespace kpengine::panel
{
    class GlyphSet;
}

namespace kpengine::live2d::editor
{
    struct Live2DViewerBubbleModelBounds final
    {
        bool valid = false;
        float min_x = 0.0f;
        float min_y = 0.0f;
        float max_x = 0.0f;
        float max_y = 0.0f;
    };

    // Presentation-only owner for the optional speech/emotion bubble. It keeps
    // panel glyphs, timing, layout policy, and bubble GPU resources out of the
    // viewer lifecycle host.
    class Live2DViewerBubble final
    {
    public:
        Live2DViewerBubble();
        ~Live2DViewerBubble();

        Live2DViewerBubble(const Live2DViewerBubble &) = delete;
        Live2DViewerBubble &operator=(const Live2DViewerBubble &) = delete;

        bool Initialize(graphics::RenderBackend &backend, TextureFormat color_format,
                        std::string &diagnostic);
        bool SetGlyphs(panel::GlyphSet glyphs, std::string &diagnostic);
        bool SetInitialText(std::string_view text, std::string &diagnostic);
        bool ShowEmotion(std::string_view text, std::string &diagnostic);

        void Tick(float delta_time) noexcept;
        bool IsEnabled() const noexcept { return enabled_; }
        bool IsTimed() const noexcept { return timed_; }
        bool IsPopSettled() const noexcept { return pop_ >= 1.0f; }

        void CollectRetiredResources() noexcept;
        void BuildDraws(const graphics::Extent2D &extent,
                        const Live2DViewerBubbleModelBounds &model,
                        std::vector<render::SubmissionDraw> &out) const;

        std::uint32_t GetLiveGpuHandleCount() const noexcept;
        void Cleanup() noexcept;

    private:
        struct State;
        std::unique_ptr<State> state_;
        bool enabled_ = false;
        bool timed_ = false;
        float pop_ = 1.0f;
        float remaining_ = 0.0f;
    };
}

#endif
