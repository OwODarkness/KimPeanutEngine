#ifndef KPENGINE_EDITOR_IMGUI_RENDERER_H
#define KPENGINE_EDITOR_IMGUI_RENDERER_H

#include <imgui.h>

#include "editor/settings/editor_settings.h"
#include "graphics/backend/common/editor_presentation_bridge.h"
#include "graphics/backend/common/render_target.h"

namespace kpengine::editor{
    class IEditorImguiRenderer{
    public:
        virtual ~IEditorImguiRenderer() = default;
        virtual bool Initialize(graphics::IEditorPresentationBridge *presentation_bridge) = 0;
        virtual void Shutdown() = 0;

        virtual void NewFrame() = 0;
        virtual void Render() = 0;
        virtual void SetBackgroundColor(const LogColor &color) = 0;
        virtual ImTextureID GetTextureID(const graphics::RenderTargetView &view) = 0;
        virtual void DrawSceneImage(ImTextureID texture_id, const ImVec2 &size) = 0;
    };
}

#endif
