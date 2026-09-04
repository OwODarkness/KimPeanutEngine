#include "editor/ui/editor_ui.h"

#include <imgui.h>
#include <stdexcept>
#include "config/path.h"
#include "editor/platform/editor_imgui_glfw_wsi.h"
#include "editor/platform/editor_imgui_opengl_renderer.h"
#include "editor/platform/editor_imgui_vulkan_renderer.h"
#include "editor/ui/component/editor_window_component.h"
#include "editor/ui/component/editor_console_component.h"
#include "editor/ui/component/editor_menubar_component.h"
#include "editor/ui/component/editor_viewport_component.h"
#include "editor/log/editor_log_component.h"
#include "editor/settings/editor_settings.h"
#include "editor/profile/editor_builtin_metrics.h"
#include "editor/profile/editor_profile_bar.h"
#include "platform/memory_stats_sampler.h"
#include "runtime/engine.h"
#include "runtime/render/render_system.h"
#include "runtime/screenshot/runtime_screenshot_service.h"
#include "log/logger.h"

namespace kpengine::editor
{
    EditorUI::EditorUI() = default;

    void EditorUI::Initialize(const EditorUIInitInfo &init_info)
    {
        InitializePresentation(init_info);
        try
        {
            PromoteToWorkspace();
        }
        catch (...)
        {
            Close();
            throw;
        }
    }

    void EditorUI::InitializePresentation(const EditorUIInitInfo &init_info)
    {
        if (imgui_context_created_ || renderer_ || wsi_ || !components_.empty())
        {
            Close();
        }
        try
        {
            init_info_ = init_info;
            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            imgui_context_created_ = true;
            ImGui::StyleColorsDark();

            CreateImguiBackends(init_info);

            EditorSettings settings{};
            settings.log_colors = DefaultLogColors();
            try
            {
                settings = ReadEditorSettings(GetSettingsPath());
            }
            catch (const std::exception &e)
            {
                KP_LOG("LogEditorUI", LOG_LEVEL_WARNING,
                       "editor settings unavailable (%s), using defaults", e.what());
            }
            log_colors_ = settings.log_colors;
            renderer_->SetBackgroundColor(settings.background_color);

            ImGuiIO &io = ImGui::GetIO();
            io.ConfigWindowsMoveFromTitleBarOnly = true;
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        }
        catch (const std::exception &e)
        {
            Close();
            KP_LOG("LogEditorUI", LOG_LEVEL_WARNING,
                   "editor UI initialization rolled back (%s)", e.what());
            throw;
        }
        catch (...)
        {
            Close();
            throw;
        }
    }

    void EditorUI::CreateImguiBackends(const EditorUIInitInfo &init_info)
    {
        // This module owns WSI + renderer selection. EditorLib only orchestrates
        // tools through EditorUI and therefore stays decoupled from GL/Vulkan.
        if (init_info.editor_presentation_bridge == nullptr)
        {
            throw std::runtime_error("Editor presentation bridge is unavailable");
        }
        wsi_ = init_info.wsi_factory ? init_info.wsi_factory()
                                     : std::make_unique<EditorImguiGLFWWSI>();
        if (!wsi_)
        {
            throw std::runtime_error("Editor ImGui window backend is unavailable");
        }
        if (init_info.renderer_factory)
        {
            renderer_ = init_info.renderer_factory(
                init_info.editor_presentation_bridge->GetGraphicsAPI());
        }
        else if (init_info.editor_presentation_bridge->GetGraphicsAPI() ==
                 GraphicsAPIType::GRAPHICS_API_OPENGL)
        {
            renderer_ = std::make_unique<EditorImguiOpenglRenderer>();
        }
        else if (init_info.editor_presentation_bridge->GetGraphicsAPI() ==
                 GraphicsAPIType::GRAPHICS_API_VULKAN)
        {
            renderer_ = std::make_unique<EditorImguiVulkanRenderer>();
        }

        if (!renderer_)
        {
            throw std::runtime_error("Unsupported graphics API for Editor UI");
        }
        renderer_init_attempted_ = true;
        if (!renderer_->Initialize(init_info.editor_presentation_bridge))
        {
            throw std::runtime_error("Editor ImGui renderer initialization failed");
        }
        renderer_initialized_ = true;
        wsi_init_attempted_ = true;
        if (!wsi_->Initialize(init_info.window,
                              init_info.editor_presentation_bridge->GetGraphicsAPI()))
        {
            throw std::runtime_error("Editor ImGui window backend initialization failed");
        }
        wsi_initialized_ = true;
    }

    void EditorUI::BuildMenuBar(render::RenderSystem *render_system)
    {
        // Render-capture is the first bound action: RenderSystem owns the capture
        // service wired to the scene target, so the editor reuses the runtime
        // screenshot export path without touching Render or Graphics internals.
        if (render_system && render_system->GetRenderCaptureService())
        {
            screenshot_service_ = std::make_unique<runtime::RuntimeScreenshotService>(
                *render_system->GetRenderCaptureService());
        }

        // Top-level menu bar first so it draws above the tool windows.
        std::vector<Menu> menus;
        menus.push_back(Menu{"File"});
        menus.push_back(Menu{"Edit"});
        Menu tool_menu{"Tool"};
        tool_menu.items.push_back(MenuItem{
            "Capture Screenshot",
            {},
            false,
            screenshot_service_ != nullptr,
            [this] { TriggerScreenshot(); },
        });
        menus.push_back(std::move(tool_menu));
        menus.push_back(Menu{"Help"});
        components_.push_back(std::make_unique<EditorMainMenuBarComponent>(menus));
    }

    void EditorUI::TriggerScreenshot()
    {
        if (!screenshot_service_)
        {
            return;
        }
        // Empty output path selects a UTC name below save/screenshots/; the
        // export service owns naming, directory creation, and file I/O. The
        // completion callback lands on the render thread when the readback
        // resolves, so it only logs — it never touches ImGui state.
        screenshot_service_->RequestScreenshot(
            {}, [](runtime::ScreenshotResult result)
            {
                if (result.IsSuccess())
                {
                    KP_LOG("LogEditorUI", LOG_LEVEL_INFO, "Screenshot saved: %s",
                           result.output_path.c_str());
                }
                else
                {
                    KP_LOG("LogEditorUI", LOG_LEVEL_WARNING, "Screenshot failed: %s",
                           result.diagnostic.c_str());
                }
            });
    }

    void EditorUI::BuildViewportWindow(
        render::RenderSystem *render_system, WindowSystem *window_system,
        input::InputSystem *input_system,
        runtime::ISceneCameraControlSink *camera_control_sink)
    {
        EditorWindowConfig config;
        config.height_ratio = 0.7f;
        std::unique_ptr<EditorWindowComponent> window_component =
            std::make_unique<EditorWindowComponent>("Viewport", config);
        window_component->AddComponent(std::make_shared<EditorViewportComponent>(
            render_system, renderer_.get(), window_system, input_system, camera_control_sink));

        components_.push_back(std::move(window_component));
    }

    void EditorUI::BuildLogWindow(LogSystem *log_system, const LogLevelColorTable &log_colors)
    {
        // Log console: top 30% of the viewport, full width.
        EditorWindowConfig log_config;
        log_config.height_ratio = 0.27f;
        log_config.pos_y_ratio = 0.7f;
        components_.push_back(std::make_unique<EditorLogComponent>(log_system, log_colors, log_config));
    }

    void EditorUI::BuildProfileBar(runtime::Engine *engine, MemoryStatsSampler *memory_sampler,
                                   render::RenderSystem *render_system)
    {
        // Bottom status bar. Metrics are injected via samplers, so the bar never sees
        // the engine or the OS — FPS from the engine (render-thread counter, race-free
        // here), memory from the platform sampler via EditorContext.
        if (!engine || !memory_sampler || !render_system)
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
        profile_metrics.push_back(std::make_unique<EditorFuncMetric>(
            "Shaders",
            [render_system]
            { return std::to_string(render_system->GetMetrics().prepared_shader_count); }));
        components_.push_back(
            std::make_unique<EditorProfileBarComponent>(std::move(profile_metrics)));
    }

    void EditorUI::PromoteToWorkspace()
    {
        if (workspace_promoted_)
        {
            return;
        }
        try
        {
            // Scene-dependent tools are deliberately created only after Runtime
            // promotes the prepared catalog to the render thread.
            BuildMenuBar(init_info_.render_system);
            BuildViewportWindow(init_info_.render_system, init_info_.window_system,
                                init_info_.input_system, init_info_.camera_control_sink);
            BuildLogWindow(init_info_.log_system, log_colors_);
            BuildProfileBar(init_info_.engine, init_info_.memory_sampler,
                            init_info_.render_system);
            BuildConsole(init_info_.command_registry, init_info_.input_system);
            workspace_promoted_ = true;
        }
        catch (...)
        {
            components_.clear();
            screenshot_service_.reset();
            throw;
        }
    }

    bool EditorUI::RenderLoading()
    {
        if (!imgui_context_created_ || !renderer_ || !wsi_)
        {
            return false;
        }
        BeginDraw();
        ImGui::SetNextWindowPos(ImVec2(24.0f, 24.0f), ImGuiCond_Once);
        ImGui::Begin("Loading");
        ImGui::TextUnformatted("Loading startup assets...");
        ImGui::End();
        ImGui::Render();
        renderer_->Render();
        return true;
    }

    void EditorUI::BuildConsole(runtime::command::CommandRegistry *command_registry,
                                input::InputSystem *input_system)
    {
        components_.push_back(std::make_unique<EditorConsoleComponent>(
            command_registry, input_system));
    }

    void EditorUI::Close()
    {
        // Destroy the console before RuntimeContext tears down InputSystem or
        // the command registry. Its listener and deferred result sink are then
        // detached while both services are still alive.
        components_.clear();
        workspace_promoted_ = false;
        init_info_ = {};
        log_colors_ = {};
        screenshot_service_.reset();
        if (wsi_ && wsi_init_attempted_)
        {
            wsi_->Shutdown();
        }
        wsi_initialized_ = false;
        if (renderer_ && renderer_init_attempted_)
        {
            renderer_->Shutdown();
        }
        renderer_initialized_ = false;
        renderer_init_attempted_ = false;
        wsi_init_attempted_ = false;
        if (imgui_context_created_)
        {
            ImGui::DestroyContext();
            imgui_context_created_ = false;
        }
        renderer_.reset();
        wsi_.reset();
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
