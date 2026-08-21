#ifndef KPENGINE_EDITOR_UI_H
#define KPENGINE_EDITOR_UI_H

#include <vector>
#include <memory>
#include "base/type.h"
#include "editor/settings/editor_settings.h"

namespace kpengine
{
    class LogSystem;
    class MemoryStatsSampler;
    namespace runtime
    {
        class Engine;
    }
    namespace render
    {
        class RenderSystem;
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
        GraphicsContext graphics_context{GraphicsAPIType::GRAPHICS_API_UNKNOW, nullptr};
        LogSystem *log_system = nullptr;
        runtime::Engine *engine = nullptr;
        MemoryStatsSampler *memory_sampler = nullptr;
        render::RenderSystem *render_system = nullptr;
    };

    class EditorUI
    {
    public:
        EditorUI();
        ~EditorUI();

        void Initialize(const EditorUIInitInfo &init_info);
        bool Render();
        void Close();
        void BeginDraw();
        void EndDraw();

    private:
        // Backend factory (chosen by the active graphics API) and the panel builders
        // that assemble the tool tree. Each panel is one helper — Initialize stays an
        // orchestration list instead of one long build routine.
        void CreateImguiBackends(WindowHandle window, GraphicsContext graphics_context);
        void BuildMenuBar();
        void BuildViewportWindow(render::RenderSystem *render_system);
        void BuildLogWindow(LogSystem *log_system, const LogLevelColorTable &log_colors);
        void BuildProfileBar(runtime::Engine *engine, MemoryStatsSampler *memory_sampler,
                             render::RenderSystem *render_system);

        // The UI is decoupled from any graphics API: the WSI feeds ImGui window
        // events, the renderer draws ImGui with the active backend (GL/Vulkan).
        std::unique_ptr<IEditorImguiRenderer> renderer_;
        std::unique_ptr<IEditorImguiWSI> wsi_;

        std::vector<std::unique_ptr<EditorUIComponent>> components_;
    };

}

#endif //KPENGINE_EDITOR_UI_H
