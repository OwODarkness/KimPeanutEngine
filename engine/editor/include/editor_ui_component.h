#ifndef KPENGINE_EDITOR_UI_COMPONENT_H
#define KPENGINE_EDITOR_UI_COMPONENT_H

#include <string>

#include <imgui/imgui.h>
#include <imgui/imgui_impl_glfw.h>
#include <imgui/imgui_impl_opengl3.h>

namespace kpengine
{
    namespace ui
    {
        class EditorUIComponent
        {
        public:
            EditorUIComponent();
            virtual ~EditorUIComponent();

            virtual void Render() = 0;
        };



    }
}

#endif