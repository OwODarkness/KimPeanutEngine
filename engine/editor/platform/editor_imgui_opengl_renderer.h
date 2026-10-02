#ifndef KPENGINE_EDITOR_IMGUI_OPENGL_RENDERER_H
#define KPENGINE_EDITOR_IMGUI_OPENGL_RENDERER_H

#include <string>

#include "editor/platform/editor_imgui_rhi_renderer.h"
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
        void SetBloomShaders(const data::ShaderData *vertex,
                             const data::ShaderData *fragment) override;
        ImTextureID GetTextureID(const graphics::RenderTargetView &view) override;
        void DrawSceneImage(ImTextureID texture_id, const ImVec2 &size) override;

    private:
        graphics::IEditorPresentationBridge *presentation_bridge_ = nullptr;
        EditorImguiRhiRenderer rhi_renderer_;
        LogColor background_color_{0.1f, 0.1f, 0.1f, 1.f};
        std::string last_glow_diagnostic_;
        std::string last_rhi_diagnostic_;
        bool imgui_backend_initialized_ = false;
    };

}

#endif
