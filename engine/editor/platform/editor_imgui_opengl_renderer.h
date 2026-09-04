#ifndef KPENGINE_EDITOR_IMGUI_OPENGL_RENDERER_H
#define KPENGINE_EDITOR_IMGUI_OPENGL_RENDERER_H

#include "editor/platform/editor_imgui_renderer.h"

namespace kpengine::editor
{

    class EditorImguiOpenglRenderer : public IEditorImguiRenderer
    {
    public:
        ~EditorImguiOpenglRenderer() = default;

        bool Initialize(graphics::IEditorPresentationBridge *presentation_bridge) override;
        void Shutdown() override;

        void NewFrame() override;
        void Render() override;
        void SetBackgroundColor(const LogColor &color) override;
        ImTextureID GetTextureID(const graphics::RenderTargetView &view) override;
        void DrawSceneImage(ImTextureID texture_id, const ImVec2 &size) override;

    private:
        LogColor background_color_{0.1f, 0.1f, 0.1f, 1.f};
        bool imgui_backend_initialized_ = false;
    };

}

#endif
