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
    void MeshSceneProxy::Draw(std::shared_ptr<RenderShader> shader)
    {

        Matrix4f transform_mat = Matrix4f::MakeTransformMatrix(transfrom_).Transpose();
        {
            GlVertexArrayGuard vao_guard(geometry_buffer_->vao);
            GlElementBufferGuard ebo_guard(geometry_buffer_->ebo);
            if (shader)
            {
                shader->UseProgram();
                for (std::vector<MeshSection>::iterator iter = mesh_resourece_ref_->mesh_sections_.begin(); iter != mesh_resourece_ref_->mesh_sections_.end(); iter++)
                {
                    shader->SetMat(SHADER_PARAM_MODEL_TRANSFORM, transform_mat[0]);
                    iter->material->Render(shader);
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
                        current_shader->SetVec3("view_position", view_pos_);
                        current_shader->SetMat("light_space_matrix", light_space_);
                        current_shader->SetInt("shadow_map", 15);
                        current_shader->SetFloat("far_plane", 25.f);
                        current_shader->SetInt("point_shadow_map", 14);
                        current_shader->SetMat(SHADER_PARAM_MODEL_TRANSFORM, transform_mat[0]);
                    }
                    iter->material->Render(current_shader);

                    current_shader->SetInt("irradiance_map", 10);
                    glActiveTexture(GL_TEXTURE10);
                    glBindTexture(GL_TEXTURE_CUBE_MAP, irradiance_map_handle_);

                    current_shader->SetInt("prefilter_map", 11);
                    glActiveTexture(GL_TEXTURE11);
                    glBindTexture(GL_TEXTURE_CUBE_MAP, prefilter_map_handle_);

                    current_shader->SetInt("brdf_map", 12);
                    glActiveTexture(GL_TEXTURE12);
                    glBindTexture(GL_TEXTURE_2D, brdf_map_handle_);

                    glDrawElements(GL_TRIANGLES, iter->index_count, GL_UNSIGNED_INT, (void *)(iter->index_start * sizeof(unsigned int)));
                }
            }
        }
        current_shader_id_ = 0;
        aabb_debugger_->Debug(transform_mat);
    }

    void MeshSceneProxy::Initialize()
    {
        PrimitiveSceneProxy::Initialize();
        assert(mesh_resourece_ref_ != nullptr);
        mesh_resourece_ref_->Initialize();
        aabb_debugger_ = std::make_shared<AABBDebugger>();
        aabb_debugger_->Initialize(mesh_resourece_ref_->aabb_);
        aabb_debugger_->is_visiable_ = false;
    }

    AABB MeshSceneProxy::GetAABB()
    {
        return mesh_resourece_ref_->aabb_;
    }
}