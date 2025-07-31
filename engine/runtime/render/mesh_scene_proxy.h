#ifndef KPENGINE_RUNTIME_MESH_SCENE_PROXY_H
#define KPENGINE_RUNTIME_MESH_SCENE_PROXY_H


#include "primitive_scene_proxy.h"
#include "mesh_section.h"
#include "runtime/core/math/math_header.h"
#include <memory>

#define SHADER_PARAM_MODEL_TRANSFORM "model"

namespace kpengine{
    class RenderMeshResource;
    class AABBDebugger;
    class RenderShader;

    class MeshSceneProxy: public PrimitiveSceneProxy{
    public:
        MeshSceneProxy();
        void Initialize() override;
        void Draw(const RenderContext& context) override;
        void DrawGeometryPass(const RenderContext& context) override;
        void SetOutlineVisibility(bool visible);
        AABB GetAABB() override;
        ~MeshSceneProxy();
    private:
        void DrawOutline(const Matrix4f& transform_mat);
        void DrawRenderable(const RenderContext& context, const Matrix4f& transform_mat);
    public:
        struct GeometryBuffer* geometry_buffer_;
        RenderMeshResource* mesh_resourece_ref_;
    private:
        unsigned int current_shader_id_;
        bool do_once = true;
        //aabb debug usage
        std::unique_ptr<AABBDebugger> aabb_debugger_;
        std::shared_ptr<RenderShader> outlining_shader_;

        bool is_outline_visible = false;
    };
}

#endif //KPENGINE_RUNTIME_MESH_SCENE_PROXY_H