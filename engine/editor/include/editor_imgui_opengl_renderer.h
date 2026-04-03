#ifndef KPENGINE_EDITOR_IMGUI_OPENGL_RENDERER_H
#define KPENGINE_EDITOR_IMGUI_OPENGL_RENDERER_H

#include "editor_imgui_renderer.h"

namespace kpengine::editor
{

    class EditorImguiOpenglRenderer : public IEditorImguiRenderer
    {
    public:
        ~EditorImguiOpenglRenderer() = default;

        void Initialize(GraphicsContext context) override;
        void Shutdown() override;

        void NewFrame() override;
        void Render() override;
    };

}

#endif