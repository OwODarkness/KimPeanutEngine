#include "outline_effect.h"

#include <glad/glad.h>
#include "runtime/core/utility/gl_vertex_array_guard.h"
#include "runtime/runtime_global_context.h"
#include "render_system.h"
#include "shader_pool.h"
#include "render_shader.h"

namespace kpengine
{
    OutlineEffect::OutlineEffect() : shader_(nullptr)
    {
    }

    void OutlineEffect::Initialize()
    {
        float quadVertices[] = {
            // positions   // texCoords
            -1.0f, 1.0f, 0.0f, 1.0f,  // top-left
            -1.0f, -1.0f, 0.0f, 0.0f, // bottom-left
            1.0f, -1.0f, 1.0f, 0.0f,  // bottom-right

            -1.0f, 1.0f, 0.0f, 1.0f, // top-left
            1.0f, -1.0f, 1.0f, 0.0f, // bottom-right
            1.0f, 1.0f, 1.0f, 1.0f   // top-right
        };

        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);

        glBindVertexArray(vao_);

        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

        // position
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
        glEnableVertexAttribArray(0);
        // texCoord
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));
        glEnableVertexAttribArray(1);

        glBindVertexArray(0);

        border_color_ = Vector3f(1.f, 1.f, 0.f);
        shader_ = runtime::global_runtime_context.render_system_->GetShaderPool()->GetShader(SHADER_CATEGORY_OUTLINING);
    }

    void OutlineEffect::Execute(const PostProcessContext &context)
    {
        if (shader_ == nullptr)
            return;
        shader_->UseProgram();
        shader_->SetInt("object_id", 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, context.g_object_id);
        shader_->SetVec2("texel_size", context.texel_size.Data());
        shader_->SetVec3("border_color", border_color_.Data());
        GlVertexArrayGuard vao_guard(vao_);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }
}