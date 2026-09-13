#include "editor/profile/editor_profile_bar.h"

#include <cstdio>
#include <utility>
#include <imgui.h>
#include "editor/profile/editor_metric.h"

namespace kpengine::editor
{
    EditorProfileBarComponent::EditorProfileBarComponent(
        std::vector<std::unique_ptr<EditorMetric>> metrics)
        : metrics_(std::move(metrics))
    {
    }

    float EditorProfileBarComponent::MeasurePreferredHeightPx() noexcept
    {
        // One text row plus the window's vertical padding, so content never clips.
        return ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y * 2.0f;
    }

    std::optional<EditorLayoutSlot> EditorProfileBarComponent::GetLayoutSlot() const noexcept
    {
        return EditorLayoutSlot::ProfileBar;
    }

    void EditorProfileBarComponent::ApplyLayout(std::optional<EditorRect> rect) noexcept
    {
        layout_rect_ = rect;
    }

    void EditorProfileBarComponent::Render()
    {
        // Bottom-anchored status bar via public ImGui API only. WorkPos/WorkSize already
        // exclude the top menu bar, so the bottom of the work area is the right anchor.
        // (ImVec2 has no +/- operators unless IMGUI_DEFINE_MATH_OPERATORS — build coords by hand.)
        ImGuiViewport *viewport = ImGui::GetMainViewport();
        const float bar_height = MeasurePreferredHeightPx();

        ImVec2 bar_pos;
        ImVec2 bar_size;
        if (layout_rect_.has_value())
        {
            // The layout owns the strip; fall back to the measured height only if the
            // layout has not resolved yet this frame.
            const EditorRect snapped = SnapEdgesToPixels(*layout_rect_);
            bar_pos = ImVec2(snapped.x, snapped.y);
            bar_size = ImVec2(snapped.width, snapped.height > 0.0f ? snapped.height : bar_height);
        }
        else
        {
            bar_pos = ImVec2(viewport->WorkPos.x,
                             viewport->WorkPos.y + viewport->WorkSize.y - bar_height);
            bar_size = ImVec2(viewport->WorkSize.x, bar_height);
        }
        ImGui::SetNextWindowPos(bar_pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(bar_size, ImGuiCond_Always);

        const ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav;
        if (ImGui::Begin("##EditorProfileBar", nullptr, flags))
        {
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.f, 2.f));
            for (size_t i = 0; i < metrics_.size(); ++i)
            {
                EditorMetric &metric = *metrics_[i];
                const std::string value = metric.Sample();
                if (i > 0)
                {
                    ImGui::SameLine();
                }
                ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), "%s",
                                   metric.Name());
                ImGui::SameLine();
                ImGui::Text("%s", value.c_str());
                if (metric.HasPlot() && metric.History().size() >= 2)
                {
                    ImGui::SameLine();
                    char plot_id[24];
                    std::snprintf(plot_id, sizeof(plot_id), "##plot%zu", i);
                    ImGui::PlotLines(plot_id, metric.History().data(),
                                     static_cast<int>(metric.History().size()), 0, nullptr,
                                     FLT_MAX, FLT_MAX,
                                     ImVec2(64.f, ImGui::GetFrameHeight()));
                }
            }
            ImGui::PopStyleVar();
        }
        ImGui::End();
    }
}
