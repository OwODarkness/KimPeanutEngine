#include "render_system.h"

#include "runtime/render/render_scene.h"
#include "runtime/render/render_camera.h"

namespace kpengine{
    void RenderSystem::Initialize()
    {
        //TODO 
        //Initialize render_scene, render_camera
        render_scene_ = std::make_shared<RenderScene>();
        render_scene_->Initialize();

        render_camera_ = std::make_shared<RenderCamera>();
    }

    void RenderSystem::Tick(float DeltaTime)
    {
        render_scene_->Render();
    }
}