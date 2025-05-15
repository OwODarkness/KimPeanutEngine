#include "environment_map.h"
#include <iostream>
#include <vector>
#include <glad/glad.h>
#include "runtime/render/render_shader.h"
#include "runtime/render/render_texture.h"
#include "runtime/runtime_header.h"
#include "runtime/core/math/math_header.h"

namespace kpengine{
    EnvironmentMapWrapper::EnvironmentMapWrapper(const std::string& hdr_path):
    hdr_texture_(std::make_shared<RenderTextureHDR>(hdr_path)),
    hdr_path_(hdr_path),
    capture_fbo_(0),
    capture_rbo_(0)
    {

    }

    bool EnvironmentMapWrapper::Initialize()
    {
        hdr_texture_->Initialize();
        equirect_to_cubemap_shader_ = runtime::global_runtime_context.render_system_->GetShaderPool()->GetShader(SHADER_CATEGORY_EQUIRECTANGULAR);
        
        glGenFramebuffers(1, &capture_fbo_);
        glGenRenderbuffers(1, &capture_rbo_);

        glBindFramebuffer(GL_FRAMEBUFFER, capture_fbo_);
        glBindRenderbuffer(GL_RENDERBUFFER, capture_rbo_);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 512, 512);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, capture_rbo_);

        GenerateEnvironmentMap();

        return true;
    }

    std::shared_ptr<RenderTexture> EnvironmentMapWrapper::GenerateEnvironmentMap()
    {
        glGenTextures(1, &environment_map_handle_);
        glBindTexture(GL_TEXTURE_CUBE_MAP, environment_map_handle_);
        for(unsigned int i = 0;i < 6 ;i++)
        {
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F, 512, 512, 0, GL_RGB, GL_FLOAT, nullptr);
        }
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

        Matrix4f projection = Matrix4f::MakePerProjMatrix(90.f, 1.f, 0.1f, 10.f);
        std::vector<Matrix4f> views = {
            Matrix4f::MakeCameraMatrix({0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, -1.f, 0.f}),
            Matrix4f::MakeCameraMatrix({0.f, 0.f, 0.f}, {-1.f, 0.f, 0.f}, {0.f, -1.f, 0.f}),
            Matrix4f::MakeCameraMatrix({0.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 0.f, 1.f}),
            Matrix4f::MakeCameraMatrix({0.f, 0.f, 0.f}, {0.f, -1.f, 0.f}, {0.f, 0.f, -1.f}),
            Matrix4f::MakeCameraMatrix({0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, {0.f, -1.f, 0.f}),
            Matrix4f::MakeCameraMatrix({0.f, 0.f, 0.f}, {0.f, 0.f, -1.f}, {0.f, -1.f, 0.f}),
        };
        
        equirect_to_cubemap_shader_->UseProgram();
        equirect_to_cubemap_shader_->SetMat("projection", projection.Transpose()[0]);
        equirect_to_cubemap_shader_->SetInt("equiretangular_map", 0);
        glActiveTexture(GL_TEXTURE0 );
        glBindTexture(GL_TEXTURE_2D, hdr_texture_->GetTexture());
        
        glViewport(0, 0, 512, 512);
        glBindFramebuffer(GL_FRAMEBUFFER, capture_fbo_);
        for(unsigned int  i = 0;i<6;i++)
        {
            equirect_to_cubemap_shader_->SetMat("view", views[i].Transpose()[0]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, environment_map_handle_, 0);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            RenderCube();
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        environment_map_ = std::make_shared<RenderTextureCubeMap>(hdr_path_, environment_map_handle_);
    }

    void EnvironmentMapWrapper::RenderCube()
    {
        if(vao == 0)
        {
            glGenVertexArrays(1, &vao);
            glGenBuffers(1, &vbo);
            glBindVertexArray(vao);

            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
            glBindVertexArray(0);
        }

        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 36);
        glBindVertexArray(0);
    }

    unsigned int EnvironmentMapWrapper::vao = 0;
    unsigned int EnvironmentMapWrapper::vbo = 0;
    const float EnvironmentMapWrapper::vertices[108] = {
        // positions
        -1.0f, 1.0f, -1.0f,
        -1.0f, -1.0f, -1.0f,
        1.0f, -1.0f, -1.0f,
        1.0f, -1.0f, -1.0f,
        1.0f, 1.0f, -1.0f,
        -1.0f, 1.0f, -1.0f,

        -1.0f, -1.0f, 1.0f,
        -1.0f, -1.0f, -1.0f,
        -1.0f, 1.0f, -1.0f,
        -1.0f, 1.0f, -1.0f,
        -1.0f, 1.0f, 1.0f,
        -1.0f, -1.0f, 1.0f,

        1.0f, -1.0f, -1.0f,
        1.0f, -1.0f, 1.0f,
        1.0f, 1.0f, 1.0f,
        1.0f, 1.0f, 1.0f,
        1.0f, 1.0f, -1.0f,
        1.0f, -1.0f, -1.0f,

        -1.0f, -1.0f, 1.0f,
        -1.0f, 1.0f, 1.0f,
        1.0f, 1.0f, 1.0f,
        1.0f, 1.0f, 1.0f,
        1.0f, -1.0f, 1.0f,
        -1.0f, -1.0f, 1.0f,

        -1.0f, 1.0f, -1.0f,
        1.0f, 1.0f, -1.0f,
        1.0f, 1.0f, 1.0f,
        1.0f, 1.0f, 1.0f,
        -1.0f, 1.0f, 1.0f,
        -1.0f, 1.0f, -1.0f,

        -1.0f, -1.0f, -1.0f,
        -1.0f, -1.0f, 1.0f,
        1.0f, -1.0f, -1.0f,
        1.0f, -1.0f, -1.0f,
        -1.0f, -1.0f, 1.0f,
        1.0f, -1.0f, 1.0f};
}