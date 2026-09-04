#ifndef KPENGINE_RUNTIME_GRAPHICS_OPENGL_EDITOR_BRIDGE_H
#define KPENGINE_RUNTIME_GRAPHICS_OPENGL_EDITOR_BRIDGE_H

#include "graphics/backend/common/editor_presentation_bridge.h"

namespace kpengine::graphics
{
    class OpenglEditorBridge final : public IEditorPresentationBridge
    {
    public:
        GraphicsAPIType GetGraphicsAPI() const override
        {
            return GraphicsAPIType::GRAPHICS_API_OPENGL;
        }
    };
}

#endif
