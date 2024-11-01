#include "render_scene.h"
#include "runtime/render/frame_buffer.h"
#include "runtime/render/render_object.h"
#include "runtime/test/render_object_test.h"
namespace kpengine{

    enum class RenderSceneMode: uint8_t{
        SceneMode_Editor
    };

    void RenderScene::Initialize()
    {
        scene_ = std::make_shared<FrameBuffer>(1280, 720);
        scene_->Initialize();

        render_object_ = test::GetRenderObjectRectangle();
        render_object_->Initialize();
    }

    void RenderScene::Render()
    {
        BeginDraw();
        render_object_->Render();
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