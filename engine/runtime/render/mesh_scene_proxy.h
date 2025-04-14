#ifndef KPENGINE_RUNTIME_MESH_SCENE_PROXY_H
#define KPENGINE_RUNTIME_MESH_SCENE_PROXY_H

#include <vector>

#include "primitive_scene_proxy.h"
#include "mesh_section.h"



namespace kpengine{
    class MeshSceneProxy: public PrimitiveSceneProxy{
    public:
        virtual void Draw(std::shared_ptr<RenderShader> shader) override;
    public:
        unsigned int vao_;
        std::vector<MeshSection> mesh_sections;
    };
}

#endif //KPENGINE_RUNTIME_MESH_SCENE_PROXY_H