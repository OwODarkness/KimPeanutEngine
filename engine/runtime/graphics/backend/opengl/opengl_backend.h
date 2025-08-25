#ifndef KPENGINE_RUNTIME_GRAPHICS_OPENGL_BACKEND_H
#define KPENGINE_RUNTIME_GRAPHICS_OPENGL_BACKEND_H

#include "common/render_backend.h"

namespace kpengine::graphics{
    class OpenglBackend: public RenderBackend{
    public:
        virtual void Initialize() override;
        virtual void BeginFrame() override;
        virtual void EndFrame() override;
    };
}

#endif