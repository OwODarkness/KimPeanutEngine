#ifndef KPENGINE_EDITOR_TEXT_COMPONENT_H
#define KPENGINE_EDITOR_TEXT_COMPONENT_H

#include <string>
#include "editor/include/editor_ui_component.h"

namespace kpengine{
    namespace ui{

        class EditorTextComponent: public EditorUIComponent{
        public:
            EditorTextComponent(const std::string& content, ImVec4 color = ImVec4(1.f, 1.f, 1.f, 1.f));
            virtual void Render() override;
        public:
            std::string content_;
            ImVec4 color_;
        };

        class EditorDynamicTextComponent: public EditorTextComponent{ 
        public:
        
            EditorDynamicTextComponent(int* text_ref ,ImVec4 color = ImVec4(1.f, 1.f, 1.f, 1.f));
            
            virtual void Render() override;
        public:
            int* text_ref_;
        };
    }
}

#endif