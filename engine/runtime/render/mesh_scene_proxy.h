#ifndef KPENGINE_RUNTIME_MESH_SCENE_PROXY_H
#define KPENGINE_RUNTIME_MESH_SCENE_PROXY_H

#include <vector>

#include "primitive_scene_proxy.h"
#include "mesh_section.h"

#define SHADER_PARAM_MODEL_TRANSFORM "model"

namespace kpengine{
    class MeshSceneProxy: public PrimitiveSceneProxy{
    public:
        MeshSceneProxy();
        void Draw(std::shared_ptr<RenderShader> shader) override;
        void Initialize() override;
    public:
        unsigned int vao_;
        std::vector<MeshSection> mesh_sections_;
    private:
        std::shared_ptr<RenderShader> current_shader_;
    };
}

#endif //KPENGINE_RUNTIME_MESH_SCENE_PROXY_H