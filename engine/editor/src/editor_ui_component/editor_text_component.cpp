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
    }
}