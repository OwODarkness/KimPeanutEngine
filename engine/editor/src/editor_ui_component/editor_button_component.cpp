#include "editor_button_component.h"

namespace kpengine
{
    namespace ui
    {

        EditorButtonComponent::EditorButtonComponent(const std::string &label):
        label_(label)
        {
            button_style.text_color = ImVec4(1.f, 1.f, 1.f, 1.f);
            button_style.background_normal_color = ImVec4(0.2f, 0.3f, 0.4f, 1.0f);
            button_style.background_hovered_color = ImVec4(0.3f, 0.4f, 0.5f, 1.0f);
            button_style.background_active_color = ImVec4(0.1f, 0.2f, 0.3f, 1.0f);
        }

        void EditorButtonComponent::BindClickEvent(std::function<void()> callback)
        {
            click_callback_ = callback;
        }

        void EditorButtonComponent::Render()
        {
            ImGui::PushStyleColor(ImGuiCol_Text, button_style.text_color);            
            ImGui::PushStyleColor(ImGuiCol_Button, button_style.background_normal_color);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, button_style.background_hovered_color);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, button_style.background_active_color);
            ImGui::Button(label_.c_str());
            ImGui::PopStyleColor(sizeof(ButtonStyle) / sizeof(ImVec4));
            if(ImGui::IsItemClicked())
            {
                on_click_notify_.ExecuteIfBound();
            }
        }


    }
}