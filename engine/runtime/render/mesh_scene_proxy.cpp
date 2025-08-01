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
                                       mesh_resourece_ref_(nullptr),
                                       current_shader_id_(0)
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

    void MeshSceneProxy::Draw(const RenderContext &context)
    {
        Matrix4f transform_mat = Matrix4f::MakeTransformMatrix(transfrom_);
        glStencilMask(0x00);
        aabb_debugger_->Debug(transform_mat.Transpose());
        DrawRenderable(context, transform_mat.Transpose());
    }

    void MeshSceneProxy::DrawGeometryPass(const RenderContext &context)
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
            context.shader->SetInt("object_id", id_);
            iter->material->Render(context.shader, 0);
            glDrawElements(GL_TRIANGLES, iter->index_count, GL_UNSIGNED_INT, (void *)(iter->index_start * sizeof(unsigned int)));
        }
    }

    void MeshSceneProxy::DrawRenderable(const RenderContext &context, const Matrix4f &transform_mat)
    {
        {
            GlVertexArrayGuard vao_guard(geometry_buffer_->vao);
            GlElementBufferGuard ebo_guard(geometry_buffer_->ebo);
            if (context.shader)
            {
                context.shader->UseProgram();
                for (std::vector<MeshSection>::iterator iter = mesh_resourece_ref_->mesh_sections_.begin(); iter != mesh_resourece_ref_->mesh_sections_.end(); iter++)
                {
                    context.shader->SetMat(SHADER_PARAM_MODEL_TRANSFORM, transform_mat[0]);
                    iter->material->Render(context.shader, 0);
                    glDrawElements(GL_TRIANGLES, iter->index_count, GL_UNSIGNED_INT, (void *)(iter->index_start * sizeof(unsigned int)));
                }
            }
            else
            {

                for (std::vector<MeshSection>::iterator iter = mesh_resourece_ref_->mesh_sections_.begin(); iter != mesh_resourece_ref_->mesh_sections_.end(); iter++)
                {
                    unsigned int new_shader_id = iter->material->shader_->GetShaderProgram();
                    std::shared_ptr<RenderShader> current_shader = iter->material->shader_;

                    current_shader->UseProgram();
                    if (new_shader_id != current_shader_id_)
                    {
                        current_shader_id_ = new_shader_id;
                        current_shader->SetVec3("view_position", context.view_position);
                        current_shader->SetMat("light_space_matrix", light_space_);
                        current_shader->SetFloat("far_plane", context.far_plane);
                        current_shader->SetMat(SHADER_PARAM_MODEL_TRANSFORM, transform_mat[0]);

                        int used_count = iter->material->Render(current_shader, 0);

                        current_shader->SetInt("shadow_map", 10);
                        glActiveTexture(GL_TEXTURE10);
                        glBindTexture(GL_TEXTURE_2D, context.directional_shadow_map);

                        current_shader->SetInt("point_shadow_map", 11);
                        glActiveTexture(GL_TEXTURE11);
                        glBindTexture(GL_TEXTURE_CUBE_MAP, context.point_shadow_map);

                        current_shader->SetInt("irradiance_map", 12);
                        glActiveTexture(GL_TEXTURE12);
                        glBindTexture(GL_TEXTURE_CUBE_MAP, context.irradiance_map);

                        current_shader->SetInt("prefilter_map", 13);
                        glActiveTexture(GL_TEXTURE13);
                        glBindTexture(GL_TEXTURE_CUBE_MAP, context.prefilter_map);

                        current_shader->SetInt("brdf_map", 14);
                        glActiveTexture(GL_TEXTURE14);
                        glBindTexture(GL_TEXTURE_2D, context.brdf_map);

                        current_shader->SetInt("g_position", 15);
                        glActiveTexture(GL_TEXTURE15);
                        glBindTexture(GL_TEXTURE_2D, context.g_position);

                        current_shader->SetInt("g_normal", 16);
                        glActiveTexture(GL_TEXTURE16);
                        glBindTexture(GL_TEXTURE_2D, context.g_normal);

                        current_shader->SetInt("g_albedo", 17);
                        glActiveTexture(GL_TEXTURE17);
                        glBindTexture(GL_TEXTURE_2D, context.g_albedo);

                        current_shader->SetInt("g_material", 18);
                        glActiveTexture(GL_TEXTURE18);
                        glBindTexture(GL_TEXTURE_2D, context.g_material);
                    }

                    glDrawElements(GL_TRIANGLES, iter->index_count, GL_UNSIGNED_INT, (void *)(iter->index_start * sizeof(unsigned int)));
                }
            }
        }
        current_shader_id_ = 0;
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