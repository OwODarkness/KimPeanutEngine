#ifndef KPENGINE_RUNTIME_RENDER_SYSTEM_H
#define KPENGINE_RUNTIME_RENDER_SYSTEM_H

#include <memory>

namespace kpengine{
    class RenderScene;
    class RenderCamera;

    class RenderSystem{
    public:
        RenderSystem() = default;
        void Initialize();
        void Tick(float DeltaTime);
        RenderScene* GetRenderScene() {return render_scene_.get();}
        RenderCamera* GetRenderCamera() {return render_camera_.get();}
    private:
        std::shared_ptr<RenderScene> render_scene_;
        std::shared_ptr<RenderCamera> render_camera_;
    };
}

#endif