#ifndef KPENGINE_EDITOR_ACTOR_EDITOR_ACTOR_INSPECTOR_COMPONENT_H
#define KPENGINE_EDITOR_ACTOR_EDITOR_ACTOR_INSPECTOR_COMPONENT_H

#include <unordered_map>

#include "editor/actor/actor_editor_model.h"
#include "editor/ui/component/editor_window_component.h"

namespace kpengine
{
    class WindowSystem;
    namespace input
    {
        class InputSystem;
    }
    namespace runtime
    {
        class ISceneCameraControlSink;
    }
}

namespace kpengine::editor
{
    class EditorActorInspectorComponent final : public EditorWindowComponent
    {
    public:
        EditorActorInspectorComponent(ActorEditorModel &model,
                                      WindowSystem *window_system,
                                      input::InputSystem *input_system,
                                      runtime::ISceneCameraControlSink *camera_control_sink);
        ~EditorActorInspectorComponent() override;

        void RenderContent() override;

    private:
        void UpdateValueMouseCapture(bool transform_drag_active);
        void ReleaseValueMouseCapture();

        ActorEditorModel &model_;
        WindowSystem *window_system_ = nullptr;
        input::InputSystem *input_system_ = nullptr;
        runtime::ISceneCameraControlSink *camera_control_sink_ = nullptr;
        bool value_mouse_captured_ = false;
        std::unordered_map<ActorEditorPropertyKey, double, ActorEditorPropertyKeyHash>
            numeric_drafts_;
    };
}

#endif
