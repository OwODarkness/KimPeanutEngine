#include "editor/actor/editor_world_outliner_component.h"

#include <imgui.h>

namespace kpengine::editor
{
    EditorWorldOutlinerComponent::EditorWorldOutlinerComponent(ActorEditorModel &model)
        : EditorWindowComponent("World Outliner", EditorWindowConfig{0.0f, 0.0f, 0.22f, 0.28f,
                                                                        true}),
          model_(model)
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
                if (ImGui::Selectable(label.c_str(), selected))
                {
                    model_.SelectActor(actor.actor);
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
