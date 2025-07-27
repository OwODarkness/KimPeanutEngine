#ifndef KPENGINE_EDITOR_SCENE_MANAGER_H
#define KPENGINE_EDITOR_SCENE_MANAGER_H

#include <memory>


namespace kpengine{
    class RenderSystem;
namespace ui{
    class EditorSceneComponent;
    }
namespace input{
    class InputContext;
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

        std::shared_ptr<input::InputContext> GetInputContext() const{return input_context_;}
    private:
        void OnClickMouseCallback(float mouse_pos_x, float mouse_pos_y);

    private:
        std::unique_ptr<ui::EditorSceneComponent> scene_ui_;
        std::shared_ptr<input::InputContext> input_context_;
        int object_selected_index;
    };
}
}

#endif