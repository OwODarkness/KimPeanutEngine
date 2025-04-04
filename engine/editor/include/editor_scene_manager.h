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
        EditorSceneManager();
        ~EditorSceneManager();
        void Initialize();
        void Tick();
        void Close();
        bool IsCursorInScene(float cursor_x, float cursor_y);
        bool IsSCeneFocus() const;
    private:
        std::unique_ptr<ui::EditorSceneComponent> scene_ui_;
    };
}
}

#endif