#include "aabb_debugger.h"

#include <glad/glad.h>
#include "runtime/core/utility/gl_vertex_array_guard.h"
#include "runtime/core/utility/gl_vertex_buffer_guard.h"
#include "runtime/core/utility/gl_element_buffer_guard.h"
#include "runtime/runtime_header.h"
#include "runtime/render/render_shader.h"
namespace kpengine{

void AABBDebugger::Initialize(const AABB& aabb)
{
    shader_ = runtime::global_runtime_context.render_system_->GetShaderPool()->GetShader(SHADER_CATEGORY_DEBUGCUBIC);
    Vector3f min_ = aabb.min_;
    Vector3f max_ = aabb.max_;

    Vector3f corners[8] = 
    {
            {min_.x_, min_.y_, min_.z_},
            {max_.x_, min_.y_, min_.z_},
            {max_.x_, max_.y_, min_.z_},
            {min_.x_, max_.y_, min_.z_},

            {min_.x_, min_.y_, max_.z_},
            {max_.x_, min_.y_, max_.z_},
            {max_.x_, max_.y_, max_.z_},
           {min_.x_, max_.y_, max_.z_}
    };

        unsigned int aabb_indices[] = {
            // Bottom face
            0, 1,
           1, 2,
            2, 3,
            3, 0,
            // Top face
            4, 5,
            5, 6,
           6, 7,
            7, 4,
            // Vertical edges
            0, 4,
            1, 5,
            2, 6,
            3, 7};

            glGenVertexArrays(1, &vao_);
            glGenBuffers(1, &vbo_);
            glGenBuffers(1, &ebo_);
            GlVertexArrayGuard vao_guard(vao_);
            
            GlVertexBufferGuard vbo_guard(vbo_);
            glBufferData(GL_ARRAY_BUFFER, sizeof(corners), corners, GL_STATIC_DRAW);
            GlElementBufferGuard ebo_guard(ebo_);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(aabb_indices), aabb_indices, GL_STATIC_DRAW);
    
             glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vector3f), (void*)0);

}

void AABBDebugger::Debug(const Matrix4f& mat) const
{
    if(!is_visiable_)
    {
        return ;
    }
    GlVertexArrayGuard vao_guard(vao_);
    GlElementBufferGuard ebo_guard(ebo_);
    shader_->UseProgram();
    shader_->SetMat("model", mat[0]);
    glDrawElements(GL_LINES, 24, GL_UNSIGNED_INT, 0);
}

}