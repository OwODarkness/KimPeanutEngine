#include "render_scene.h"

#include <glm/gtc/type_ptr.hpp>


#include "runtime/render/frame_buffer.h"
#include "runtime/render/render_object.h"
#include "runtime/test/render_object_test.h"
#include "runtime/render/render_shader.h"
#include "runtime/render/render_camera.h"

namespace kpengine{

    enum class RenderSceneMode: uint8_t{
        SceneMode_Editor
    };

    void RenderScene::Initialize(std::shared_ptr<RenderCamera> camera)
    {
        scene_ = std::make_shared<FrameBuffer>(1280, 720);
        scene_->Initialize();

        render_object_ = test::GetRenderObjectTeapot();
        render_object_->Initialize();

        render_camera_ = camera;

    }

    void RenderScene::Render()
    {
        BeginDraw();
        render_object_->Render();
        render_object_->GetShader()->SetVec3("ambient", glm::value_ptr(ambient_light_.ambient));
        render_object_->GetShader()->SetVec3("point_light.position", glm::value_ptr(point_light_.position));
        render_object_->GetShader()->SetVec3("point_light.color", glm::value_ptr(point_light_.color));
        render_object_->GetShader()->SetVec3("view_position", glm::value_ptr(render_camera_->GetPosition()));
        

        EndDraw();
    }

    void RenderScene::BeginDraw()
    {
        scene_->BindFrameBuffer();
    }

    void RenderScene::EndDraw()
    {
        scene_->UnBindFrameBuffer();
    }

}