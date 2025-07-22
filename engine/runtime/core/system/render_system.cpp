#include "render_system.h"
#include <iostream>
#include <glad/glad.h>
#include "runtime/render/render_scene.h"
#include "runtime/render/render_camera.h"
#include "runtime/render/texture_pool.h"

namespace kpengine
{
    RenderSystem::RenderSystem():
    render_camera_(std::make_shared<RenderCamera>()),
    render_scene_(std::make_unique<RenderScene>()),
    shader_pool_(std::make_unique<ShaderPool>()),
    texture_pool_(std::make_unique<TexturePool>())
    {

    }
    void RenderSystem::Initialize()
    {
        shader_pool_->Initialize();
        render_camera_->Initialize();
        render_scene_->Initialize(render_camera_);
    }

    void RenderSystem::PostInitialize()
    {
        render_camera_->PostInitialize();
    }

    void RenderSystem::Tick(float delta_time)
    {
        GLuint query;
        glGenQueries(1, &query);

        glBeginQuery(GL_PRIMITIVES_GENERATED, query);
        render_scene_->Render(delta_time);
        glEndQuery(GL_PRIMITIVES_GENERATED);
        GLuint primitives_generated = 0;
        glGetQueryObjectuiv(query, GL_QUERY_RESULT, &primitives_generated);
        glDeleteQueries(1, &query);
        triangle_count_this_frame_ = primitives_generated;
    }

    void RenderSystem::SetCurrentShaderMode(const std::string& target)
    {
        current_shader_mode_ = target;
        render_scene_->SetCurrentShader(shader_pool_->GetShader(current_shader_mode_));
    }

    RenderSystem::~RenderSystem()
    {
        render_scene_.reset();
    }
}