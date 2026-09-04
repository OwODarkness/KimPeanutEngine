#ifndef KPENGINE_EDITOR_UI_H
#define KPENGINE_EDITOR_UI_H

#include <vector>
#include <memory>
#include <functional>
#include "base/type.h"
#include "editor/settings/editor_settings.h"
#include "graphics/backend/common/editor_presentation_bridge.h"

namespace kpengine
{
    class WindowSystem;
    class LogSystem;
    class MemoryStatsSampler;
    namespace runtime
    {
        class Engine;
        class RuntimeScreenshotService;
        namespace command
        {
            class CommandRegistry;
        }
    }
    namespace input
    {
        class InputSystem;
    }
    namespace render
    {
        class RenderSystem;
    }
    namespace runtime
    {
        class ISceneCameraControlSink;
    }
}

namespace kpengine::editor
{

    class IEditorImguiRenderer;
    class IEditorImguiWSI;
    class EditorUIComponent;

    // Parameter bundle for EditorUI::Initialize, so the signature doesn't grow with each
    // injected dependency (mirrors EditorContextInitInfo / WindowCreateInfo). Members are
    // defaulted: a null engine/memory_sampler just omits the profile bar.
    struct EditorUIInitInfo
    {
        WindowHandle window = nullptr;
        graphics::IEditorPresentationBridge *editor_presentation_bridge = nullptr;
        LogSystem *log_system = nullptr;
        runtime::Engine *engine = nullptr;
        MemoryStatsSampler *memory_sampler = nullptr;
        render::RenderSystem *render_system = nullptr;
        runtime::command::CommandRegistry *command_registry = nullptr;
        input::InputSystem *input_system = nullptr;
        WindowSystem *window_system = nullptr;
        runtime::ISceneCameraControlSink *camera_control_sink = nullptr;
        std::function<std::unique_ptr<IEditorImguiRenderer>(GraphicsAPIType)>
            renderer_factory;
        std::function<std::unique_ptr<IEditorImguiWSI>()> wsi_factory;
    };

    class EditorUI
    {
    public:
        EditorUI();
        ~EditorUI();

        void Initialize(const EditorUIInitInfo &init_info);
        void InitializePresentation(const EditorUIInitInfo &init_info);
        void PromoteToWorkspace();
        bool RenderLoading();
        bool Render();
        void Close();
        void BeginDraw();
        void EndDraw();

    private:
        // Backend factory (chosen by the active graphics API) and the panel builders
        // that assemble the tool tree. Each panel is one helper — Initialize stays an
        // orchestration list instead of one long build routine.
        void CreateImguiBackends(const EditorUIInitInfo &init_info);
        void BuildMenuBar(render::RenderSystem *render_system);
        void BuildViewportWindow(render::RenderSystem *render_system,
                                 WindowSystem *window_system,
                                 input::InputSystem *input_system,
                                 runtime::ISceneCameraControlSink *camera_control_sink);
        void BuildLogWindow(LogSystem *log_system, const LogLevelColorTable &log_colors);
        void BuildProfileBar(runtime::Engine *engine, MemoryStatsSampler *memory_sampler,
                             render::RenderSystem *render_system);
        void BuildConsole(runtime::command::CommandRegistry *command_registry,
                          input::InputSystem *input_system);
        // Binds the Tool > Capture Screenshot command to the runtime export path.
        void TriggerScreenshot();

        // The UI is decoupled from any graphics API: the WSI feeds ImGui window
        // events, the renderer draws ImGui with the active backend (GL/Vulkan).
        std::unique_ptr<IEditorImguiRenderer> renderer_;
        std::unique_ptr<IEditorImguiWSI> wsi_;
        bool imgui_context_created_ = false;
        bool renderer_init_attempted_ = false;
        bool renderer_initialized_ = false;
        bool wsi_init_attempted_ = false;
        bool wsi_initialized_ = false;
        bool workspace_promoted_ = false;
        EditorUIInitInfo init_info_{};
        LogLevelColorTable log_colors_;

        // Runtime export path for the render-capture command. Borrowed service,
        // built from the render system's capture service when the UI initializes.
        std::unique_ptr<runtime::RuntimeScreenshotService> screenshot_service_;

        std::vector<std::unique_ptr<EditorUIComponent>> components_;
    };

}

#endif //KPENGINE_EDITOR_UI_H
