#ifndef KPENGINE_RUNTIME_RENDER_SYSTEM_H
#define KPENGINE_RUNTIME_RENDER_SYSTEM_H

#include <memory>
#include "runtime/render/shader_pool.h"

namespace kpengine{
    class RenderScene;
    class RenderCamera;
    class TexturePool;
    class Gizmos;

    class RenderSystem{
    public:
        RenderSystem();
        ~RenderSystem();
        void Initialize();
        void PostInitialize();
        void Tick(float delta_time);
        
        RenderScene* GetRenderScene() {return render_scene_.get();}
        RenderCamera* GetRenderCamera() {return render_camera_.get();}
        ShaderPool* GetShaderPool() {return shader_pool_.get();}
        TexturePool* GetTexturePool(){return texture_pool_.get();}
        std::string GetCurrentShaderMode() const{return current_shader_mode_;}
        const int* GetTriangleCountRef() const{return &triangle_count_this_frame_;}

        void SetCurrentShaderMode(const std::string& target);
        void SetVisibleAxis(std::shared_ptr<Gizmos> axis);
        
    private:
        std::shared_ptr<RenderCamera> render_camera_;
        std::unique_ptr<RenderScene> render_scene_;
        std::unique_ptr<ShaderPool> shader_pool_;
        std::unique_ptr<TexturePool> texture_pool_;

    private:
        std::string current_shader_mode_;
        int triangle_count_this_frame_;
    };
}

#endif