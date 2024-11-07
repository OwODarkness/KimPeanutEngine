#include "render_system.h"

#include <memory>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "runtime/render/render_scene.h"
#include "runtime/render/render_camera.h"
#include "runtime/render/render_object.h"
#include "runtime/render/render_shader.h"
namespace kpengine{
    void RenderSystem::Initialize()
    {
        //TODO 
        //Initialize render_scene, render_camera

        render_camera_ = std::make_shared<RenderCamera>();

        render_scene_ = std::make_shared<RenderScene>();
        render_scene_->Initialize(render_camera_);

    }

    void RenderSystem::Tick(float DeltaTime)
    {
        render_scene_->Render();

        glm::mat4 view_transform = render_camera_->GetViewMatrix();
        glm::mat4 projection_transform = render_camera_->GetProjectionMatrix();

        
        std::shared_ptr<RenderShader> shader = render_scene_->render_object_->GetShader();
        shader->SetMat("view", glm::value_ptr(view_transform));
        shader->SetMat("projection", glm::value_ptr(projection_transform));

    }
}