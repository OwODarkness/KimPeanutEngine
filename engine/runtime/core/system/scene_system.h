#ifndef RUNTIME_SCENE_SYSTEM_H
#define RUNTIME_SCENE_SYSTEM_H

#include <memory>
#include <vector>


namespace kpengine{
    class FrameBuffer;
    class RenderObject;
    class SceneSystem{
    public:
        SceneSystem() = default;

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