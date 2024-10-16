#ifndef KPENGINE_EDITOR_EDITOR_BUTTON_COMPONENT_H
#define KPENGINE_EDITOR_EDITOR_BUTTON_COMPONENT_H

#include <string>
#include <functional>
#include "editor/include/editor_ui_component.h"


namespace kpengine{
    namespace ui{

        struct ButtonStyle{
            ImVec4 text_color;
            ImVec4 background_normal_color;
            ImVec4 background_hovered_color;
            ImVec4 background_active_color;
        };


        class EditorButtonComponent: public EditorUIComponent{
        public:
            EditorButtonComponent(const std::string& label);
            virtual void Render() override;
            void BindClickEvent(std::function<void()> callback);
        public:
            std::string label_;
            ButtonStyle button_style;

        private:
            std::function<void()> click_callback_;
        };
    }
}

#endif