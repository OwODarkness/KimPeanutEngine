#include "editor/ui/editor_ui.h"
#include "editor/ui/editor_theme.h"

#include <chrono>
#include <imgui.h>
#include <optional>
#include <stdexcept>
#include "config/path.h"
#include "editor/platform/editor_imgui_glfw_wsi.h"
#include "editor/platform/editor_imgui_opengl_renderer.h"
#include "editor/platform/editor_imgui_vulkan_renderer.h"
#include "editor/ui/component/editor_window_component.h"
#include "editor/ui/component/editor_camera_settings_component.h"
#include "editor/ui/component/editor_loading_component.h"
#include "editor/ui/component/editor_startup_profiler_component.h"
#include "editor/ui/component/editor_console_component.h"
#include "editor/ui/component/editor_debug_viewer_component.h"
#include "editor/ui/component/editor_gpu_profiler_component.h"
#include "editor/ui/component/editor_menubar_component.h"
#include "editor/ui/component/editor_tool_row_component.h"
#include "editor/ui/component/editor_viewport_component.h"
#include "editor/ui/editor_extension_registry.h"
#include "editor/log/editor_log_component.h"
#include "editor/settings/editor_layout_settings.h"
#include "editor/settings/editor_settings.h"
#include "editor/profile/editor_builtin_metrics.h"
#include "editor/profile/editor_profile_bar.h"
#include "editor/actor/actor_editor_model.h"
#include "editor/actor/editor_world_outliner_component.h"
#include "editor/actor/editor_actor_inspector_component.h"
#include "platform/memory_stats_sampler.h"
#include "runtime/engine.h"
#include "runtime/render/render_system.h"
#include "runtime/runtime_camera_control.h"
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

    void EditorUI::InitializeViewer(const EditorUIInitInfo &init_info,
                                    std::function<void()> viewer_content)
    {
        InitializePresentation(init_info);
        viewer_content_ = std::move(viewer_content);
        viewer_mode_ = true;
        workspace_promoted_ = true;
        loading_components_.clear();
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
            code_font_ = ApplyCodexTheme();

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
            renderer_->SetBackgroundColor(init_info.background_color_override.has_value()
                                              ? *init_info.background_color_override
                                              : settings.background_color);

            ImGuiIO &io = ImGui::GetIO();
            io.ConfigWindowsMoveFromTitleBarOnly = true;
            io.ConfigDragClickToInputText = true;
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

            BuildLoadingTree();

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

        // One item per registered tool-row panel, built from the row rather than written
        // out by hand. Hardcoding the two original ids is exactly how this menu drifted:
        // the Debug Viewer and Performance Profiler became tabs with no View entry, and a
        // CLOSED one then had nothing that could reopen it — the terminal-state bug ED1
        // and ED2 each hit. Iterating the row cannot drift. It is also why this builder
        // now runs after BuildToolRow; the main menu bar is its own ImGui window, so its
        // position in components_ does not affect where it draws.
        //
        // Checking a panel Docks it as well as showing it, which is what makes one call
        // undo isolate AND pin. Toggling visibility alone would leave an isolated panel
        // floating and a pinned one pinned, so the menu could never bring either back.
        Menu view_menu{"View"};
        for (std::size_t index = 0; index < tool_row_model_.GetEntryCount(); ++index)
        {
            const EditorToolRowEntry *const entry = tool_row_model_.GetEntry(index);
            if (entry == nullptr)
            {
                continue;
            }
            // The id is captured by value: it must not borrow from an entry, so that a
            // later model edit cannot leave the menu holding a dangling reference.
            const std::string id = entry->id;
            view_menu.items.push_back(MenuItem{
                entry->title,
                {},
                true,
                [this, id]
                {
                    if (tool_row_model_.IsOpenById(id))
                    {
                        tool_row_model_.SetOpenById(id, false);
                    }
                    else
                    {
                        tool_row_model_.ShowInRowById(id);
                    }
                },
                [this, id] { return tool_row_model_.IsOpenById(id); },
            });
        }
        menus.push_back(std::move(view_menu));

        Menu tool_menu{"Tool"};
        tool_menu.items.push_back(MenuItem{
            "Capture Screenshot",
            {},
            screenshot_service_ != nullptr,
            [this] { TriggerScreenshot(); },
            {},
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

    std::unique_ptr<EditorWindowComponent> EditorUI::BuildViewportPanel()
    {
        // No layout slot: this panel is hosted by the dock host, which owns its rectangle.
        // A slot here would put it in the per-frame layout pass as well and draw it twice.
        auto window_component = std::make_unique<EditorWindowComponent>(
            "Viewport", EditorWindowConfig{});
        window_component->AddComponent(std::make_shared<EditorViewportComponent>(
            init_info_.render_system, renderer_.get(), init_info_.window_system,
            init_info_.input_system, init_info_.camera_control_sink,
            init_info_.scene_selection_sink, actor_model_.get()));
        return window_component;
    }

    std::unique_ptr<EditorWindowComponent> EditorUI::BuildCameraSettingsPanel()
    {
        return std::make_unique<EditorCameraSettingsComponent>(init_info_.camera_control_sink);
    }

    std::unique_ptr<EditorWindowComponent> EditorUI::BuildWorldOutlinerPanel()
    {
        return std::make_unique<EditorWorldOutlinerComponent>(*actor_model_);
    }

    std::unique_ptr<EditorWindowComponent> EditorUI::BuildActorInspectorPanel()
    {
        return std::make_unique<EditorActorInspectorComponent>(
            *actor_model_, init_info_.window_system, init_info_.input_system,
            init_info_.camera_control_sink);
    }

    void EditorUI::BuildRegisteredWorkspaceExtensions()
    {
        for (const auto &factory :
             GetEditorExtensionRegistry().SnapshotWorkspaceComponentFactories())
        {
            if (std::unique_ptr<EditorUIComponent> component =
                    factory(init_info_.render_system, renderer_.get()))
            {
                // Deliberately left top-level and self-placing. The factory returns the
                // base class and carries no id or title, and a dock entry needs both, so
                // adopting one would mean guessing an identity for it from its window
                // title. Nothing registers an extension today; when something does, the
                // factory is what should grow the id and title, not this loop.
                components_.push_back(std::move(component));
            }
        }
    }

    bool EditorUI::BuildActorTools()
    {
        const bool any_dependency = init_info_.reflection_catalog != nullptr ||
                                     init_info_.actor_snapshot_source != nullptr ||
                                     init_info_.actor_edit_sink != nullptr;
        const bool all_dependencies = init_info_.reflection_catalog != nullptr &&
                                      init_info_.actor_snapshot_source != nullptr &&
                                      init_info_.actor_edit_sink != nullptr;
        if (!any_dependency)
        {
            KP_LOG("LogEditorUI", LOG_LEVEL_WARNING,
                   "actor inspection unavailable: Runtime reflection bridge is not published");
            return false;
        }
        if (!all_dependencies)
        {
            throw std::runtime_error(
                "actor inspection requires the reflection catalog, snapshot source, and edit sink");
        }

        actor_model_ = std::make_unique<ActorEditorModel>(
            init_info_.reflection_catalog, init_info_.actor_snapshot_source,
            init_info_.actor_edit_sink);
        return true;
    }

    std::unique_ptr<EditorWindowComponent> EditorUI::BuildLogPanel(
        LogSystem *log_system, const LogLevelColorTable &log_colors)
    {
        // Geometry belongs to the tool row while the log is docked; this config only
        // describes the panel if it is ever given its own window.
        EditorWindowConfig log_config;
        log_config.width_ratio = 0.8f;
        log_config.height_ratio = 0.26f;
        log_config.pos_y_ratio = 0.7f;
        log_config.extra_flags = ImGuiWindowFlags_HorizontalScrollbar;
        return std::make_unique<EditorLogComponent>(log_system, log_colors, log_config);
    }

    std::unique_ptr<EditorWindowComponent> EditorUI::BuildConsolePanel(
        runtime::command::CommandRegistry *command_registry,
        input::InputSystem *input_system, ImFont *code_font)
    {
        return std::make_unique<EditorConsoleComponent>(command_registry, input_system,
                                                        code_font);
    }

    std::unique_ptr<EditorWindowComponent> EditorUI::BuildDebugViewerPanel()
    {
        return std::make_unique<EditorDebugViewerComponent>(init_info_.render_system,
                                                            renderer_.get());
    }

    std::unique_ptr<EditorWindowComponent> EditorUI::BuildGpuProfilerPanel()
    {
        return std::make_unique<EditorGpuProfilerComponent>(
            init_info_.engine, init_info_.render_system, this);
    }

    void EditorUI::BuildToolRow(LogSystem *log_system, const LogLevelColorTable &log_colors,
                                runtime::command::CommandRegistry *command_registry,
                                input::InputSystem *input_system, ImFont *code_font,
                                bool actor_tools_available)
    {
        // A stale model would make the host inherit visibility and placement from a tree
        // that has already been torn down.
        tool_row_model_.Clear();

        EditorWindowConfig row_config;
        row_config.width_ratio = 0.8f;
        row_config.height_ratio = 0.26f;
        row_config.pos_y_ratio = 0.7f;
        row_config.extra_flags = ImGuiWindowFlags_HorizontalScrollbar;

        auto row = std::make_unique<EditorToolRowComponent>(tool_row_model_, row_config);
        // The host draws every dock window, so it needs the resolved rectangles. Borrowed:
        // layout_ outlives the component vector, because members are destroyed in reverse
        // declaration order.
        row->SetLayoutModel(&layout_);
        // The host is the only component that needs the layout, and it is inside components_.

        // The workspace skeleton. Each panel declares the dock it starts in, which is
        // where it has always been; from here on where it lives is data, not code.
        row->AddPanel(kToolRowViewportId, "Viewport", BuildViewportPanel(),
                      /*open=*/true, EditorLayoutSlot::Viewport);
        if (actor_tools_available)
        {
            row->AddPanel(kToolRowWorldOutlinerId, "World Outliner", BuildWorldOutlinerPanel(),
                          /*open=*/true, EditorLayoutSlot::WorldOutliner);
            row->AddPanel(kToolRowActorInspectorId, "Actor Inspector",
                          BuildActorInspectorPanel(),
                          /*open=*/true, EditorLayoutSlot::ActorInspector);
        }
        row->AddPanel(kToolRowCameraSettingsId, "Camera Settings", BuildCameraSettingsPanel(),
                      /*open=*/true, EditorLayoutSlot::CameraSettings);
        row->AddPanel(kToolRowDebugViewerId, "Debug Viewer", BuildDebugViewerPanel(),
                      /*open=*/true, EditorLayoutSlot::DebugViewer);
        row->AddPanel(kToolRowGpuProfilerId, "Performance Profiler", BuildGpuProfilerPanel(),
                      /*open=*/true, EditorLayoutSlot::GpuProfiler);

        row->AddPanel(kToolRowLogId, "Log", BuildLogPanel(log_system, log_colors),
                      /*open=*/true);

        // The console keeps its historical "closed until asked for" state.
        EditorWindowComponent *const console =
            row->AddPanel(kToolRowConsoleId, "Console",
                          BuildConsolePanel(command_registry, input_system, code_font),
                          /*open=*/false);
        if (console != nullptr)
        {
            // A closed panel is not rendered, so the host must still pump the console or
            // deferred command results would be stranded until it reopens.
            row->SetPanelPump(kToolRowConsoleId,
                              [console]
                              {
                                  static_cast<EditorConsoleComponent *>(console)->Pump();
                              });
        }

        // Last, because a placement names a panel id that has to be registered first.
        std::string placement_diagnostic;
        ApplyPlacementState(loaded_layout_state_, tool_row_model_, &placement_diagnostic);
        if (!placement_diagnostic.empty())
        {
            KP_LOG("LogEditorUI", LOG_LEVEL_WARNING, "editor layout: %s",
                   placement_diagnostic.c_str());
        }

        components_.push_back(std::move(row));
    }

    void EditorUI::BuildProfileBar(runtime::Engine *engine, MemoryStatsSampler *memory_sampler,
                                   render::RenderSystem *render_system)
    {
        // Bottom status bar. Keep this surface limited to the five live headline
        // metrics; detailed pass timings and geometry counters live in GPU Profiler.
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
        profile_metrics.push_back(std::make_unique<EditorFuncMetric>(
            "CPU",
            [render_system]
            {
                char value[32]{};
                std::snprintf(value, sizeof(value), "%.2f ms",
                              render_system->GetMetrics().profile.cpu_total_ms);
                return std::string{value};
            }));
        profile_metrics.push_back(std::make_unique<EditorFuncMetric>(
            "GPU",
            [render_system]
            {
                const auto profile = render_system->GetMetrics().profile;
                double total = 0.0;
                bool measured = false;
                for (const auto &pass : profile.passes)
                {
                    if (pass.gpu_time_ms.has_value())
                    {
                        total += *pass.gpu_time_ms;
                        measured = true;
                    }
                }
                if (!measured)
                {
                    return std::string{"N/A"};
                }
                char value[32]{};
                std::snprintf(value, sizeof(value), "%.2f ms", total);
                return std::string{value};
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

    void EditorUI::PromoteToWorkspace()
    {
        if (workspace_promoted_)
        {
            return;
        }
        try
        {
            // Before building, so the first frame already uses the saved layout.
            LoadLayoutState();

            // Scene-dependent tools are deliberately created only after Runtime
            // promotes the prepared catalog to the render thread.
            const bool actor_tools_available = BuildActorTools();
            BuildRegisteredWorkspaceExtensions();
            BuildProfileBar(init_info_.engine, init_info_.memory_sampler,
                            init_info_.render_system);
            // Every workspace panel is one of the host's entries. That is what makes
            // moving one between docks an assignment rather than a change of owner.
            BuildToolRow(init_info_.log_system, log_colors_, init_info_.command_registry,
                         init_info_.input_system, code_font_, actor_tools_available);
            // AFTER the host: the View menu is generated from the registered panels, so
            // building it first would produce an empty menu. Its own ImGui window, so
            // running last changes nothing about where it draws.
            BuildMenuBar(init_info_.render_system);
            workspace_promoted_ = true;
            loading_components_.clear();

            // The loaded placements were applied while building the row, so this frame's
            // arrangement is the baseline. Without seeding it, the first render would see
            // a revision it has never saved and write the file straight back.
            saved_placement_revision_ = tool_row_model_.GetPlacementRevision();
        }
        catch (...)
        {
            components_.clear();
            actor_model_.reset();
            screenshot_service_.reset();
            layout_.ResetToDefault();
            loaded_layout_state_ = EditorLayoutState{};
            throw;
        }
    }

    void EditorUI::BeginClosing()
    {
        if (closing_)
        {
            return;
        }
        // Workspace components borrow Runtime services (the console, log, actor
        // tools, and screenshot bridge). Destroy them before RuntimeContext starts
        // releasing those services, while retaining the loading components and
        // ImGui backend for the visible closing stages.
        components_.clear();
        actor_model_.reset();
        screenshot_service_.reset();
        closing_ = true;
    }

    void EditorUI::SetActorInspectionServices(
        const reflection::IReflectionCatalog *reflection_catalog,
        gameplay::IGameplayEditorSnapshotSource *actor_snapshot_source,
        gameplay::IGameplayEditorEditSink *actor_edit_sink)
    {
        if (workspace_promoted_)
        {
            throw std::runtime_error(
                "actor inspection services cannot change after workspace promotion");
        }
        init_info_.reflection_catalog = reflection_catalog;
        init_info_.actor_snapshot_source = actor_snapshot_source;
        init_info_.actor_edit_sink = actor_edit_sink;
    }

    bool EditorUI::RenderLoading()
    {
        return RenderActiveTree();
    }

    void EditorUI::Close()
    {
        // Destroy the console before RuntimeContext tears down InputSystem or
        // the command registry. Its listener and deferred result sink are then
        // detached while both services are still alive.
        components_.clear();
        loading_components_.clear();
        actor_model_.reset();
        // The panel rects described a tree that no longer exists.
        layout_.ResetToDefault();
        workspace_promoted_ = false;
        closing_ = false;
        init_info_ = {};
        log_colors_ = {};
        code_font_ = nullptr;
        last_render_time_ms_ = 0.0;
        last_imgui_build_time_ms_ = 0.0;
        last_imgui_submit_time_ms_ = 0.0;
        viewer_mode_ = false;
        viewer_content_ = {};
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

    void EditorUI::DrawRenderTarget(const graphics::RenderTargetView &view,
                                    const ImVec2 &size)
    {
        if (renderer_ == nullptr || !view.IsValid())
        {
            ImGui::TextDisabled("Live2D render target unavailable");
            return;
        }
        renderer_->DrawSceneImage(renderer_->GetTextureID(view), size);
    }

    bool EditorUI::Render()
    {
        return RenderActiveTree();
    }

    void EditorUI::BuildLoadingTree()
    {
        loading_components_.clear();
        loading_components_.push_back(std::make_unique<EditorLoadingComponent>(
            init_info_.startup_snapshot_source));
        BuildStartupProfilerWindow();
    }

    void EditorUI::BuildStartupProfilerWindow()
    {
        loading_components_.push_back(std::make_unique<EditorStartupProfilerComponent>(
            init_info_.startup_snapshot_source, init_info_.memory_sampler));
    }

    void EditorUI::ApplyLayoutToTree()
    {
        const ImGuiViewport *const viewport = ImGui::GetMainViewport();
        const EditorRect work_area{viewport->WorkPos.x, viewport->WorkPos.y,
                                   viewport->WorkSize.x, viewport->WorkSize.y};

        // The status bar's height is content-derived, so its measurement is pushed before
        // resolving rather than living in the model as a constant.
        layout_.SetFixedExtentPixels(EditorSplitterId::StatusBar,
                                     EditorProfileBarComponent::MeasurePreferredHeightPx());
        layout_.Resolve(work_area);

        for (const std::unique_ptr<EditorUIComponent> &component : components_)
        {
            if (component == nullptr)
            {
                continue;
            }
            const std::optional<EditorLayoutSlot> slot = component->GetLayoutSlot();
            if (!slot.has_value())
            {
                continue;  // places itself: the menu bar, and anything not yet slotted
            }
            // An empty rect for a hidden region would collapse the panel, so a slot the
            // layout could not resolve pushes nullopt and the panel keeps its own
            // geometry rather than vanishing.
            if (layout_.HasSlot(*slot))
            {
                component->ApplyLayout(layout_.RectOf(*slot));
            }
            else
            {
                component->ApplyLayout(std::nullopt);
            }
        }
    }

    void EditorUI::LoadLayoutState()
    {
        // Defaults first, so a partial or absent file leaves the rest of the layout alone.
        layout_.ResetToDefault();
        loaded_layout_state_ = EditorLayoutState{};

        std::string diagnostic;
        loaded_layout_state_ =
            ReadEditorLayoutState(GetEditorLayoutPath(), &diagnostic);
        if (!diagnostic.empty())
        {
            KP_LOG("LogEditorUI", LOG_LEVEL_WARNING, "editor layout: %s", diagnostic.c_str());
        }
        ApplyLayoutState(loaded_layout_state_, layout_);
        // The placements are NOT applied here: they name tool-row panels that do not
        // exist yet. BuildToolRow applies them once every panel is registered.
    }

    void EditorUI::SaveLayoutState()
    {
        try
        {
            EditorLayoutState state = CaptureLayoutState(layout_);
            CapturePlacementState(tool_row_model_, state);
            WriteEditorLayoutState(GetEditorLayoutPath(), state);
            saved_placement_revision_ = tool_row_model_.GetPlacementRevision();
        }
        catch (const std::exception &e)
        {
            // A layout preference that cannot be saved must not take the editor down.
            KP_LOG("LogEditorUI", LOG_LEVEL_WARNING, "editor layout not saved (%s)", e.what());
        }
    }

    bool EditorUI::RenderActiveTree()
    {
        if (!imgui_context_created_ || !renderer_ || !wsi_)
        {
            return false;
        }
        const auto render_started = std::chrono::steady_clock::now();
        BeginDraw();
        if (workspace_promoted_ && actor_model_)
        {
            actor_model_->BeginFrame();
            if (init_info_.scene_selection_sink != nullptr)
            {
                for (;;)
                {
                    const std::optional<runtime::ScenePickResult> result =
                        init_info_.scene_selection_sink->ConsumeScenePickResult();
                    if (!result.has_value())
                    {
                        break;
                    }
                    if (result->hit)
                    {
                        (void)actor_model_->SelectActor(result->actor);
                    }
                    else
                    {
                        actor_model_->ClearSelection();
                    }
                }
            }
        }
        if (viewer_mode_ && !closing_ && viewer_content_)
        {
            viewer_content_();
        }
        else if (workspace_promoted_ && !closing_)
        {
            // Resolve and push geometry before drawing, then draw the seams on top
            // afterwards. The layout pass runs only for the workspace tree: the loading
            // tree and the injected viewer keep placing themselves.
            ApplyLayoutToTree();
            for (const auto &component : components_)
            {
                component->Render();
            }
            splitter_handles_.Render(layout_);
            // Persist on release, not per frame. The placement latch lives in the model,
            // which outlives the row, so nothing here holds a component pointer.
            if (splitter_handles_.ConsumeDragJustEnded() ||
                tool_row_model_.GetPlacementRevision() != saved_placement_revision_)
            {
                SaveLayoutState();
            }
        }
        else
        {
            for (const auto &component : loading_components_)
            {
                component->Render();
            }
        }
        ImGui::Render();
        const auto imgui_build_finished = std::chrono::steady_clock::now();
        renderer_->Render();
        const auto imgui_submit_finished = std::chrono::steady_clock::now();
        EndDraw();
        last_imgui_build_time_ms_ =
            std::chrono::duration<double, std::milli>(imgui_build_finished - render_started)
                .count();
        last_imgui_submit_time_ms_ =
            std::chrono::duration<double, std::milli>(imgui_submit_finished - imgui_build_finished)
                .count();
        last_render_time_ms_ =
            std::chrono::duration<double, std::milli>(imgui_submit_finished - render_started)
                .count();
        return true;
    }

    EditorUI::~EditorUI() = default;

}
