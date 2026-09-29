#include "terrain_editor.h"

#include <algorithm>
#include <exception>
#include <functional>

#include <imgui.h>

#include "editor/log/editor_log_component.h"
#include "editor/settings/editor_settings.h"
#include "editor/ui/component/editor_gpu_profiler_component.h"
#include "editor/ui/component/editor_tool_row_component.h"
#include "editor/ui/component/editor_window_component.h"
#include "editor/ui/editor_ui.h"
#include "render/render_system.h"
#include "runtime/engine.h"
#include "runtime/runtime_global_context.h"
#include "runtime/window/window_system.h"
#include "product/terrain_core.h"

namespace kpengine::terrain
{
    namespace
    {
        class TerrainEditorDockPanel final : public editor::EditorWindowComponent
        {
        public:
            TerrainEditorDockPanel(std::string title, std::function<void()> render_content)
                : EditorWindowComponent(title), render_content_(std::move(render_content))
            {
            }

            void RenderContent() override
            {
                if (render_content_)
                {
                    render_content_();
                }
            }

        private:
            std::function<void()> render_content_;
        };

        ImVec2 FitView(const graphics::RenderTargetView &view, const ImVec2 &available)
        {
            if (!view.IsValid() || available.x <= 0.0f || available.y <= 0.0f)
            {
                return available;
            }
            const float aspect = static_cast<float>(view.width) /
                                 static_cast<float>(view.height);
            const float available_aspect = available.x / available.y;
            return available_aspect > aspect
                       ? ImVec2(available.y * aspect, available.y)
                       : ImVec2(available.x, available.x / aspect);
        }
    }

    TerrainEditor::TerrainEditor() = default;

    TerrainEditor::~TerrainEditor()
    {
        Shutdown();
    }

    bool TerrainEditor::Initialize(
        runtime::Engine &engine,
        std::shared_ptr<const ScalarField2D> heightfield,
        std::string &diagnostic)
    {
        diagnostic.clear();
        runtime::RuntimeContext &context = runtime::global_runtime_context;
        if (!heightfield || context.window_system_ == nullptr ||
            context.render_system_ == nullptr || context.log_system_ == nullptr)
        {
            diagnostic = "Terrain editor presentation services are unavailable";
            return false;
        }

        heightfield_ = std::move(heightfield);
        editor::EditorUIInitInfo init_info{};
        init_info.window = context.window_system_->GetNativeHandle();
        init_info.editor_presentation_bridge =
            context.render_system_->GetEditorPresentationBridge();
        init_info.log_system = context.log_system_.get();
        init_info.engine = &engine;
        init_info.render_system = context.render_system_.get();
        init_info.window_system = context.window_system_.get();

        try
        {
            ui_ = std::make_unique<editor::EditorUI>();
            ui_->InitializeViewer(init_info, [this] { RenderPanels(); });
            log_panel_ = std::make_unique<editor::EditorLogComponent>(
                context.log_system_.get(), editor::DefaultLogColors());
            performance_panel_ =
                std::make_unique<editor::EditorGpuProfilerComponent>(
                    &engine, context.render_system_.get(), ui_.get());

            layout_.ResetToCompactViewerDefault();
            dock_model_.Clear();
            dock_host_ = std::make_unique<editor::EditorToolRowComponent>(
                dock_model_, editor::EditorWindowConfig{});
            dock_host_->SetLayoutModel(&layout_);
            dock_host_->AddPanel(
                "terrain_view", "Terrain View",
                std::make_unique<TerrainEditorDockPanel>(
                    "Terrain View", [this] { RenderTerrainView(); }),
                true, editor::EditorLayoutSlot::Viewport);
            dock_host_->AddPanel(
                "heightmap_debug", "Heightmap Debug",
                std::make_unique<TerrainEditorDockPanel>(
                    "Heightmap Debug", [this] { heightmap_view_.RenderContent(heightfield_.get()); }),
                true, editor::EditorLayoutSlot::DebugViewer);
            dock_host_->AddPanel(
                "terrain_performance", "Performance Profiler",
                std::make_unique<TerrainEditorDockPanel>(
                    "Performance Profiler",
                    [this] { performance_panel_->RenderContent(); }),
                true, editor::EditorLayoutSlot::GpuProfiler);
            dock_host_->AddPanel(
                "terrain_log", "Log",
                std::make_unique<TerrainEditorDockPanel>(
                    "Log", [this] { log_panel_->RenderContent(); }),
                true, editor::EditorLayoutSlot::ToolRow);
            dock_host_->AddPanel(
                "terrain_controls", "Terrain Controls",
                std::make_unique<TerrainEditorDockPanel>(
                    "Terrain Controls", [this] { RenderControlPanel(); }),
                true, editor::EditorLayoutSlot::ToolRow);
        }
        catch (const std::exception &error)
        {
            diagnostic = std::string("Terrain editor UI initialization failed: ") +
                         error.what();
            Shutdown();
            return false;
        }
        return true;
    }

    bool TerrainEditor::Render(std::string &diagnostic)
    {
        diagnostic.clear();
        if (!ui_ || !ui_->Render())
        {
            diagnostic = "Terrain editor ImGui presentation failed";
            return false;
        }
        return true;
    }

    void TerrainEditor::Shutdown() noexcept
    {
        if (ui_)
        {
            ui_->Close();
            ui_.reset();
        }
        dock_host_.reset();
        performance_panel_.reset();
        log_panel_.reset();
        heightfield_.reset();
    }

    void TerrainEditor::RenderPanels()
    {
        ApplyLayout();
        if (dock_host_)
        {
            dock_host_->Render();
        }
        splitter_handles_.Render(layout_);
    }

    void TerrainEditor::ApplyLayout()
    {
        const ImGuiViewport *const viewport = ImGui::GetMainViewport();
        layout_.Resolve({viewport->WorkPos.x, viewport->WorkPos.y,
                         viewport->WorkSize.x, viewport->WorkSize.y});
    }

    void TerrainEditor::RenderTerrainView()
    {
        render::RenderSystem *const render_system =
            runtime::global_runtime_context.render_system_.get();
        const ImVec2 available = ImGui::GetContentRegionAvail();
        if (render_system != nullptr && available.x > 0.0f && available.y > 0.0f)
        {
            render_system->RequestSceneRenderTargetExtent(
                static_cast<std::uint32_t>(available.x),
                static_cast<std::uint32_t>(available.y));
            const graphics::RenderTargetView view = render_system->GetSceneRenderTargetView();
            const ImVec2 image_size = FitView(view, available);
            ImGui::SetCursorPos(ImVec2(
                ImGui::GetCursorPosX() + std::max(0.0f, (available.x - image_size.x) * 0.5f),
                ImGui::GetCursorPosY() + std::max(0.0f, (available.y - image_size.y) * 0.5f)));
            ui_->DrawRenderTarget(view, image_size);
        }
    }

    void TerrainEditor::RenderControlPanel()
    {
    }
}
