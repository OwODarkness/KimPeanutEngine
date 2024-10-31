#ifndef KPENGINE_EDITOR_EDITOR_SCENE_MANAGER_H
#define KPENGINE_EDITOR_EDITOR_SCENE_MANAGER_H

#include <memory>

namespace kpengine{
    class FrameBuffer;
    class Mesh;
    class ShaderHelper;
namespace ui{
    class EditorSceneComponent;
}
namespace editor{
    class EditorSceneManager{
    public:
        EditorSceneManager() = default;
        void Initialize(FrameBuffer* scene);
        void Tick();
        void Close();
    public:
        FrameBuffer* scene_;
        Mesh* mesh;
        ShaderHelper* shader;
    private:
        std::shared_ptr<ui::EditorSceneComponent> scene_ui_;
    };
}
}

#endif