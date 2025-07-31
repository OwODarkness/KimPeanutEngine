#include "render_axis.h"
#include <glad/glad.h>
#include "runtime/core/utility/gl_vertex_array_guard.h"
#include "runtime/core/utility/gl_vertex_buffer_guard.h"
#include "runtime/core/utility/gl_element_buffer_guard.h"
#include "runtime/runtime_header.h"
#include "runtime/render/render_shader.h"

namespace kpengine
{
    void RenderAxis::Initialize()
    {
        shader_ = runtime::global_runtime_context.render_system_->GetShaderPool()->GetShader(SHADER_CATEGORY_AXIS);
        struct Vertex
        {
            Vector3f pos;
            Vector3f color;
        };
        Vertex axis_lines[] = {
            // X axis (red)
            {{0, 0, 0}, {1, 0, 0}},
            {{1, 0, 0}, {1, 0, 0}},

            // Y axis (green)
            {{0, 0, 0}, {0, 1, 0}},
            {{0, 1, 0}, {0, 1, 0}},

            // Z axis (blue)
            {{0, 0, 0}, {0, 0, 1}},
            {{0, 0, 1}, {0, 0, 1}},
        };

        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
        GlVertexArrayGuard vao_guard(vao_);

        GlVertexBufferGuard vbo_guard(vbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(axis_lines), axis_lines, GL_STATIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)(offsetof(Vertex, color)));
    }

    void RenderAxis::SetModelTransform(const Matrix4f &model)
    {
        model_mat_ = model;
    }

    void RenderAxis::Draw()
    {
        glDisable(GL_DEPTH_TEST);
        GlVertexArrayGuard vao_guard(vao_);
        shader_->UseProgram();
        shader_->SetMat("model", model_mat_[0]);
        glDrawArrays(GL_LINES, 0, 6);
        glEnable(GL_DEPTH_TEST);
    }
}