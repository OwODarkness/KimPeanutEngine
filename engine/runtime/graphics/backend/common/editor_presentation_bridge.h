#ifndef KPENGINE_RUNTIME_GRAPHICS_EDITOR_PRESENTATION_BRIDGE_H
#define KPENGINE_RUNTIME_GRAPHICS_EDITOR_PRESENTATION_BRIDGE_H

#include <functional>

#include "base/type.h"

namespace kpengine::graphics
{
    class CommandRecorder;
    class RenderBackend;

    // Borrowed, backend-owned capability used only by the editor presentation
    // adapters. API-specific code may downcast this type; common Render code
    // never receives the native graphics context.
    class IEditorPresentationBridge
    {
    public:
        using RhiFrameCallback = std::function<bool(RenderBackend &, CommandRecorder &)>;

        virtual ~IEditorPresentationBridge() = default;
        virtual GraphicsAPIType GetGraphicsAPI() const = 0;

        // Grants borrowed common resource/recording access only while a backend
        // frame is active. The caller must invoke this outside any rendering
        // scope and balance every target/presentation scope before returning.
        virtual bool ExecuteRhiFrame(const RhiFrameCallback &record_frame)
        {
            (void)record_frame;
            return false;
        }
    };
}

#endif
