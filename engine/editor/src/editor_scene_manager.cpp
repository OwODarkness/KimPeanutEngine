#include "editor_scene_manager.h"
#include "shader/shader.h"
#include "runtime/core/base/Mesh.h"
#include "runtime/render/frame_buffer.h"
#include "editor/include/editor_ui_component/editor_scene_component.h"
namespace kpengine
{
    namespace editor
    {

        void EditorSceneManager::Initialize(FrameBuffer *scene)
        {
            scene_ = scene;
            scene_ui_ = std::make_shared<ui::EditorSceneComponent>(scene_);

            shader = new kpengine::ShaderHelper("normal.vs", "normal.fs");
            shader->Initialize();

            std::vector<Vertex> verticles = {
                {{0.5, -0.5, 0}},
                {{-0.5, -0.5, 0}},
                {{0, 0.5, 0}}};
            std::vector<unsigned int> indices = {0, 1, 2};
            mesh = new kpengine::Mesh(verticles, indices);
            mesh->Initialize();
        }

        void EditorSceneManager::Tick()
        {

            scene_->BindFrameBuffer();
            shader->UseProgram();
            mesh->Draw();
            scene_ui_->Render();
            scene_->UnBindFrameBuffer();
        }

        void EditorSceneManager::Close()
        {
            scene_ui_.reset();
        }
    }
}