#include "editor/ui/editor_ui.h"

#include <imgui.h>
#include "config/path.h"
#include "editor/platform/editor_imgui_glfw_wsi.h"
#include "editor/platform/editor_imgui_opengl_renderer.h"
#include "editor/platform/editor_imgui_vulkan_renderer.h"
#include "editor/ui/component/editor_window_component.h"
#include "editor/ui/component/editor_text_component.h"
#include "editor/ui/component/editor_menubar_component.h"
#include "editor/log/editor_log_component.h"
#include "editor/settings/editor_settings.h"
#include "editor/profile/editor_builtin_metrics.h"
#include "editor/profile/editor_profile_bar.h"
#include "platform/memory_stats_sampler.h"
#include "runtime/engine.h"
#include "log/logger.h"

namespace kpengine::editor
{
    EditorUI::EditorUI() = default;

    void EditorUI::Initialize(const EditorUIInitInfo &init_info)
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::StyleColorsDark();

        CreateImguiBackends(init_info.window, init_info.backend_type);

        ImGuiIO &io = ImGui::GetIO();
        io.ConfigWindowsMoveFromTitleBarOnly = true;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        // Tool tree. Each panel is built by its own helper so Initialize stays an
        // orchestration list — add a panel as one more call, not more inline code.
        BuildMenuBar();
        BuildPlaceholderWindow();
        BuildLogWindow(init_info.log_system);
        BuildProfileBar(init_info.engine, init_info.memory_sampler);
    }

    void EditorUI::CreateImguiBackends(WindowHandle window, GraphicsAPIType backend_type)
    {
        // WSI + renderer are chosen by the active graphics API, keeping the editor UI
        // decoupled from GL/Vulkan (the reconstruction seam).
        wsi_ = std::make_unique<EditorImguiGLFWWSI>();
        if (backend_type == GraphicsAPIType::GRAPHICS_API_OPENGL)
        {
            renderer_ = std::make_unique<EditorImguiOpenglRenderer>();
        }
        else if (backend_type == GraphicsAPIType::GRAPHICS_API_VULKAN)
        {
            renderer_ = std::make_unique<EditorImguiVulkanRenderer>();
        }

        GraphicsContext context{backend_type, nullptr};
        renderer_->Initialize(context);
        wsi_->Initialize(window, backend_type);
    }

    void EditorUI::BuildMenuBar()
    {
        // Top-level menu bar first so it draws above the tool windows. Menus have no
        // items yet — bare File/Edit/Tool/Help, no event bindings.
        std::vector<Menu> menus;
        menus.push_back(Menu{"File"});
        menus.push_back(Menu{"Edit"});
        menus.push_back(Menu{"Tool"});
        menus.push_back(Menu{"Help"});
        components_.push_back(std::make_unique<EditorMainMenuBarComponent>(menus));
    }

    void EditorUI::BuildPlaceholderWindow()
    {
        // A label window as the placeholder tool surface. Replace with real panels
        // (scene view, inspector, ...) as they land — each its own builder.
                EditorWindowConfig config;
        config.height_ratio = 0.5f;
        std::unique_ptr<EditorWindowComponent> window_component =
            std::make_unique<EditorWindowComponent>("window", config);
        window_component->AddComponent(std::make_shared<EditorTextComponent>("hello imgui"));

        components_.push_back(std::move(window_component ));
    }

    void EditorUI::BuildLogWindow(LogSystem *log_system)
    {
        // Re-reads the runtime log system each frame (ImGui immediate mode). Entry
        // colors come from config/settings.json — cosmetic, defaults on any failure.
        LogLevelColorTable log_colors = DefaultLogColors();
        try
        {
            log_colors = ReadEditorSettings(GetSettingsPath()).log_colors;
        }
        catch (const std::exception &e)
        {
            KP_LOG("LogEditorUI", LOG_LEVEL_WARNING,
                   "editor settings unavailable (%s), using default log colors", e.what());
        }
        // Log console: top 30% of the viewport, full width.
        EditorWindowConfig log_config;
        log_config.height_ratio = 0.26f;
        log_config.pos_y_ratio = 0.7f;
        components_.push_back(std::make_unique<EditorLogComponent>(log_system, log_colors, log_config));
    }

    void EditorUI::BuildProfileBar(runtime::Engine *engine, MemoryStatsSampler *memory_sampler)
    {
        // Bottom status bar. Metrics are injected via samplers, so the bar never sees
        // the engine or the OS — FPS from the engine (render-thread counter, race-free
        // here), memory from the platform sampler via EditorContext.
        if (!engine || !memory_sampler)
        {
            return;
        }

        std::vector<std::unique_ptr<EditorMetric>> profile_metrics;
        profile_metrics.push_back(std::make_unique<EditorFPSMetric>(
            [engine]
            { return engine->GetFPS(); }));
        profile_metrics.push_back(std::make_unique<EditorFrameTimeMetric>(
            [engine]
            {
                const int fps = engine->GetFPS();
                return fps > 0 ? 1000.f / static_cast<float>(fps) : 0.f;
            }));
        profile_metrics.push_back(std::make_unique<EditorMemoryMetric>(
            [memory_sampler]() -> EditorMemoryMetric::Stats
            {
                const MemoryStats stats = memory_sampler->Sample();
                return {stats.process_mb, stats.system_available_mb};
            }));
        components_.push_back(
            std::make_unique<EditorProfileBarComponent>(std::move(profile_metrics)));
    }

    void EditorUI::Close()
    {
        renderer_->Shutdown();
        wsi_->Shutdown();
        ImGui::DestroyContext();
        renderer_.reset();
        wsi_.reset();
        components_.clear();
    }

    void EditorUI::BeginDraw()
    {
        renderer_->NewFrame();
        wsi_->NewFrame();
        ImGui::NewFrame();
    }

    void EditorUI::EndDraw()
    {
    }

    bool EditorUI::Render()
    {
        for (const auto &component : components_)
        {
            component->Render();
        }
        ImGui::Render();
        renderer_->Render();

        return true;
    }

    EditorUI::~EditorUI() = default;

}