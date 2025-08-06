#include "directional_shadow_caster.h"
#include <glad/glad.h>
#include "runtime/runtime_global_context.h"
#include "runtime/core/system/render_system.h"
#include "runtime/render/shader_pool.h"
#include "runtime/render/render_shader.h"
#include "runtime/render/primitive_scene_proxy.h"
#include "runtime/render/render_light.h"

namespace kpengine
{

    void DirectionalShadowCaster::Initialize()
    {
        shader_ = runtime::global_runtime_context.render_system_->GetShaderPool()->GetShader(SHADER_CATEGORY_DIRECTIONALSHADOW);

        glGenFramebuffers(1, &fbo_);
        glGenTextures(1, &shadow_map_);

        glBindTexture(GL_TEXTURE_2D, shadow_map_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, width_, height_, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        GLfloat borderColor[] = {1.0, 1.0, 1.0, 1.0};
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);

        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadow_map_, 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    void DirectionalShadowCaster::AddLight(std::shared_ptr<LightData> light)
    {
        std::shared_ptr<DirectionalLightData> directional =  std::dynamic_pointer_cast<DirectionalLightData>(light);
        if (directional)
        {
            light_ = directional;
        }
    }

    void DirectionalShadowCaster::Render(const std::vector<std::shared_ptr<PrimitiveSceneProxy>> &proxies)
    {
        if (!shader_ || !light_)
        {
            return;
        }
        glViewport(0, 0, width_, height_);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        glCullFace(GL_FRONT);
        glClear(GL_DEPTH_BUFFER_BIT);

        Matrix4f light_space_matrix = CalculateLighSpaceMatrix(-200.f * light_->direction);
        shader_->UseProgram();
        shader_->SetMat("light_space_matrix", light_space_matrix.Transpose()[0]);
        for (const auto &proxy : proxies)
        {
            if (proxy->IsVisible())
            {
                proxy->Draw({.shader = shader_});
            }
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glCullFace(GL_BACK);
    }

    Matrix4f DirectionalShadowCaster::CalculateLighSpaceMatrix(const Vector3f &light_position)
    {
        Matrix4f proj = Matrix4f::MakeOrthProjMatrix(-10.0f, 10.0f, -10.0f, 10.0f, near_plane_, far_plane_);
        Matrix4f view = Matrix4f::MakeCameraMatrix(light_position, Vector3f(0.f, -1.f, 0.f), Vector3f(0.f, 1.f, 0.f));
        return proj * view;
    }
}