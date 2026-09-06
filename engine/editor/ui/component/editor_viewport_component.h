#ifndef KPENGINE_EDITOR_VIEWPORT_COMPONENT_H
#define KPENGINE_EDITOR_VIEWPORT_COMPONENT_H

#include <memory>

#include "editor/ui/component/editor_ui_component.h"
#include "graphics/backend/common/render_target.h"
#include "spatial/ray.h"

namespace kpengine::render
{
    class RenderSystem;
}

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
        class ISceneSelectionSink;
    }
}

namespace kpengine::editor
{
    class IEditorImguiRenderer;
    class ActorEditorModel;
    class EditorTransformGizmo;

    // Draws the render system's borrowed scene-output view. It owns neither the
    // render target nor the ImGui texture registration.
    class EditorViewportComponent final : public EditorUIComponent
    {
    public:
        EditorViewportComponent(render::RenderSystem *render_system,
                                IEditorImguiRenderer *imgui_renderer,
                                WindowSystem *window_system,
                                input::InputSystem *input_system,
                                runtime::ISceneCameraControlSink *camera_control_sink,
                                runtime::ISceneSelectionSink *scene_selection_sink,
                                ActorEditorModel *actor_model);
        ~EditorViewportComponent() override;

        void Render() override;

    private:
        void SetCameraCapture(bool captured);
        void RequestScenePick(const ImVec2 &image_min, const ImVec2 &image_size,
                             const graphics::RenderTargetView &view) const;

        render::RenderSystem *render_system_ = nullptr;
        IEditorImguiRenderer *imgui_renderer_ = nullptr;
        WindowSystem *window_system_ = nullptr;
        input::InputSystem *input_system_ = nullptr;
        runtime::ISceneCameraControlSink *camera_control_sink_ = nullptr;
        runtime::ISceneSelectionSink *scene_selection_sink_ = nullptr;
        std::unique_ptr<EditorTransformGizmo> transform_gizmo_;
        bool camera_capture_active_ = false;
    };
}

#endif
