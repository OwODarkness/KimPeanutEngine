#include "render_system.h"
#include "runtime/render/shader_pool.h"
#include "runtime/render/render_scene.h"
#include "runtime/render/render_camera.h"


namespace kpengine
{
    RenderSystem::RenderSystem():
    render_camera_(std::make_shared<RenderCamera>()),
    render_scene_(std::make_unique<RenderScene>()),
    shader_pool_(std::make_unique<ShaderPool>())
    {

    }
    void RenderSystem::Initialize()
    {
        shader_pool_->Initialize();
        render_scene_->Initialize(render_camera_);
        
    }

    void RenderSystem::Tick(float delta_time)
    {
        render_scene_->Render(delta_time);
    }

    RenderSystem::~RenderSystem()
    {
        render_scene_.reset();
    }
}