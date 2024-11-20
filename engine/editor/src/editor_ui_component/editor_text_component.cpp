#include "editor_text_component.h"

namespace kpengine{
    namespace ui{
        
        EditorTextComponent::EditorTextComponent(const std::string& content, ImVec4 color):
        content_(content),
        color_(color)
        {
        }

        void EditorTextComponent::Render()
        {
            ImGui::TextColored(color_, content_.c_str());
        }

        EditorDynamicTextComponent::EditorDynamicTextComponent(int* text_ref ,ImVec4 color):
        text_ref_(text_ref), EditorTextComponent("1", color)
        {

        }

        void EditorDynamicTextComponent::Render()
        {
            content_ = std::to_string(*text_ref_);
            EditorTextComponent::Render();
        }

    }
}