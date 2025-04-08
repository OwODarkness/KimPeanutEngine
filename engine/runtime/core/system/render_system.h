#ifndef KPENGINE_RUNTIME_RENDER_SYSTEM_H
#define KPENGINE_RUNTIME_RENDER_SYSTEM_H

#include <memory>

namespace kpengine{
    class RenderScene;
    class RenderCamera;
   class ShaderPool;

    class RenderSystem{
    public:
        RenderSystem();
        ~RenderSystem();
        void Initialize();
        void Tick(float DeltaTime);
        RenderScene* GetRenderScene() {return render_scene_.get();}
        RenderCamera* GetRenderCamera() {return render_camera_.get();}
        ShaderPool* GetShaderPool() {return shader_pool_.get();}
        
    private:
        std::shared_ptr<RenderCamera> render_camera_;
        std::unique_ptr<RenderScene> render_scene_;
        std::unique_ptr<ShaderPool> shader_pool_;
    };
}

#endif