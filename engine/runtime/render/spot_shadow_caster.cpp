#include "spot_shadow_caster.h"
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include "runtime/runtime_global_context.h"
#include "runtime/core/system/render_system.h"
#include "runtime/render/shader_pool.h"
#include "runtime/render/render_shader.h"
#include "runtime/render/primitive_scene_proxy.h"
#include "runtime/render/render_light.h"
namespace kpengine
{
    void SpotShadowCaster::Initialize()
    {
        shader_ = runtime::global_runtime_context.render_system_->GetShaderPool()->GetShader(SHADER_CATEGORY_DIRECTIONALSHADOW);
        near_plane_ = 0.1f;
        far_plane_ = 5.f;

        for (int i = 0; i < SPOT_MAX_SHADOW_MAP_NUM; i++)
        {
            glGenFramebuffers(1, &fbos_[i]);
            glGenTextures(1, &shadow_maps_[i]);

            glBindTexture(GL_TEXTURE_2D, shadow_maps_[i]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, width_, height_, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
            float borderColor[] = {1.0, 1.0, 1.0, 1.0};
            glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);

            glBindFramebuffer(GL_FRAMEBUFFER, fbos_[i]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadow_maps_[i], 0);
            glDrawBuffer(GL_NONE);
            glReadBuffer(GL_NONE);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }
    }
    void SpotShadowCaster::AddLight(std::shared_ptr<LightData> light)
    {
        std::shared_ptr<SpotLightData> point_light = std::dynamic_pointer_cast<SpotLightData>(light);
        if (point_light)
        {
            lights_.push_back(point_light);
        }
    }
    void SpotShadowCaster::Render(const std::vector<std::shared_ptr<PrimitiveSceneProxy>> &proxies)
    {
        if (!shader_)
        {
            return;
        }
        int current_shadow_count = 0;
        shader_->UseProgram();
        for (auto &light : lights_)
        {
            if (current_shadow_count < SPOT_MAX_SHADOW_MAP_NUM)
            {
                light_space_matrices[current_shadow_count] = CalculateLighSpaceMatrix(light);
                float* data = light_space_matrices[current_shadow_count].Transpose()[0];

                shader_->SetMat("light_space_matrix",data);
                glViewport(0, 0, width_, height_);
                glBindFramebuffer(GL_FRAMEBUFFER, fbos_[current_shadow_count]);
                glClear(GL_DEPTH_BUFFER_BIT);
                RenderContext shader_context{.shader = shader_};
                for (const auto &proxy : proxies)
                {
                    if (!proxy->IsVisible())
                    {
                        continue;
                    }
                    proxy->Draw(shader_context);
                }
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                light->shadow_map_index = current_shadow_count + 1;
                current_shadow_count++;
            }
            else
            {
                light->shadow_map_index = 0;
            }
        }
    }

    Matrix4f SpotShadowCaster::CalculateLighSpaceMatrix(const std::shared_ptr<SpotLightData> &light) const
    {
        if (!light)
            return {};
        Matrix4f view = Matrix4f::MakeCameraMatrix(light->position, light->direction, Vector3f(0.f, 1.f, 0.f));
        float fov = math::DegreeToRadian(light->outer_cutoff * 2.f) ;
        float aspect = static_cast<float>(width_) / static_cast<float>(height_);
        Matrix4f proj = Matrix4f::MakePerProjMatrix(fov, aspect, near_plane_, light->radius);
        return proj * view;
    }
}