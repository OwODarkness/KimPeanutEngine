#include "editor/ui/component/editor_button_component.h"

namespace kpengine::editor
{

    EditorButtonComponent::EditorButtonComponent(const std::string &label) : label_(label)
    {
        button_style.text_color = ImVec4(0.8431373f, 0.9764706f, 1.0f, 1.0f); // #d7f9ff
        button_style.background_normal_color = ImVec4(0.0784314f, 0.1411765f,
                                                       0.2196078f, 1.0f); // #142438
        button_style.background_hovered_color = ImVec4(0.0862745f, 0.2509804f,
                                                        0.3294118f, 1.0f); // #164054
        button_style.background_active_color = ImVec4(0.0f, 0.4235294f,
                                                       0.4784314f, 1.0f); // #006c7a
    }

    void EditorButtonComponent::Render()
    {
        ImGui::PushStyleColor(ImGuiCol_Text, button_style.text_color);
        ImGui::PushStyleColor(ImGuiCol_Button, button_style.background_normal_color);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, button_style.background_hovered_color);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, button_style.background_active_color);
        ImGui::Button(label_.c_str());
        ImGui::PopStyleColor(sizeof(ButtonStyle) / sizeof(ImVec4));
        if (ImGui::IsItemClicked())
        {
            on_click_notify_.ExecuteIfBound();
        }
    }

}
