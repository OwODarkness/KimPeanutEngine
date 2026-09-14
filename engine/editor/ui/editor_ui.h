#ifndef KPENGINE_EDITOR_UI_H
#define KPENGINE_EDITOR_UI_H

#include <vector>
#include <memory>
#include <functional>
#include <optional>
#include <cstdint>
#include "base/type.h"
#include "editor/asset/asset_browser_model.h"
#include "editor/settings/editor_layout_settings.h"
#include "editor/settings/editor_settings.h"
#include "editor/ui/component/editor_layout_model.h"
#include "editor/ui/component/editor_splitter_handles.h"
#include "editor/ui/component/editor_tool_row_model.h"
#include "graphics/backend/common/editor_presentation_bridge.h"
#include "graphics/backend/common/render_target.h"
#include "runtime/runtime_startup.h"

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
            class ISceneSelectionSink;
        }
    namespace reflection
    {
        class IReflectionCatalog;
    }
    namespace gameplay
    {
        class IGameplayEditorSnapshotSource;
        class IGameplayEditorEditSink;
    }
    namespace asset
    {
        // The Asset-owned catalog boundary. Only this interface crosses into the Editor;
        // the provider that implements it stays Runtime's business.
        class IAssetCatalogSnapshotSource;
    }
}

struct ImFont;
struct ImVec2;

namespace kpengine::editor
{

    class IEditorImguiRenderer;
    class IEditorImguiWSI;
    class EditorUIComponent;
    class EditorWindowComponent;  // factory return type only; keeps imgui.h out of here
    class ActorEditorModel;

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
        runtime::ISceneSelectionSink *scene_selection_sink = nullptr;
        const reflection::IReflectionCatalog *reflection_catalog = nullptr;
        gameplay::IGameplayEditorSnapshotSource *actor_snapshot_source = nullptr;
        gameplay::IGameplayEditorEditSink *actor_edit_sink = nullptr;
        // Null is supported and expected in lifecycle tests: the browser it feeds is then
        // disabled rather than absent, so nothing has to null-check a panel.
        asset::IAssetCatalogSnapshotSource *asset_catalog_source = nullptr;
        std::function<runtime::StartupSnapshot()> startup_snapshot_source;
        std::function<std::unique_ptr<IEditorImguiRenderer>(GraphicsAPIType)>
            renderer_factory;
        std::function<std::unique_ptr<IEditorImguiWSI>()> wsi_factory;
        // Standalone hosts may use the editor's ImGui presentation backend
        // without adopting the scene editor's color policy.
        std::optional<LogColor> background_color_override;
    };

    class EditorUI
    {
    public:
        EditorUI();
        ~EditorUI();

        void Initialize(const EditorUIInitInfo &init_info);
        void InitializeViewer(const EditorUIInitInfo &init_info,
                              std::function<void()> viewer_content);
        void InitializePresentation(const EditorUIInitInfo &init_info);
        void SetActorInspectionServices(
            const reflection::IReflectionCatalog *reflection_catalog,
            gameplay::IGameplayEditorSnapshotSource *actor_snapshot_source,
            gameplay::IGameplayEditorEditSink *actor_edit_sink);
        // Pre-promotion setter, alongside SetActorInspectionServices: both hand over
        // borrowed Runtime interfaces the render thread must have before it builds the
        // workspace tools, and both must be called before PromoteToWorkspace().
        void SetAssetCatalogSnapshotSource(asset::IAssetCatalogSnapshotSource *source);
        void PromoteToWorkspace();
        void BeginClosing();
        bool RenderLoading();
        bool Render();
        void Close();
        void BeginDraw();
        void EndDraw();
        void DrawRenderTarget(const graphics::RenderTargetView &view, const ImVec2 &size);

        double GetLastRenderTimeMs() const noexcept { return last_render_time_ms_; }
        double GetLastImGuiBuildTimeMs() const noexcept
        {
            return last_imgui_build_time_ms_;
        }
        double GetLastImGuiSubmitTimeMs() const noexcept
        {
            return last_imgui_submit_time_ms_;
        }

    private:
        // Backend factory (chosen by the active graphics API) and the panel builders
        // that assemble the tool tree. Each panel is one helper — Initialize stays an
        // orchestration list instead of one long build routine.
        void CreateImguiBackends(const EditorUIInitInfo &init_info);
        void BuildMenuBar(render::RenderSystem *render_system);
        void BuildRegisteredWorkspaceExtensions();
        // Panel factories: these return a component that EditorUI does not own, so the
        // dock host can take it. No build step holds a pointer into the component tree.
        std::unique_ptr<EditorWindowComponent> BuildViewportPanel();
        std::unique_ptr<EditorWindowComponent> BuildCameraSettingsPanel();
        std::unique_ptr<EditorWindowComponent> BuildWorldOutlinerPanel();
        std::unique_ptr<EditorWindowComponent> BuildActorInspectorPanel();
        std::unique_ptr<EditorWindowComponent> BuildLogPanel(
            LogSystem *log_system, const LogLevelColorTable &log_colors);
        std::unique_ptr<EditorWindowComponent> BuildConsolePanel(
            runtime::command::CommandRegistry *command_registry,
            input::InputSystem *input_system, ImFont *code_font);
        std::unique_ptr<EditorWindowComponent> BuildDebugViewerPanel();
        std::unique_ptr<EditorWindowComponent> BuildGpuProfilerPanel();
        // The browser is a dock panel like any other; it owns no window geometry, so its
        // movability comes from being dragged between docks.
        std::unique_ptr<EditorWindowComponent> BuildAssetBrowserPanel();
        // The dock host: it owns every workspace panel and draws one window per dock.
        // Also applies the persisted placements, which is why it runs late — a placement
        // names a panel that has to be registered first.
        void BuildToolRow(LogSystem *log_system, const LogLevelColorTable &log_colors,
                          runtime::command::CommandRegistry *command_registry,
                          input::InputSystem *input_system, ImFont *code_font,
                          bool actor_tools_available);
        void BuildProfileBar(runtime::Engine *engine, MemoryStatsSampler *memory_sampler,
                             render::RenderSystem *render_system);
        // Creates the actor model the outliner and inspector borrow. False when Runtime
        // has not published the reflection bridge, in which case neither is built.
        bool BuildActorTools();
        void BuildLoadingTree();
        void BuildStartupProfilerWindow();
        bool RenderActiveTree();

        // Resolves the layout against this frame's work area and pushes a rect into every
        // component that declares a slot. Called from the workspace branch only, so the
        // loading tree keeps placing itself.
        void ApplyLayoutToTree();
        void LoadLayoutState();
        void SaveLayoutState();
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
        bool closing_ = false;
        bool viewer_mode_ = false;
        EditorUIInitInfo init_info_{};
        std::function<void()> viewer_content_;
        LogLevelColorTable log_colors_;
        ImFont *code_font_ = nullptr;
        double last_render_time_ms_ = 0.0;
        double last_imgui_build_time_ms_ = 0.0;
        double last_imgui_submit_time_ms_ = 0.0;

        // Runtime export path for the render-capture command. Borrowed service,
        // built from the render system's capture service when the UI initializes.
        std::unique_ptr<runtime::RuntimeScreenshotService> screenshot_service_;

        // Declared before the component tree so components are destroyed first;
        // all of them borrow this model and the injected Runtime interfaces.
        std::unique_ptr<ActorEditorModel> actor_model_;

        // Every workspace panel's placement, visibility, and lock. Declared before
        // components_ for the same reason, and owned here rather than by the host so the
        // View menu and the layout file can bind by id without reaching into the tree.
        EditorToolRowModel tool_row_model_;

        // The Asset Browser's state. Declared before components_ for the same reason as the
        // models above: the panel borrows it, so the panel must be destroyed first. It
        // borrows the injected catalog source, which Runtime owns and outlives this object.
        AssetBrowserModel asset_browser_model_;

        // Panel geometry, owned here because EditorUI is what resolves it and pushes a
        // rect into each component before the tree renders. ImGui-free: all of its
        // arithmetic is unit-tested and none of it needs a frame.
        EditorLayoutModel layout_;
        EditorSplitterHandles splitter_handles_;

        // The layout file as read at promotion. Held because its placements name tool-row
        // panels that do not exist until BuildToolRow runs, so they are applied after it
        // rather than inside LoadLayoutState.
        EditorLayoutState loaded_layout_state_;
        // The placement revision already persisted, so a placement change writes the file
        // once instead of every frame. Seeded after promotion, when the loaded file has
        // been applied and must not be written straight back.
        std::uint64_t saved_placement_revision_ = 0;

        std::vector<std::unique_ptr<EditorUIComponent>> components_;
        std::vector<std::unique_ptr<EditorUIComponent>> loading_components_;
    };

}

#endif //KPENGINE_EDITOR_UI_H
