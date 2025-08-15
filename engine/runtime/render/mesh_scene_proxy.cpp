#include "mesh_scene_proxy.h"

#include <glad/glad.h>

#include "runtime/runtime_header.h"
#include "runtime/render/render_mesh_resource.h"
#include "runtime/render/render_shader.h"
#include "runtime/render/render_material.h"
#include "runtime/core/utility/utility.h"
#include "runtime/core/log/logger.h"
#include "geometry_buffer.h"
#include "runtime/core/utility/aabb_debugger.h"

namespace kpengine
{
    MeshSceneProxy::MeshSceneProxy() : geometry_buffer_(nullptr),
                                       mesh_resourece_ref_(nullptr)
    {
    }

    void MeshSceneProxy::Initialize()
    {
        PrimitiveSceneProxy::Initialize();
        assert(mesh_resourece_ref_ != nullptr);
        mesh_resourece_ref_->Initialize();
        aabb_debugger_ = std::make_unique<AABBDebugger>();
        aabb_debugger_->Initialize(mesh_resourece_ref_->aabb_);
        aabb_debugger_->is_visiable_ = false;
    }

    void MeshSceneProxy::Draw(const RenderContext &context) const
    {
        Matrix4f transform_mat = Matrix4f::MakeTransformMatrix(transfrom_);
        aabb_debugger_->Debug(transform_mat.Transpose());
        DrawRenderable(context, transform_mat.Transpose());
    }

    void MeshSceneProxy::DrawGeometryPass(const RenderContext &context) const
    {
        if (!context.shader)
        {
            return;
        }
        GlVertexArrayGuard vao_guard(geometry_buffer_->vao);
        GlElementBufferGuard ebo_guard(geometry_buffer_->ebo);
        context.shader->UseProgram();
        Matrix4f transform_mat = Matrix4f::MakeTransformMatrix(transfrom_);
        for (std::vector<MeshSection>::iterator iter = mesh_resourece_ref_->mesh_sections_.begin(); iter != mesh_resourece_ref_->mesh_sections_.end(); iter++)
        {
            context.shader->SetMat("model", transform_mat.Transpose()[0]);
            context.shader->SetBool("is_outline_visible", is_outline_visible);
            context.shader->SetFloat("near_plane", context.near_plane);
            context.shader->SetFloat("far_plane", context.far_plane);
            context.shader->SetInt("object_id", id_);
            iter->material->Render(context.shader, 0);
            glDrawElements(GL_TRIANGLES, iter->index_count, GL_UNSIGNED_INT, (void *)(iter->index_start * sizeof(unsigned int)));
        }
    }

    void MeshSceneProxy::DrawRenderable(const RenderContext &context, const Matrix4f &transform_mat) const
    {
        if (!context.shader)
            return;
        // unsigned int current_shader_id = 0;
        GlVertexArrayGuard vao_guard(geometry_buffer_->vao);
        GlElementBufferGuard ebo_guard(geometry_buffer_->ebo);
        context.shader->UseProgram();
        for (std::vector<MeshSection>::iterator iter = mesh_resourece_ref_->mesh_sections_.begin(); iter != mesh_resourece_ref_->mesh_sections_.end(); iter++)
        {
            context.shader->SetMat(SHADER_PARAM_MODEL_TRANSFORM, transform_mat[0]);
            iter->material->Render(context.shader, 0);
            glDrawElements(GL_TRIANGLES, iter->index_count, GL_UNSIGNED_INT, (void *)(iter->index_start * sizeof(unsigned int)));
        }
    }

    void MeshSceneProxy::SetOutlineVisibility(bool visible)
    {
        is_outline_visible = visible;
    }

    AABB MeshSceneProxy::GetAABB()
    {
        return mesh_resourece_ref_->aabb_;
    }

    MeshSceneProxy::~MeshSceneProxy() = default;
}