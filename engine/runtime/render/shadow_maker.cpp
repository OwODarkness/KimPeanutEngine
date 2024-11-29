#include "shadow_maker.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

#include "platform/path/path.h"
#include "runtime/render/render_shader.h"
namespace kpengine
{

    ShadowMaker::ShadowMaker(int width, int height) : width_(width), height_(height)
    {
        shader = std::make_shared<RenderShader>(GetShaderDirectory() + "shadow_mapping_depth.vs", GetShaderDirectory() + "shadow_mapping_depth.fs");
    }

    void ShadowMaker::Initialize()
    {
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

        shader->Initialize();
    }

    void ShadowMaker::BindFrameBuffer()
    {
        glCullFace(GL_FRONT);
        glViewport(0, 0, width_, height_);
        glBindFramebuffer(GL_FRAMEBUFFER, frame_buffer_);
        glClear(GL_DEPTH_BUFFER_BIT);
    }
    void ShadowMaker::UnBindFrameBuffer()
    {
        glCullFace(GL_BACK);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    glm::mat4 ShadowMaker::CalculateShadowTransform(glm::vec3 light_position)
    {
        glm::mat4 light_projection, light_view;
        light_projection = glm::ortho(-10.0f, 10.0f, -10.0f, 10.0f, near_plane_, far_plane_);
        light_view = glm::lookAt(light_position, glm::vec3(0.f), glm::vec3(0.f, 1.f, 0.f));

        return light_projection * light_view;
    }

    PointShadowMaker::PointShadowMaker(int width, int height) : width_(width), height_(height) {
        shader = std::make_shared<RenderShader>(GetShaderDirectory() + "pointshadow_mapping_depth.vs", GetShaderDirectory() + "pointshadow_mapping_depth.fs", GetShaderDirectory() + "pointshadow_mapping_depth.gs");
        
    }

    void PointShadowMaker::Initialize()
    {
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

        shader->Initialize();
    }


    void PointShadowMaker::BindFrameBuffer()
    {
        glViewport(0, 0, width_, height_);
        glBindFramebuffer(GL_FRAMEBUFFER, frame_buffer_);
        glClear(GL_DEPTH_BUFFER_BIT);
    }

    void PointShadowMaker::UnBindFrameBuffer()
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    std::vector<glm::mat4> PointShadowMaker::CalculateShadowTransform(const glm::vec3& light_position)
    {
        std::vector<glm::mat4> transforms;
        transforms.reserve(6);

        float aspect = (float)(width_ / height_);
        glm::mat4 projection =  glm::perspective(glm::radians(90.f), aspect, near_plane_, far_plane_);
        transforms.push_back(projection * glm::lookAt(light_position, light_position + glm::vec3(1.f, 0.f, 0.f), glm::vec3(0.f, -1.f, 0.f)));
        transforms.push_back(projection * glm::lookAt(light_position, light_position + glm::vec3(-1.f, 0.f, 0.f), glm::vec3(0.f, -1.f, 0.f)));
        transforms.push_back(projection * glm::lookAt(light_position, light_position + glm::vec3(0.f, 1.f, 0.f), glm::vec3(0.f, 0.f, 1.f)));
        transforms.push_back(projection * glm::lookAt(light_position, light_position + glm::vec3(0.f, -1.f, 0.f), glm::vec3(0.f, 0.f, -1.f)));
        transforms.push_back(projection * glm::lookAt(light_position, light_position + glm::vec3(0.f, 0.f, 1.f), glm::vec3(0.f, -1.f, 0.f)));
        transforms.push_back(projection * glm::lookAt(light_position, light_position + glm::vec3(0.f, 0.f, -1.f), glm::vec3(0.f, -1.f, 0.f)));
        
        return transforms;
    }
}