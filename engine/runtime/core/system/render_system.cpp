#include "render_system.h"

#include <memory>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "runtime/render/render_scene.h"
#include "runtime/render/render_object.h"
#include "runtime/render/render_camera.h"


namespace kpengine
{
    void RenderSystem::Initialize()
    {
        // TODO

        render_camera_ = std::make_shared<RenderCamera>();

        render_scene_ = std::make_shared<RenderScene>();
        render_scene_->Initialize(render_camera_);
    }

    void RenderSystem::Tick(float DeltaTime)
    {
        render_scene_->Render(DeltaTime);

    }
}