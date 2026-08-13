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

    void EditorProfileBarComponent::Render()
    {
        // Bottom-anchored status bar via public ImGui API only. WorkPos/WorkSize already
        // exclude the top menu bar, so the bottom of the work area is the right anchor.
        // (ImVec2 has no +/- operators unless IMGUI_DEFINE_MATH_OPERATORS — build coords by hand.)
        ImGuiViewport *viewport = ImGui::GetMainViewport();
        // One text row plus the window's vertical padding, so content never clips.
        const float bar_height =
            ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y * 2.0f;
        const ImVec2 bar_pos(viewport->WorkPos.x,
                             viewport->WorkPos.y + viewport->WorkSize.y - bar_height);
        ImGui::SetNextWindowPos(bar_pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, bar_height), ImGuiCond_Always);

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
