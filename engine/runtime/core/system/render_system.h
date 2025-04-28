#ifndef KPENGINE_RUNTIME_RENDER_SYSTEM_H
#define KPENGINE_RUNTIME_RENDER_SYSTEM_H

#include <memory>
#include "runtime/render/shader_pool.h"

namespace kpengine{
    class RenderScene;
    class RenderCamera;

    class RenderSystem{
    public:
        RenderSystem();
        ~RenderSystem();
        void Initialize();
        void Tick(float delta_time);
        RenderScene* GetRenderScene() {return render_scene_.get();}
        RenderCamera* GetRenderCamera() {return render_camera_.get();}
        ShaderPool* GetShaderPool() {return shader_pool_.get();}

        void SetCurrentShaderMode(const std::string& target);
        std::string GetCurrentShaderMode() const{return current_shader_mode_;}
        
    private:
        std::shared_ptr<RenderCamera> render_camera_;
        std::unique_ptr<RenderScene> render_scene_;
        std::unique_ptr<ShaderPool> shader_pool_;
    private:
        std::string current_shader_mode_;
    };
}

#endif