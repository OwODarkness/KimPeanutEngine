#include "live2d_viewer_bubble.h"

#include <algorithm>
#include <array>
#include <string_view>

#include "bubble_renderer.h"
#include "graphics/backend/common/render_backend.h"
#include "glyph_cell.h"
#include "panel.h"
#include "panel_glyph_product.h"
#include "log/logger.h"

namespace kpengine::live2d::editor
{
    namespace
    {
        constexpr std::uint32_t kMinimumEmotionTextWidth = panel::kHalfwidthAdvance * 4u;

        panel::GlyphCell MakeEmotionGlyph(
            const std::array<std::string_view, 8u> &pattern)
        {
            panel::GlyphCell glyph{};
            glyph.advance = panel::kHalfwidthAdvance;
            for (std::uint32_t row = 0u; row < pattern.size(); ++row)
            {
                for (std::uint32_t column = 0u; column < pattern[row].size(); ++column)
                {
                    if (pattern[row][column] != ' ')
                    {
                        glyph.SetDot(row * 2u, column, true);
                        glyph.SetDot(row * 2u + 1u, column, true);
                    }
                }
            }
            return glyph;
        }

        panel::GlyphSet MakeEmotionGlyphs()
        {
            std::vector<panel::GlyphCell> glyphs(95u);
            const auto set = [&glyphs](const char character,
                                       const std::array<std::string_view, 8u> &pattern) {
                glyphs[static_cast<std::size_t>(character) - 32u] =
                    MakeEmotionGlyph(pattern);
            };
            set('^', {"  **  ", " *  * ", "*    *", "      ",
                      "      ", "      ", "      ", "      "});
            set('_', {"      ", "      ", "      ", "      ",
                      "      ", "      ", " ******", "      "});
            set('T', {"*******", "*******", "   **  ", "   **  ",
                      "   **  ", "   **  ", "   **  ", "   **  "});
            set('>', {"*     ", " **   ", "   ** ", "     *",
                      "     *", "   ** ", " **   ", "*     "});
            set('<', {"     *", "   ** ", " **   ", "*     ",
                      "*     ", " **   ", "   ** ", "     *"});
            set('o', {"  **** ", " **  **", "**    *", "**    *",
                      "**    *", " **  **", "  **** ", "      "});
            set('O', {"  **** ", " **  **", "**    *", "**    *",
                      "**    *", " **  **", "  **** ", "      "});
            set('x', {"*    *", " *  * ", "  **  ", "  **  ",
                      "  **  ", " *  * ", "*    *", "      "});
            return panel::GlyphSet(32u, std::move(glyphs));
        }

        void ConfigureAppearance(BubbleAppearance &appearance,
                                 BubblePlacement &placement) noexcept
        {
            appearance.fill_color = {0.02f, 0.03f, 0.06f, 1.0f};
            appearance.bezel_color = {0.07f, 0.16f, 0.30f, 1.0f};
            appearance.dot_color = {0.10f, 0.95f, 1.0f, 1.0f};
            appearance.outline_color = {1.0f, 0.16f, 0.66f, 1.0f};
            appearance.dot_gap = 0.22f;
            placement.center_x = 0.26f;
            placement.center_y = 0.16f;
            placement.height_fraction = 0.15f;
        }
    }

    struct Live2DViewerBubble::State final
    {
        std::unique_ptr<BubbleRenderer> renderer;
        panel::GlyphSet glyphs{32u, std::vector<panel::GlyphCell>(95u)};
        panel::Panel panel;
        panel::DotMatrix ink{0u, 0u};
        BubbleAppearance appearance{};
        BubblePlacement placement{};
        bool placement_logged = false;
    };

    Live2DViewerBubble::Live2DViewerBubble()
        : state_(std::make_unique<State>())
    {
        state_->glyphs = MakeEmotionGlyphs();
        ConfigureAppearance(state_->appearance, state_->placement);
    }

    Live2DViewerBubble::~Live2DViewerBubble()
    {
        Cleanup();
    }

    bool Live2DViewerBubble::Initialize(graphics::RenderBackend &backend,
                                        const TextureFormat color_format,
                                        std::string &diagnostic)
    {
        if (state_->renderer)
        {
            return true;
        }
        state_->renderer = std::make_unique<BubbleRenderer>();
        if (!state_->renderer->Initialize(backend, color_format, diagnostic))
        {
            state_->renderer.reset();
            return false;
        }
        return true;
    }

    bool Live2DViewerBubble::SetGlyphs(panel::GlyphSet glyphs,
                                       std::string &diagnostic)
    {
        if (glyphs.Count() == 0u)
        {
            diagnostic = "Live2D bubble glyph set is empty";
            return false;
        }
        state_->glyphs = std::move(glyphs);
        return true;
    }

    bool Live2DViewerBubble::SetInitialText(const std::string_view text,
                                            std::string &diagnostic)
    {
        if (!state_->renderer)
        {
            diagnostic = "Live2D bubble renderer is not initialized";
            return false;
        }
        state_->panel.SetText(0u, text);
        state_->ink = state_->panel.Rebuild(state_->glyphs);
        if (!state_->renderer->UploadText(state_->ink, diagnostic))
        {
            return false;
        }
        enabled_ = true;
        timed_ = false;
        pop_ = 0.0f;
        return true;
    }

    bool Live2DViewerBubble::ShowEmotion(const std::string_view text,
                                         std::string &diagnostic)
    {
        if (!SetInitialText(text, diagnostic))
        {
            return false;
        }
        timed_ = true;
        remaining_ = 2.5f;
        return true;
    }

    void Live2DViewerBubble::Tick(const float delta_time) noexcept
    {
        if (!enabled_)
        {
            return;
        }
        constexpr float kPopSeconds = 0.18f;
        pop_ = std::min(1.0f, pop_ + delta_time / kPopSeconds);
        if (timed_)
        {
            remaining_ -= delta_time;
            if (remaining_ <= 0.0f)
            {
                remaining_ = 0.0f;
                timed_ = false;
                enabled_ = false;
            }
        }
    }

    void Live2DViewerBubble::CollectRetiredResources() noexcept
    {
        if (state_->renderer)
        {
            state_->renderer->CollectRetiredResources();
        }
    }

    void Live2DViewerBubble::BuildDraws(
        const graphics::Extent2D &extent,
        const Live2DViewerBubbleModelBounds &model,
        std::vector<render::SubmissionDraw> &out) const
    {
        if (!enabled_ || !state_->renderer)
        {
            return;
        }
        const panel::DotBounds bounds = panel::LitBounds(state_->ink);
        if (bounds.empty)
        {
            return;
        }
        BubbleLayoutRequest request;
        request.text_width = bounds.right - bounds.left + 1u;
        request.text_height = bounds.bottom - bounds.top + 1u;
        if (timed_)
        {
            request.text_width = std::max(request.text_width, kMinimumEmotionTextWidth);
        }
        if (model.valid)
        {
            const float model_center_x = (model.min_x + model.max_x) * 0.5f;
            const float model_center_y = (model.min_y + model.max_y) * 0.5f;
            request.tail = BubbleTailFromDirection(
                model_center_x - state_->placement.center_x,
                model_center_y - state_->placement.center_y);
        }
        else
        {
            request.tail = BubbleTail::Down;
        }
        BubbleLayout layout;
        if (!BuildBubbleLayout(request, layout) || extent.width == 0u || extent.height == 0u)
        {
            return;
        }
        state_->placement.progress = pop_;
        state_->placement.target_aspect =
            static_cast<float>(extent.width) / static_cast<float>(extent.height);
        graphics::Viewport viewport{};
        viewport.width = static_cast<float>(extent.width);
        viewport.height = static_cast<float>(extent.height);
        std::string diagnostic;
        if (!state_->renderer->BuildDraws(layout, state_->placement,
                                          state_->appearance, viewport, out, diagnostic))
        {
            KP_LOG("Live2DViewer", LOG_LEVEL_WARNING, "bubble draw skipped: %s",
                   diagnostic.c_str());
        }
    }

    std::uint32_t Live2DViewerBubble::GetLiveGpuHandleCount() const noexcept
    {
        return state_->renderer ? state_->renderer->GetLiveGpuHandleCount() : 0u;
    }

    void Live2DViewerBubble::Cleanup() noexcept
    {
        if (state_ && state_->renderer)
        {
            state_->renderer->Cleanup();
            state_->renderer.reset();
        }
        enabled_ = false;
    }
}
