#ifndef KPENGINE_RUNTIME_SKYBOX_H
#define KPENGINE_RUNTIME_SKYBOX_H

#include <memory>

namespace kpengine{

    
    class RenderTextureCubeMap;
    class RenderShader;
    
    class Skybox{
    public:
        Skybox(std::shared_ptr<RenderShader> skybox_shader, std::shared_ptr<RenderTextureCubeMap> cubemap);
        void Initialize();
        void Render();
    private:
        std::shared_ptr<RenderShader> shader_;
        std::shared_ptr<RenderTextureCubeMap> cubemap_;

        unsigned int vbo_;
        unsigned int vao_;

        static const float skybox_vertices[108];
    };
}

#endif