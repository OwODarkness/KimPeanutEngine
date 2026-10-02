#ifndef KPENGINE_RUNTIME_GRAPHICS_OPENGL_EDITOR_BRIDGE_H
#define KPENGINE_RUNTIME_GRAPHICS_OPENGL_EDITOR_BRIDGE_H

#include <functional>

#include "graphics/backend/common/editor_presentation_bridge.h"

namespace kpengine::graphics
{
    class RenderBackend;

    class OpenglEditorBridge final : public IEditorPresentationBridge
    {
    public:
        void BindBackend(RenderBackend &backend) noexcept { backend_ = &backend; }
        bool ExecuteRhiFrame(const RhiFrameCallback &record_frame) override;

        GraphicsAPIType GetGraphicsAPI() const override
        {
            return GraphicsAPIType::GRAPHICS_API_OPENGL;
        }

    private:
        RenderBackend *backend_ = nullptr;
    };
}

#endif
