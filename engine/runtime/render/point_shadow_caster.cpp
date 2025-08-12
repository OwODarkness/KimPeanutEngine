#include "point_shadow_caster.h"
#include <glad/glad.h>
#include <iostream>
#include "runtime/runtime_global_context.h"
#include "runtime/core/system/render_system.h"
#include "runtime/render/shader_pool.h"
#include "runtime/render/render_shader.h"
#include "runtime/render/primitive_scene_proxy.h"
#include "runtime/render/render_light.h"

namespace kpengine
{
    void PointShadowCaster::Initialize()
    {
        shader_ = runtime::global_runtime_context.render_system_->GetShaderPool()->GetShader(SHADER_CATEGORY_POINTSHADOW);
        near_plane_ = 1.f;
        far_plane_ = 25.f;

        for (int i = 0; i < POINT_MAX_SHADOW_MAP_NUM; i++)
        {
            glGenFramebuffers(1, &fbos_[i]);
            glGenTextures(1, &shadow_maps_[i]);

            glBindTexture(GL_TEXTURE_CUBE_MAP, shadow_maps_[i]);
            for (int j = 0; j < 6; j++)
            {
                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + j, 0, GL_DEPTH_COMPONENT, width_, height_, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
            }

            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

            glBindFramebuffer(GL_FRAMEBUFFER, fbos_[i]);
            glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, shadow_maps_[i], 0);
            glDrawBuffer(GL_NONE);
            glReadBuffer(GL_NONE);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }
    }
    void PointShadowCaster::AddLight(std::shared_ptr<LightData> light)
    {
        std::shared_ptr<PointLightData> point_light = std::dynamic_pointer_cast<PointLightData>(light);
        if (point_light)
        {
            lights_.push_back(point_light);
        }
    }
    void PointShadowCaster::Render(const std::vector<std::shared_ptr<PrimitiveSceneProxy>> &proxies)
    {
        int current_shaodow_num = 0;

        for (auto &light : lights_)
        {
            if (current_shaodow_num < POINT_MAX_SHADOW_MAP_NUM)
            {
                glViewport(0, 0, width_, height_);
                glBindFramebuffer(GL_FRAMEBUFFER, fbos_[current_shaodow_num]);
                glClear(GL_DEPTH_BUFFER_BIT);
                RenderContext point_shader_context{.shader = shader_};
                shader_->UseProgram();
                std::array<Matrix4f, 6> light_space_matrices = CalculateLighSpaceMatrices(light->position);
                for (int i = 0; i < 6; i++)
                {
                    shader_->SetMat(("shadow_matrices[" + std::to_string(i) + ']').c_str(), light_space_matrices[i].Transpose()[0]);
                }
                shader_->SetVec3("light_position", light->position.Data());
                shader_->SetFloat("far_plane", far_plane_);

                for (auto &proxy : proxies)
                {
                    if (!proxy->IsVisible())
                    {
                        continue;
                    }
                    proxy->Draw(point_shader_context);
                }
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                light->shadow_map_index = current_shaodow_num + 1;
                current_shaodow_num++;
            }
            else
            {
                light->shadow_map_index = 0;
            }
        }
    }

    std::array<Matrix4f, 6> PointShadowCaster::CalculateLighSpaceMatrices(const Vector3f &light_position)
    {
        std::array<Matrix4f, 6> light_space_matrices;
        float aspect = static_cast<float>(width_) / static_cast<float>(height_);
        Matrix4f projection = Matrix4f::MakePerProjMatrix(math::DegreeToRadian(90.f), aspect, near_plane_, far_plane_);
        light_space_matrices[0] = projection * Matrix4f::MakeCameraMatrix(light_position, {1.f, 0.f, 0.f}, {0.f, -1.f, 0.f});
        light_space_matrices[1] = projection * Matrix4f::MakeCameraMatrix(light_position, {-1.f, 0.f, 0.f}, {0.f, -1.f, 0.f});
        light_space_matrices[2] = projection * Matrix4f::MakeCameraMatrix(light_position, {0.f, 1.f, 0.f}, {0.f, 0.f, 1.f});
        light_space_matrices[3] = projection * Matrix4f::MakeCameraMatrix(light_position, {0.f, -1.f, 0.f}, {0.f, 0.f, -1.f});
        light_space_matrices[4] = projection * Matrix4f::MakeCameraMatrix(light_position, {0.f, 0.f, 1.f}, {0.f, -1.f, 0.f});
        light_space_matrices[5] = projection * Matrix4f::MakeCameraMatrix(light_position, {0.f, 0.f, -1.f}, {0.f, -1.f, 0.f});
        return light_space_matrices;
    }


}