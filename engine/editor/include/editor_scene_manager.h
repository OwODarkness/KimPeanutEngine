#ifndef KPENGINE_EDITOR_SCENE_MANAGER_H
#define KPENGINE_EDITOR_SCENE_MANAGER_H

#include <memory>
#include "runtime/core/math/math_header.h"
#include "runtime/render/aabb.h"

namespace kpengine{
    class RenderSystem;
    class Actor;
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
        bool IsSceneFocus() const;
        bool IntersectRayAABB(const Vector3f& origin, const Vector3f& ray_dir, const AABB& aabb, float& out_dist);
        
        std::shared_ptr<input::InputContext> GetInputContext() const{return input_context_;}
    private:
        void OnClickMouseCallback(float mouse_pos_x, float mouse_pos_y);

    private:
        std::unique_ptr<ui::EditorSceneComponent> scene_ui_;
        std::shared_ptr<input::InputContext> input_context_;
        int object_selected_index;
        std::shared_ptr<Actor> last_select_actor_{};
    };
}
}

#endif