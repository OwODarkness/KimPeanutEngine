#ifndef KPENGINE_RUNTIME_MESH_SCENE_PROXY_H
#define KPENGINE_RUNTIME_MESH_SCENE_PROXY_H

#include <memory>

#include "primitive_scene_proxy.h"
#include "mesh_section.h"
#include "math/math_header.h"

#define SHADER_PARAM_MODEL_TRANSFORM "model"

namespace kpengine{
    class RenderMeshResource;
    class AABBDebugger;
    class RenderShader;

    class MeshSceneProxy: public PrimitiveSceneProxy{
    public:
        MeshSceneProxy();
        void Initialize() override;
        void Draw(const RenderContext& context) const override;
        void DrawGeometryPass(const RenderContext& context) const override;
        void SetOutlineVisibility(bool visible);
        AABB GetAABB() override;
        ~MeshSceneProxy();
    private:
        void DrawRenderable(const RenderContext& context, const Matrix4f& transform_mat) const;
    public:
        struct GeometryBuffer* geometry_buffer_;
        RenderMeshResource* mesh_resourece_ref_;
    private:
        //aabb debug usage
        std::unique_ptr<AABBDebugger> aabb_debugger_;

        bool is_outline_visible = false;
    };
}

#endif //KPENGINE_RUNTIME_MESH_SCENE_PROXY_H