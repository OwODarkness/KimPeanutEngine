#include "editor/ui/component/editor_loading_component.h"

#include <cstdio>
#include <utility>

#include <imgui.h>

namespace kpengine::editor
{
    EditorLoadingComponent::EditorLoadingComponent(
        std::function<runtime::StartupSnapshot()> snapshot_source)
        : snapshot_source_(std::move(snapshot_source))
    {
    }

    void EditorLoadingComponent::Render()
    {
        const runtime::StartupSnapshot snapshot =
            snapshot_source_ ? snapshot_source_() : runtime::StartupSnapshot{};
        last_view_model_ = BuildEditorLoadingViewModel(snapshot);

        const ImGuiViewport *viewport = ImGui::GetMainViewport();
        const ImVec2 panel_size(560.0f, 250.0f);
        const ImVec2 panel_position(
            viewport->WorkPos.x + (viewport->WorkSize.x - panel_size.x) * 0.5f,
            viewport->WorkPos.y + (viewport->WorkSize.y - panel_size.y) * 0.5f);
        ImGui::SetNextWindowPos(panel_position, ImGuiCond_Always);
        ImGui::SetNextWindowSize(panel_size, ImGuiCond_Always);

        constexpr ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration |
                                                   ImGuiWindowFlags_NoMove |
                                                   ImGuiWindowFlags_NoSavedSettings |
                                                   ImGuiWindowFlags_NoBringToFrontOnFocus;
        ImGui::Begin("##StartupLoading", nullptr, window_flags);
        ImGui::TextUnformatted("KimPeanut Engine");
        ImGui::TextUnformatted("Loading startup scene");
        ImGui::Separator();
        ImGui::Text("Stage: %s", last_view_model_.stage_label.c_str());
        if (!last_view_model_.current_item.empty())
        {
            ImGui::Text("Current: %s", last_view_model_.current_item.c_str());
        }
        ImGui::TextUnformatted(last_view_model_.counts_label.c_str());
        ImGui::Spacing();

        if (last_view_model_.determinate)
        {
            char overlay[32]{};
            std::snprintf(overlay, sizeof(overlay), "%.0f%%",
                          last_view_model_.fraction * 100.0f);
            ImGui::ProgressBar(last_view_model_.fraction, ImVec2(-1.0f, 0.0f), overlay);
        }
        else
        {
            ImGui::ProgressBar(-static_cast<float>(ImGui::GetTime()),
                               ImVec2(-1.0f, 0.0f), "Working...");
        }

        if (last_view_model_.failed && !last_view_model_.diagnostic.empty())
        {
            ImGui::Spacing();
            ImGui::TextWrapped("Diagnostic: %s", last_view_model_.diagnostic.c_str());
        }
        ImGui::End();
    }
}
