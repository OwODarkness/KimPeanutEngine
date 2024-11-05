#ifndef KPENGINE_EDITOR_SCENE_MANAGER_H
#define KPENGINE_EDITOR_SCENE_MANAGER_H

#include <memory>

namespace kpengine{
    class RenderSystem;
namespace ui{
    class EditorSceneComponent;
}
namespace editor{
    class EditorSceneManager{
    public:
        EditorSceneManager() = default;
        void Initialize();
        void Tick();
        void Close();
    private:
        std::shared_ptr<ui::EditorSceneComponent> scene_ui_;
    };
}
}

#endif