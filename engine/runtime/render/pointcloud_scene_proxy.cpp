#include "pointcloud_scene_proxy.h"

#include <glad/glad.h>

#include "runtime/render/render_shader.h"
#include "runtime/render/render_pointcloud_resource.h"
#include "runtime/core/utility/gl_vertex_array_guard.h"
#include "runtime/render/render_material.h"

namespace kpengine{
    void PointCloudSceneProxy::Initialize()
    {
        PrimitiveSceneProxy::Initialize();

        unsigned int shader_id = pointcloud_resource_ref_->material_->shader_->GetShaderProgram();

        unsigned int uniform_block_index = glGetUniformBlockIndex(shader_id, "CameraMatrices");
        glUniformBlockBinding(shader_id, uniform_block_index, 0);
    }
    
    void PointCloudSceneProxy::Draw(std::shared_ptr<RenderShader> shader)
    {
        GlVertexArrayGuard vao_guard(vao_);
        Matrix4f transform_mat = Matrix4f::MakeTransformMatrix(transfrom_).Transpose();
    
        std::shared_ptr<RenderShader> cur_shader = pointcloud_resource_ref_->material_->shader_;

        cur_shader->UseProgram();
        cur_shader->SetMat(SHADER_PARAM_MODEL_TRANSFORM, transform_mat[0]);
        glDrawArrays(GL_POINTS, 0, pointcloud_resource_ref_->vertex_buffer_.size() * sizeof(Vector3f));
    }
}