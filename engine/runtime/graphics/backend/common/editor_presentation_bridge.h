#ifndef KPENGINE_RUNTIME_GRAPHICS_EDITOR_PRESENTATION_BRIDGE_H
#define KPENGINE_RUNTIME_GRAPHICS_EDITOR_PRESENTATION_BRIDGE_H

#include "base/type.h"

namespace kpengine::graphics
{
    // Borrowed, backend-owned capability used only by the editor presentation
    // adapters. API-specific code may downcast this type; common Render code
    // never receives the native graphics context.
    class IEditorPresentationBridge
    {
    public:
        virtual ~IEditorPresentationBridge() = default;
        virtual GraphicsAPIType GetGraphicsAPI() const = 0;
    };
}

#endif
