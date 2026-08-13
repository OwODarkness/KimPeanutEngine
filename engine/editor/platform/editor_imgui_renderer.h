#ifndef KPENGINE_EDITOR_IMGUI_RENDERER_H
#define KPENGINE_EDITOR_IMGUI_RENDERER_H

#include "base/type.h"

namespace kpengine::editor{
    class IEditorImguiRenderer{
    public:
        virtual ~IEditorImguiRenderer() = default;
        virtual void Initialize(GraphicsContext context) = 0;
        virtual void Shutdown() = 0;

        virtual void NewFrame() = 0;
        virtual void Render() = 0;
    };
}

#endif