#include "shadow_maker.h"

#include <glad/glad.h>

#include "platform/path/path.h"
#include "runtime/render/render_shader.h"
namespace kpengine
{

    ShadowMaker::ShadowMaker(int width, int height) : width_(width), height_(height)
    {
    }

    void ShadowMaker::Initialize()
    {
        shader->Initialize();
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

    DirectionalShadowMaker::DirectionalShadowMaker(int width, int height):
    ShadowMaker(width, height)
    {
        std::string shader_directory = GetShaderDirectory();
        shader = std::make_shared<RenderShader>(
            "shadow_mapping_depth.vs", 
            "shadow_mapping_depth.fs");

    }

    void DirectionalShadowMaker::Initialize()
    {
        ShadowMaker::Initialize();
        
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

    void DirectionalShadowMaker::CalculateShadowTransform(const Vector3f& light_position, std::vector<Matrix4f>& out_shadow_transforms)
    {
        Matrix4f light_projection, light_view;
        light_projection = Matrix4f::MakeOrthProjMatrix(-10.0f, 10.0f, -10.0f, 10.0f, near_plane_, far_plane_);
        light_view = Matrix4f::MakeCameraMatrix(light_position, Vector3f(0.f, -1.f, 0.f), Vector3f(0.f, 1.f, 0.f));
        out_shadow_transforms.push_back(light_projection * light_view);
    }

    PointShadowMaker::PointShadowMaker(int width, int height) : ShadowMaker(width, height){
        shader = std::make_shared<RenderShader>( 
            "pointshadow_mapping_depth.vs", 
            "pointshadow_mapping_depth.fs",
            "pointshadow_mapping_depth.gs");
        near_plane_ = 1.f;
        far_plane_ = 25.f;
    }

    void PointShadowMaker::Initialize()
    {
        ShadowMaker::Initialize();
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


    void PointShadowMaker::BindFrameBuffer()
    {
        ShadowMaker::BindFrameBuffer();
    }

    void PointShadowMaker::UnBindFrameBuffer()
    {
        ShadowMaker::UnBindFrameBuffer();
    }



    void  PointShadowMaker::CalculateShadowTransform(const Vector3f& light_position, std::vector<Matrix4f>& out_shadow_transforms)
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
}