#ifndef KPENGINE_EDITOR_UI_COMPONENT_H
#define KPENGINE_EDITOR_UI_COMPONENT_H

#include <imgui/imgui.h>

namespace kpengine::editor
{
    class EditorUIComponent
    {
    public:
        virtual ~EditorUIComponent() = default;
        virtual void Render() = 0;
    };

}

#endif