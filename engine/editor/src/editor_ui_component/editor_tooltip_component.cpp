#include "editor_tooltip_component.h"
#include "editor_text_component.h"

namespace kpengine::editor
{
        EditorTooltipComponent::EditorTooltipComponent(const std::string &content, ImVec4 color)
        {
            inner_comp_ = new EditorTextComponent(content, color);
        }
        EditorTooltipComponent::EditorTooltipComponent(EditorUIComponent *inner_comp)
        {
            inner_comp_ = inner_comp;
        }

        EditorTooltipComponent::~EditorTooltipComponent()
        {
            delete inner_comp_;
            inner_comp_ = nullptr;
        }

        void EditorTooltipComponent::Render()
        {
            ImGui::SameLine();
            ImGui::TextDisabled("?");
            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                inner_comp_->Render();
                ImGui::EndTooltip();
            }
    }
}