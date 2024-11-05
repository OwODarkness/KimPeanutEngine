#ifndef KPENGINE_EDITOR_TOOLTIP_COMPONENT_H
#define KPENGINE_EDITOR_TOOLTIP_COMPONENT_H

#include <string>

#include "editor/include/editor_ui_component.h"

namespace kpengine
{
    namespace ui
    {
        class EditorTooltipComponent : public EditorUIComponent
        {
        public:
            EditorTooltipComponent(const std::string &content, ImVec4 color = ImVec4(1.f, 1.f, 1.f, 1.f));
            EditorTooltipComponent(EditorUIComponent *inner_comp);
            virtual ~EditorTooltipComponent();

            virtual void Render() override;

        private:
            EditorUIComponent *inner_comp_;
        };
    }
}

#endif