#ifndef KPENGINE_RUNTIME_RENDER_SCENE_H
#define KPENGINE_RUNTIME_RENDER_SCENE_H

#include <memory>

#include "runtime/render/render_light.h"

namespace kpengine{
    class FrameBuffer;
    class RenderObject;
    class RenderCamera;
    class RenderScene{
    public:
        RenderScene() = default;

        void Initialize(std::shared_ptr<RenderCamera> camera);

        void BeginDraw();

        void Render();

        void EndDraw();

    public:
        std::shared_ptr<FrameBuffer> scene_;//frame buffer

        std::shared_ptr<RenderObject> render_object_;

        std::shared_ptr<RenderCamera> render_camera_;

        AmbientLight ambient_light_;

        PointLight point_light_;
    };
}

#endif