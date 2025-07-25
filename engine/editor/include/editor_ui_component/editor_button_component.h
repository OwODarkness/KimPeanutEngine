#ifndef KPENGINE_EDITOR_BUTTON_COMPONENT_H
#define KPENGINE_EDITOR_BUTTON_COMPONENT_H

#include <string>
#include <functional>
#include "editor/include/editor_ui_component.h"

#include "runtime/core/system/delegate.h"

DECLARE_DELEGATE(FOnButtonClickNotify)

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
        public:
            std::string label_;
            ButtonStyle button_style;

            FOnButtonClickNotify on_click_notify_;
        };
    }
}

#endif