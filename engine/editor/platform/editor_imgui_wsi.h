#ifndef KPENGINE_EDITOR_IMGUI_WSI_H
#define KPENGINE_RDITOR_IMGUI_WSI_H

#include "base/type.h"

namespace kpengine::editor{
    class IEditorImguiWSI{
    public:
        virtual ~IEditorImguiWSI() = default;
        virtual void Initialize(WindowHandle handle, GraphicsAPIType graphics_type) = 0;
        virtual void Shutdown() = 0;
        virtual void NewFrame() = 0;
    };
}

#endif