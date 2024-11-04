#ifndef RUNTIME_RENDER_SYSTEM_H
#define RUNTIME_RENDER_SYSTEM_H

#include <memory>

namespace kpengine{
    class RenderScene;

    class RenderSystem{
    public:
        RenderSystem() = default;
        void Initialize();
        void Tick(float DeltaTime);
        std::shared_ptr<RenderScene> GetRenderScene() const{return render_scene_;}
    private:
        std::shared_ptr<RenderScene> render_scene_;
    };
}

#endif