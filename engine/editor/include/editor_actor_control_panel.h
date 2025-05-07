#ifndef KPENGINE_EDITOR_ACTOR_CONTROL_PANEL_H
#define KPENGINE_EDITOR_ACTOR_CONTROL_PANEL_H

#include "editor/include/editor_ui_component/editor_window_component.h"


namespace kpengine{
    class Actor;
namespace ui{
    class EditorActorControlPanel: public EditorWindowComponent{
    public:
        EditorActorControlPanel(Actor* actor);
        
    private:
        Actor* actor_;

    };
}
}

#endif