#include "editor/actor/editor_world_outliner_component.h"

#include "editor/ui/editor_ui_glow.h"

#include <imgui.h>

namespace kpengine::editor
{
    EditorWorldOutlinerComponent::EditorWorldOutlinerComponent(
        ActorEditorModel &model, const bool *glow_enabled)
        : EditorWindowComponent("World Outliner", EditorWindowConfig{}),
          model_(model), glow_enabled_(glow_enabled)
    {
    }

    void EditorWorldOutlinerComponent::RenderContent()
    {
        const auto &snapshot = model_.GetSnapshot();
        if (!snapshot)
        {
            ImGui::TextDisabled("Gameplay snapshot unavailable");
            return;
        }
        if (snapshot->truncated || snapshot->omitted.actors != 0)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.72f, 0.25f, 1.0f));
            ImGui::Text("Snapshot truncated: %zu actors omitted", snapshot->omitted.actors);
            ImGui::PopStyleColor();
        }
        if (snapshot->actors.empty())
        {
            ImGui::TextDisabled("No actors");
            return;
        }

        const std::optional<gameplay::ActorHandle> selection = model_.GetSelection();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(snapshot->actors.size()));
        while (clipper.Step())
        {
            for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index)
            {
                const gameplay::ActorEditorSnapshot &actor = snapshot->actors[static_cast<std::size_t>(index)];
                ImGui::PushID(static_cast<int>(actor.actor.id));
                ImGui::PushID(static_cast<int>(actor.actor.generation));
                const bool selected = selection.has_value() && *selection == actor.actor;
                const std::string label = actor.display_name.empty() ? "Actor" : actor.display_name;
                uint32_t glow_region_id = actor.actor.id * 2654435761U +
                                          actor.actor.generation;
                if (glow_region_id == 0)
                {
                    glow_region_id = 1;
                }
                if (selected && (glow_enabled_ == nullptr || *glow_enabled_))
                {
                    const ImVec2 row_min = ImGui::GetCursorScreenPos();
                    const ImVec2 row_max(row_min.x + ImGui::GetContentRegionAvail().x,
                                         row_min.y + ImGui::GetTextLineHeightWithSpacing());
                    constexpr ImU32 kSelectionGlow = IM_COL32(87, 202, 255, 255);
                    BeginEditorGlowRegion(ImGui::GetWindowDrawList(), glow_region_id,
                                          row_min, row_max, 8.0f);
                    PushEditorGlowEmission(ImGui::GetWindowDrawList(), glow_region_id,
                                           kSelectionGlow, 0.72f);
                }
                if (ImGui::Selectable(label.c_str(), selected))
                {
                    model_.SelectActor(actor.actor);
                }
                if (selected && (glow_enabled_ == nullptr || *glow_enabled_))
                {
                    PopEditorGlowEmission(ImGui::GetWindowDrawList(), glow_region_id);
                    EndEditorGlowRegion(ImGui::GetWindowDrawList(), glow_region_id);
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Handle %u:%u", actor.actor.id,
                                     static_cast<unsigned int>(actor.actor.generation));
                }
                ImGui::SameLine();
                ImGui::TextDisabled("%u:%u", actor.actor.id,
                                    static_cast<unsigned int>(actor.actor.generation));
                ImGui::PopID();
                ImGui::PopID();
            }
        }
    }
}
