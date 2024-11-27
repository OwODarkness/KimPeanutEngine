#include "shadow_maker.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

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
        glGenFramebuffers(1, &depth_buffer_);
        glGenTextures(1, &depth_texture_);

        glBindTexture(GL_TEXTURE_2D, depth_texture_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, width_, height_, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        GLfloat borderColor[] = {1.0, 1.0, 1.0, 1.0};
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);

        glBindFramebuffer(GL_FRAMEBUFFER, depth_buffer_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth_texture_, 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        shader->Initialize();
    }

    std::shared_ptr<RenderShader> ShadowMaker::GetShader()
    {
        return shader;
    }

}