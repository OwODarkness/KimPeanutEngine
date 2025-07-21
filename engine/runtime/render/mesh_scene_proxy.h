#ifndef KPENGINE_RUNTIME_MESH_SCENE_PROXY_H
#define KPENGINE_RUNTIME_MESH_SCENE_PROXY_H


#include "primitive_scene_proxy.h"
#include "mesh_section.h"
#include <memory>

#define SHADER_PARAM_MODEL_TRANSFORM "model"

namespace kpengine{
    class RenderMeshResource;
    class AABBDebugger;
    class MeshSceneProxy: public PrimitiveSceneProxy{
    public:
        MeshSceneProxy();
        void Draw(std::shared_ptr<RenderShader> shader) override;
        void Initialize() override;
        AABB GetAABB() override;
    public:
        struct GeometryBuffer* geometry_buffer_;
        RenderMeshResource* mesh_resourece_ref_;
    private:
        unsigned int current_shader_id_;
        bool do_once = true;
        //aabb debug usage
        std::shared_ptr<AABBDebugger> aabb_debugger_;
    };
}

#endif //KPENGINE_RUNTIME_MESH_SCENE_PROXY_H