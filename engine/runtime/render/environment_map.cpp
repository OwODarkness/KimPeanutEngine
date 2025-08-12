#include "environment_map.h"
#include <iostream>
#include <glad/glad.h>
#include "runtime/render/render_shader.h"
#include "runtime/render/render_texture.h"
#include "runtime/runtime_header.h"

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
        glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

        hdr_texture_->Initialize();
        ShaderPool* shader_pool = runtime::global_runtime_context.render_system_->GetShaderPool();
        equirect_to_cubemap_shader_ = shader_pool->GetShader(SHADER_CATEGORY_EQUIRECTANGULAR);
        irradiance_shader_ = shader_pool->GetShader(SHADER_CATEGORY_IRRADIANCE);
        prefilter_shader_ = shader_pool->GetShader(SHADER_CATEGORY_PREFILTER);
        brdf_shader_ = shader_pool->GetShader(SHADER_CATEGORY_BRDF);

        glGenFramebuffers(1, &capture_fbo_);
        glGenRenderbuffers(1, &capture_rbo_);

        Matrix4f projection = Matrix4f::MakePerProjMatrix(math::DegreeToRadian(90.f), 1.f, 0.1f, 10.f);
        std::vector<Matrix4f> views = {
            Matrix4f::MakeCameraMatrix({0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, -1.f, 0.f}),
            Matrix4f::MakeCameraMatrix({0.f, 0.f, 0.f}, {-1.f, 0.f, 0.f}, {0.f, -1.f, 0.f}),
            Matrix4f::MakeCameraMatrix({0.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 0.f, 1.f}),
            Matrix4f::MakeCameraMatrix({0.f, 0.f, 0.f}, {0.f, -1.f, 0.f}, {0.f, 0.f, -1.f}),
            Matrix4f::MakeCameraMatrix({0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, {0.f, -1.f, 0.f}),
            Matrix4f::MakeCameraMatrix({0.f, 0.f, 0.f}, {0.f, 0.f, -1.f}, {0.f, -1.f, 0.f}),
        };
        

        GenerateEnvironmentMap(projection, views);
        GenerateIrradianceMap(projection, views);
        GeneratePrefilterMap(projection, views);
        GenerateBRDFMap();
        return true;
    }

    void EnvironmentMapWrapper::GenerateEnvironmentMap(const Matrix4f& proj,  const std::vector<Matrix4f>& views)
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


        glBindFramebuffer(GL_FRAMEBUFFER, capture_fbo_);
        glBindRenderbuffer(GL_RENDERBUFFER, capture_rbo_);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 512, 512);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, capture_rbo_);


        equirect_to_cubemap_shader_->UseProgram();
        equirect_to_cubemap_shader_->SetMat("projection", proj.Transpose()[0]);
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

    void EnvironmentMapWrapper::GenerateIrradianceMap(const Matrix4f& proj,  const std::vector<Matrix4f>& views)
    {
        glGenTextures(1, &irradiance_map_handle_);
        glBindTexture(GL_TEXTURE_CUBE_MAP, irradiance_map_handle_);
        for(unsigned int i = 0;i < 6 ;i++)
        {
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F, 32, 32, 0, GL_RGB, GL_FLOAT, nullptr);
        }
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glBindFramebuffer(GL_FRAMEBUFFER, capture_fbo_);
        glBindRenderbuffer(GL_RENDERBUFFER, capture_rbo_);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 32, 32);

        irradiance_shader_->UseProgram();
        irradiance_shader_->SetMat("projection", proj.Transpose()[0]);
        irradiance_shader_->SetInt("environment_map", 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, environment_map_handle_);
        
        glViewport(0, 0, 32, 32);
        glBindFramebuffer(GL_FRAMEBUFFER, capture_fbo_);
        for(unsigned int  i = 0;i<6;i++)
        {
            irradiance_shader_->SetMat("view", views[i].Transpose()[0]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, irradiance_map_handle_, 0);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            RenderCube();
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        irradiance_map_ = std::make_shared<RenderTextureCubeMap>(hdr_path_, irradiance_map_handle_);
    }

    void EnvironmentMapWrapper::GeneratePrefilterMap(const Matrix4f& proj,  const std::vector<Matrix4f>& views)
    {
        glGenTextures(1, &prefilter_map_handle_);
        glBindTexture(GL_TEXTURE_CUBE_MAP, prefilter_map_handle_);
        for(unsigned int i = 0;i < 6 ;i++)
        {
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F, 128, 128, 0, GL_RGB, GL_FLOAT, nullptr);
        }
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

        glBindFramebuffer(GL_FRAMEBUFFER, capture_fbo_);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 128, 128);

        prefilter_shader_->UseProgram();
        prefilter_shader_->SetMat("projection", proj.Transpose()[0]);
        prefilter_shader_->SetInt("environment_map", 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, environment_map_handle_);

        glBindFramebuffer(GL_FRAMEBUFFER, capture_fbo_);

        unsigned int max_mip_level = 5;
        for(unsigned int mip_level = 0 ; mip_level < max_mip_level ;mip_level++)
        {
            unsigned int mip_width = static_cast<unsigned int>(128 * std::pow(0.5, mip_level));
            unsigned int mip_height = static_cast<unsigned int>(128 * std::pow(0.5, mip_level));

            glViewport(0, 0, mip_width, mip_height);
            glBindRenderbuffer(GL_RENDERBUFFER, capture_rbo_);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, mip_width, mip_height);
            float roughness = (float)mip_level / (float)(max_mip_level - 1 );
            prefilter_shader_->SetFloat("roughness", roughness);
            for(unsigned int i = 0;i<6;i++)
            {
                prefilter_shader_->SetMat("view", views[i].Transpose()[0]);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, prefilter_map_handle_, mip_level);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                RenderCube();
            }
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        prefilter_map_ = std::make_shared<RenderTextureCubeMap>(hdr_path_, prefilter_map_handle_);

    }

    void EnvironmentMapWrapper::GenerateBRDFMap()
    {
        glGenTextures(1, &brdf_map_handle_);
        glBindTexture(GL_TEXTURE_2D, brdf_map_handle_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, 512, 512, 0, GL_RG, GL_FLOAT, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glBindFramebuffer(GL_FRAMEBUFFER, capture_fbo_);

        glBindRenderbuffer(GL_RENDERBUFFER, capture_rbo_);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 128, 128);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, brdf_map_handle_, 0);
        glViewport(0, 0, 512, 512);
        brdf_shader_->UseProgram();
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        RenderQuad();

        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        brdf_map_ = std::make_shared<RenderTexture2D>(hdr_path_, brdf_map_handle_);
    }

    void EnvironmentMapWrapper::RenderCube()
    {
        if(cube_vao == 0)
        {
            glGenVertexArrays(1, &cube_vao);
            glGenBuffers(1, &cube_vbo);
            glBindVertexArray(cube_vao);

            glBindBuffer(GL_ARRAY_BUFFER, cube_vbo);
            glBufferData(GL_ARRAY_BUFFER, sizeof(cube_vertices), cube_vertices, GL_STATIC_DRAW);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
            glBindVertexArray(0);
        }

        glBindVertexArray(cube_vao);
        glDrawArrays(GL_TRIANGLES, 0, 36);
        glBindVertexArray(0);
    }

    void EnvironmentMapWrapper::RenderQuad()
    {
        if(quad_vao == 0)
        {
            glGenVertexArrays(1, &quad_vao);
            glGenBuffers(1, &quad_vbo);

            glBindVertexArray(quad_vao);

            glBindBuffer(GL_ARRAY_BUFFER, quad_vbo);
            glBufferData(GL_ARRAY_BUFFER, sizeof(quad_vertices), quad_vertices, GL_STATIC_DRAW);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE , 5 * sizeof(float), (void*)(3 * sizeof(float)));
            glBindVertexArray(0);
        }
        glBindVertexArray(quad_vao);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glBindVertexArray(0);
    }

    unsigned int EnvironmentMapWrapper::cube_vao = 0;
    unsigned int EnvironmentMapWrapper::cube_vbo = 0;
    const float EnvironmentMapWrapper::cube_vertices[108] = {
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

    unsigned int EnvironmentMapWrapper::quad_vao = 0;
    unsigned int EnvironmentMapWrapper::quad_vbo = 0;
    const float EnvironmentMapWrapper::quad_vertices[20] = {
            -1.0f,  1.0f, 0.0f, 0.0f, 1.0f,
            -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
             1.0f,  1.0f, 0.0f, 1.0f, 1.0f,
             1.0f, -1.0f, 0.0f, 1.0f, 0.0f
    };
}