#include "shadow_maker.h"

#include <glad/glad.h>
#include <iostream>
#include "platform/path/path.h"
#include "runtime/render/render_shader.h"
#include "runtime/runtime_global_context.h"
#include "runtime/core/system/render_system.h"
#include "runtime/render/shader_pool.h"
#include "runtime/render/render_shader.h"
#include "runtime/render/primitive_scene_proxy.h"
#include "runtime/render/render_context.h"
#include "runtime/render/render_light.h"
namespace kpengine
{

    ShadowMaker::ShadowMaker(int width, int height) : width_(width), height_(height)
    {
    }

    void ShadowMaker::Initialize()
    {
    }

    void ShadowMaker::BindFrameBuffer()
    {
        glViewport(0, 0, width_, height_);
        glBindFramebuffer(GL_FRAMEBUFFER, frame_buffer_);
        glClear(GL_DEPTH_BUFFER_BIT);
    }
    void ShadowMaker::UnBindFrameBuffer()
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    DirectionalShadowMaker::DirectionalShadowMaker(int width, int height) : ShadowMaker(width, height)
    {
    }

    void DirectionalShadowMaker::Initialize()
    {
        shader = runtime::global_runtime_context.render_system_->GetShaderPool()->GetShader(SHADER_CATEGORY_DIRECTIONALSHADOW);

        glGenFramebuffers(1, &frame_buffer_);
        glGenTextures(1, &depth_texture_);

        glBindTexture(GL_TEXTURE_2D, depth_texture_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, width_, height_, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        GLfloat borderColor[] = {1.0, 1.0, 1.0, 1.0};
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);

        glBindFramebuffer(GL_FRAMEBUFFER, frame_buffer_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth_texture_, 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void DirectionalShadowMaker::BindFrameBuffer()
    {
        glCullFace(GL_FRONT);
        ShadowMaker::BindFrameBuffer();
    }

    void DirectionalShadowMaker::UnBindFrameBuffer()
    {
        glCullFace(GL_BACK);
        ShadowMaker::UnBindFrameBuffer();
    }

    void DirectionalShadowMaker::CalculateShadowTransform(const Vector3f &light_position, std::vector<Matrix4f> &out_shadow_transforms)
    {
        Matrix4f light_projection, light_view;
        light_projection = Matrix4f::MakeOrthProjMatrix(-10.0f, 10.0f, -10.0f, 10.0f, near_plane_, far_plane_);
        light_view = Matrix4f::MakeCameraMatrix(light_position, Vector3f(0.f, -1.f, 0.f), Vector3f(0.f, 1.f, 0.f));
        out_shadow_transforms.push_back(light_projection * light_view);
    }

    PointShadowMaker::PointShadowMaker(int width, int height) : ShadowMaker(width, height)
    {
    }

    void PointShadowMaker::Initialize()
    {
        shader = runtime::global_runtime_context.render_system_->GetShaderPool()->GetShader(SHADER_CATEGORY_POINTSHADOW);
        near_plane_ = 1.f;
        far_plane_ = 25.f;

        glGenFramebuffers(1, &frame_buffer_);
        glGenTextures(1, &depth_texture_);

        glBindTexture(GL_TEXTURE_CUBE_MAP, depth_texture_);
        for (int i = 0; i < 6; i++)
        {
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_DEPTH_COMPONENT, width_, height_, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        }

        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

        glBindFramebuffer(GL_FRAMEBUFFER, frame_buffer_);
        glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, depth_texture_, 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void PointShadowMaker::Render(const std::vector<std::shared_ptr<PrimitiveSceneProxy>> &proxies)
    {
        glViewport(0, 0, width_, height_);
        glBindFramebuffer(GL_FRAMEBUFFER, frame_buffer_);
        glClear(GL_DEPTH_BUFFER_BIT);
        Vector3f light_pos = light_->position;
        // std::cout << light_pos.x_ << " " << light_pos.y_ << " " << light_pos.z_ << std::endl;
        // CalculateShadowTransform(light_pos, shadow_transforms);
        std::array<Matrix4f, 6> shadow_transforms = CalculateLighSpaceMatrices(light_pos);

        shader->UseProgram();
        for (int i = 0; i < shadow_transforms.size(); i++)
        {
            shader->SetMat(("shadow_matrices[" + std::to_string(i) + ']').c_str(), shadow_transforms[i].Transpose()[0]);
        }
        shader->SetVec3("light_position", light_pos.Data());
        shader->SetFloat("far_plane", far_plane_);
        for (auto &proxy : proxies)
        {
            if (!proxy->IsVisible())
            {
                continue;
            }
            proxy->Draw({.shader = shader});
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void PointShadowMaker::CalculateShadowTransform(const Vector3f &light_position, std::vector<Matrix4f> &out_shadow_transforms)
    {

        float aspect = (float)(width_ / height_);
        Matrix4f projection = Matrix4f::MakePerProjMatrix(90.f, aspect, near_plane_, far_plane_);
        out_shadow_transforms.push_back(projection * Matrix4f::MakeCameraMatrix(light_position, {1.f, 0.f, 0.f}, {0.f, -1.f, 0.f}));
        out_shadow_transforms.push_back(projection * Matrix4f::MakeCameraMatrix(light_position, {-1.f, 0.f, 0.f}, {0.f, -1.f, 0.f}));
        out_shadow_transforms.push_back(projection * Matrix4f::MakeCameraMatrix(light_position, {0.f, 1.f, 0.f}, {0.f, 0.f, 1.f}));
        out_shadow_transforms.push_back(projection * Matrix4f::MakeCameraMatrix(light_position, {0.f, -1.f, 0.f}, {0.f, 0.f, -1.f}));
        out_shadow_transforms.push_back(projection * Matrix4f::MakeCameraMatrix(light_position, {0.f, 0.f, 1.f}, {0.f, -1.f, 0.f}));
        out_shadow_transforms.push_back(projection * Matrix4f::MakeCameraMatrix(light_position, {0.f, 0.f, -1.f}, {0.f, -1.f, 0.f}));
    }

    void PointShadowMaker::AddLight(LightData *light)
    {
        PointLightData *point_light = static_cast<PointLightData *>(light);
        if (point_light)
        {
            light_ = point_light;
        }
        Vector3f light_pos = light_->position;
        std::cout << light_pos.x_ << " " << light_pos.y_ << " " << light_pos.z_ << std::endl;
    }

    std::array<Matrix4f, 6> PointShadowMaker::CalculateLighSpaceMatrices(const Vector3f &light_position)
    {
        std::array<Matrix4f, 6> light_space_matrices;
        float aspect = static_cast<float>(width_) / static_cast<float>(height_);
        Matrix4f projection = Matrix4f::MakePerProjMatrix(90.f, aspect, near_plane_, far_plane_);
        light_space_matrices[0] = projection * Matrix4f::MakeCameraMatrix(light_position, {1.f, 0.f, 0.f}, {0.f, -1.f, 0.f});
        light_space_matrices[1] = projection * Matrix4f::MakeCameraMatrix(light_position, {-1.f, 0.f, 0.f}, {0.f, -1.f, 0.f});
        light_space_matrices[2] = projection * Matrix4f::MakeCameraMatrix(light_position, {0.f, 1.f, 0.f}, {0.f, 0.f, 1.f});
        light_space_matrices[3] = projection * Matrix4f::MakeCameraMatrix(light_position, {0.f, -1.f, 0.f}, {0.f, 0.f, -1.f});
        light_space_matrices[4] = projection * Matrix4f::MakeCameraMatrix(light_position, {0.f, 0.f, 1.f}, {0.f, -1.f, 0.f});
        light_space_matrices[5] = projection * Matrix4f::MakeCameraMatrix(light_position, {0.f, 0.f, -1.f}, {0.f, -1.f, 0.f});
        return light_space_matrices;
    }
}