#ifndef KPENGINE_EDITOR_CAMERA_SETTINGS_COMPONENT_H
#define KPENGINE_EDITOR_CAMERA_SETTINGS_COMPONENT_H

#include "editor/ui/component/editor_window_component.h"

namespace kpengine::runtime
{
    class ISceneCameraControlSink;
}

namespace kpengine::editor
{
    class EditorCameraSettingsComponent final : public EditorWindowComponent
    {
    public:
        explicit EditorCameraSettingsComponent(
            runtime::ISceneCameraControlSink *camera_control_sink);

        void RenderContent() override;

    private:
        runtime::ISceneCameraControlSink *camera_control_sink_ = nullptr;
        float move_speed_ = 0.0f;
        bool initialized_ = false;
    };
}

#endif
