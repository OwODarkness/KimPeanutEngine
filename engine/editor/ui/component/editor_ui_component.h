#ifndef KPENGINE_EDITOR_UI_COMPONENT_H
#define KPENGINE_EDITOR_UI_COMPONENT_H

#include <imgui.h>

namespace kpengine::editor
{
    namespace ui { class UiContext; }
    class EditorUIComponent
    {
    public:
        virtual ~EditorUIComponent() = default;
        virtual void Render() = 0;
            virtual void RenderWithContext(ui::UiContext &context)
            {
                (void)context;
                Render();
            }
    };

}

#endif
