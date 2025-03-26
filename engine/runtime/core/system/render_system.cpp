#include "render_system.h"

#include "runtime/render/shader_manager.h"
#include "runtime/render/render_scene.h"
#include "runtime/render/render_object.h"
#include "runtime/render/render_camera.h"


namespace kpengine
{
    RenderSystem::RenderSystem():render_scene_(nullptr)
    {
       shader_manager_ = std::make_unique<ShaderManager>();
        render_camera_ = std::make_shared<RenderCamera>();
        render_scene_ = std::make_unique<RenderScene>();
    }
    void RenderSystem::Initialize()
    {
        // TODO
        shader_manager_->Initialize();
        render_scene_->Initialize(render_camera_);
    }

    void RenderSystem::Tick(float DeltaTime)
    {
        render_scene_->Render(DeltaTime);
    }

    RenderSystem::~RenderSystem()
    {
        render_scene_.reset();
    }
}