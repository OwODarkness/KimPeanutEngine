#ifndef KPENGINE_EDITOR_ACTOR_CONTROL_PANEL_H
#define KPENGINE_EDITOR_ACTOR_CONTROL_PANEL_H

#include <memory>

#include "editor/include/editor_ui_component/editor_window_component.h"

struct GLFWwindow;

namespace kpengine{
    class Actor;

namespace ui{
    template<typename T> class EditorDragComponent;

    class EditorActorControlPanel: public EditorWindowComponent{
    public:
        EditorActorControlPanel();
        void RenderContent() override;
        void SetActor(std::shared_ptr<Actor> actor);

        
    private:
        bool DragFloat3WithLock(const char* label, float v[3], float speed, float min, float max, GLFWwindow* window);
    private:
        std::shared_ptr<Actor> actor_;
        std::shared_ptr<Actor> last_actor_;

        float location[3] = {0.f, 0.f, 0.f};
        float rotation[3] = {0.f, 0.f, 0.f};
        float scale[3] = {0.f, 0.f, 0.f};
    };
}
}

#endif