#include "frame_buffer.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "log/logger.h"
#include "runtime_global_context.h"
namespace kpengine
{

    FrameBuffer::FrameBuffer(int width, int height) : width_(width), height_(height)
    {
    }

    FrameBuffer::~FrameBuffer()
    {
        glDeleteBuffers(1, &fbo_);
    }

    void FrameBuffer::Initialize()
    {
        glGenFramebuffers(1, &fbo_);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);

        // Create render buffer
        glGenRenderbuffers(1, &rbo_);
        glBindRenderbuffer(GL_RENDERBUFFER, rbo_);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width_, height_);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);

        // Attach Renderbuffer to framebuffer
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rbo_);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            KP_LOG("FrameBuffer", LOG_LEVEL_ERROR, "Frame buffer is not complete");
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void FrameBuffer::Finalize()
    {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        glDrawBuffers(static_cast<GLsizei>(draw_buffers_.size()), draw_buffers_.data());
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void FrameBuffer::BindFrameBuffer()
    {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        glClearColor(0.0, 0.0, 0.0, 1.0); // for gAlbedo
        glViewport(0, 0, 1280, 720);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    void FrameBuffer::UnBindFrameBuffer()
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void FrameBuffer::ReSizeFrameBuffer(int width, int height)
    {
        width_ = width;
        height_ = height;

        // glBindTexture(GL_TEXTURE_2D, texture_);
        // glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width_, height_, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
        // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        // glBindTexture(GL_TEXTURE_2D, 0);

        glBindRenderbuffer(GL_RENDERBUFFER, rbo_);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rbo_);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
    }

    void FrameBuffer::AddColorAttachment(const std::string &name,
                                         unsigned int internal_format,
                                         unsigned int format,
                                         unsigned int type)
    {

        unsigned int texture;
        glGenTextures(1, &texture);

        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(GL_TEXTURE_2D, 0, internal_format, width_, height_, 0, format, type, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, 0);

        // Store the texture and its index
        int attachment_index = static_cast<int>(color_attachments_.size());
        color_attachments_.push_back(texture);
        name_to_index_[name] = attachment_index;
        draw_buffers_.push_back(GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(attachment_index));

        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);

        // Attach to framebuffer
        glFramebufferTexture2D(GL_FRAMEBUFFER,
                               GL_COLOR_ATTACHMENT0 + attachment_index,
                               GL_TEXTURE_2D,
                               texture,
                               0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    unsigned int FrameBuffer::GetTexture(size_t index) const
    {
        return color_attachments_[index];
    }

    unsigned int FrameBuffer::GetTexture(const std::string &name) const
    {
        auto it = name_to_index_.find(name);
        if (it == name_to_index_.end())
        {
            return 0;
        }

        return GetTexture(it->second);
    }
}