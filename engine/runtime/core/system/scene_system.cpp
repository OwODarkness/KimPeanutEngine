#include "scene_system.h"
#include "runtime/render/frame_buffer.h"
#include "runtime/render/render_object.h"
#include "runtime/test/render_object_test.h"
namespace kpengine{
    void SceneSystem::Initialize()
    {
        scene_ = std::make_shared<FrameBuffer>(1280, 720);
        scene_->Initialize();

        render_object_ = test::GetRenderObjectRectangle();
        render_object_->Initialize();
    }

    void SceneSystem::Render()
    {
        BeginDraw();
        render_object_->Render();
        EndDraw();
    }

    void SceneSystem::BeginDraw()
    {
        scene_->BindFrameBuffer();
    }

    void SceneSystem::EndDraw()
    {
        scene_->UnBindFrameBuffer();
    }

}