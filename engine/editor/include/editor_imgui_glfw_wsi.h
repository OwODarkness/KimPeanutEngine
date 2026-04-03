#ifndef KPENGINE_EDITOR_IMGUI_GLFW_WSI_H
#define KPENGINE_RDITOR_IMGUI_GLFW_WSI_H

#include "editor_imgui_wsi.h"
#include "base/type.h"

namespace kpengine::editor{
    class EditorImguiGLFWWSI: public IEditorImguiWSI{
    public:
         ~EditorImguiGLFWWSI() = default;
        virtual void Initialize(WindowHandle handle, GraphicsAPIType type) override;
        virtual void Shutdown() override;
        virtual void NewFrame() override;
    };
}

#endif