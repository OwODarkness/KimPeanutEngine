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

        template<typename T>
        class EditorDynamicTextComponent : public EditorTextComponent { 
        public:
            EditorDynamicTextComponent(const T* text_ref,  const std::string& label = "", ImVec4 color = ImVec4(1.f, 1.f, 1.f, 1.f));
            virtual void Render() override;
        public:
            const T* text_ref_;
            std::string label_;
        };
        
        // Implementation must include template parameter
        template<typename T>
        EditorDynamicTextComponent<T>::EditorDynamicTextComponent(const T* text_ref, const std::string& label, ImVec4 color)
            : EditorTextComponent("", color),  // Base class first in initialization list
              text_ref_(text_ref),
              label_(label)
        {
        }
        
        template<typename T>
        void EditorDynamicTextComponent<T>::Render()
        {
            if (text_ref_) {  // Check for null pointer
                // For more generic solution, consider using string streams or specialization
                content_ = label_ + std::to_string(*text_ref_);
            }
            EditorTextComponent::Render();
        }
    }
}

#endif