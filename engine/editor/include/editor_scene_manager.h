#ifndef KPENGINE_EDITOR_EDITOR_SCENE_MANAGER_H
#define KPENGINE_EDITOR_EDITOR_SCENE_MANAGER_H

#include <memory>

namespace kpengine{
    class SceneSystem;
namespace ui{
    class EditorSceneComponent;
}
namespace editor{
    class EditorSceneManager{
    public:
        EditorSceneManager() = default;
        void Initialize(SceneSystem* scene_system);
        void Tick();
        void Close();
    public:
        SceneSystem* scene_system_;
    private:
        std::shared_ptr<ui::EditorSceneComponent> scene_ui_;
    };
}
}

#endif