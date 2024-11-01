#ifndef RUNTIME_RENDER_SCENE_H
#define RUNTIME_RENDER_SCENE_H

#include <memory>

namespace kpengine{
    class FrameBuffer;
    class RenderObject;

    class RenderScene{
    public:
        RenderScene() = default;

        void Initialize();

        void BeginDraw();

        void Render();

        void EndDraw();

        
    public:
        std::shared_ptr<FrameBuffer> scene_;//frame buffer

        std::shared_ptr<RenderObject> render_object_;
    };
}

#endif