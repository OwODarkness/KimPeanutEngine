#include "editor/ui/editor_ui.h"

#include "editor/platform/editor_imgui_renderer.h"
#include <chrono>
#include <imgui.h>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <vector>
#include "config/path.h"
#include "editor/ui/component/editor_window_component.h"
#include "editor/ui/component/editor_layout_participant.h"
#include "editor/ui/component/editor_camera_settings_component.h"
#include "editor/ui/component/editor_loading_component.h"
#include "editor/ui/component/editor_startup_profiler_component.h"
#include "editor/ui/component/editor_console_component.h"
#include "debug_viewer_view.h"
#include "gpu_profiler_view.h"
#include "editor/asset/editor_asset_browser_component.h"
#include "editor/asset/editor_asset_reference_component.h"
#include "editor/ui/component/editor_menubar_component.h"
#include "editor/ui/component/editor_tool_row_component.h"
#include "editor/ui/component/editor_viewport_component.h"
#include "editor/ui/editor_extension_registry.h"
#include "editor/log/editor_log_component.h"
#include "editor/settings/editor_layout_settings.h"
#include "editor/settings/editor_settings.h"
#include "editor_settings_model.h"
#include "editor/settings/editor_settings_window.h"
#include "editor/profile/editor_builtin_metrics.h"
#include "editor/profile/editor_profile_bar.h"
#include "editor/profile/editor_profile_metrics.h"
#include "editor/actor/actor_editor_model.h"
#include "editor/actor/editor_world_outliner_component.h"
#include "editor/actor/editor_actor_inspector_component.h"
#include "platform/memory_stats_sampler.h"
#include "runtime/engine.h"
#include "runtime/render/render_system.h"
#include "asset/shader.h"
#include "asset/shader_program.h"
#include "render/prepared_render_asset_catalog.h"
#include "runtime/runtime_camera_control.h"
#include "runtime/screenshot/runtime_screenshot_service.h"
#include "log/logger.h"

namespace kpengine::editor
{
    namespace
    {
        constexpr std::size_t kMaximumQueuedPanelCommands = 64u;

        class DebugViewerPanel final : public EditorWindowComponent
        {
        public:
            DebugViewerPanel(render::RenderSystem *render_system, EditorUI *editor_ui)
                : EditorWindowComponent("Debug Viewer"), view_(render_system),
                  editor_ui_(editor_ui)
            {
            }

            void UpdateContent(const bool visible) override { view_.Update(visible); }
            void RenderContent(ui::UiContext &context) override
            {
                view_.Draw(context, [this](const ui::UiImageId id,
                                           const graphics::RenderTargetView &target)
                {
                    if (editor_ui_ != nullptr) editor_ui_->RegisterRenderTargetImage(id, target);
                });
            }

        private:
            render::editor::DebugViewerView view_;
            EditorUI *editor_ui_ = nullptr;
        };

        class GpuProfilerPanel final : public EditorWindowComponent
        {
        public:
            GpuProfilerPanel(render::RenderSystem *render_system, runtime::Engine *engine,
                             const EditorUI *editor_ui)
                : EditorWindowComponent("Performance Profiler"), view_(render_system),
                  engine_(engine), editor_ui_(editor_ui)
            {
            }

            void RenderContent(ui::UiContext &context) override
            {
                runtime::Engine::FrameLoopMetrics frame{};
                if (engine_ != nullptr) frame = engine_->GetFrameLoopMetrics();
                const render::editor::SceneEditorTimingSnapshot timings{
                    .frame_total_ms = frame.frame_total_ms,
                    .game_wait_ms = frame.game_wait_ms,
                    .render_work_ms = frame.render_work_ms,
                    .frame_pacing_ms = frame.frame_pacing_ms,
                    .game_tick_work_ms = frame.game_tick_work_ms,
                    .game_tick_pacing_ms = frame.game_tick_pacing_ms,
                    .ui_total_ms = editor_ui_ != nullptr
                        ? editor_ui_->GetLastRenderTimeMs() : 0.0,
                    .ui_build_ms = editor_ui_ != nullptr
                        ? editor_ui_->GetLastImGuiBuildTimeMs() : 0.0,
                    .ui_submit_ms = editor_ui_ != nullptr
                        ? editor_ui_->GetLastImGuiSubmitTimeMs() : 0.0};
                view_.Draw(context, timings);
            }

        private:
            render::editor::GpuProfilerView view_;
            runtime::Engine *engine_ = nullptr;
            const EditorUI *editor_ui_ = nullptr;
        };

        std::string PanelLocation(const EditorToolRowEntry &entry)
        {
            return entry.dock.has_value() ?
                       std::string{EditorLayoutModel::RegionKey(*entry.dock)} :
                       std::string{"floating"};
        }

        bool IsActivePanel(const EditorToolRowModel &model, std::size_t index,
                           const EditorToolRowEntry &entry)
        {
            const std::optional<std::size_t> active =
                entry.dock.has_value() ? model.GetActiveInDock(*entry.dock) : std::nullopt;
            return active.has_value() && *active == index;
        }

        std::string FormatPanelState(const EditorToolRowModel &model, std::size_t index)
        {
            const EditorToolRowEntry *const entry = model.GetEntry(index);
            if (entry == nullptr)
            {
                return "<invalid panel>";
            }

            std::ostringstream output;
            output << "id=" << entry->id << " title=\"" << entry->title << "\""
                   << " open=" << (entry->visibility.IsOpen() ? "true" : "false")
                   << " location=" << PanelLocation(*entry)
                   << " active=" << (IsActivePanel(model, index, *entry) ? "true" : "false")
                   << " locked=" << (model.IsLocked(index) ? "true" : "false");
            return output.str();
        }

        runtime::command::CommandResult PanelResult(
            const EditorToolRowModel &model, std::size_t index, std::string message,
            std::uint64_t request_id)
        {
            const EditorToolRowEntry *const entry = model.GetEntry(index);
            if (entry == nullptr)
            {
                return {runtime::command::CommandStatus::Failed,
                        "Editor panel disappeared while applying the command", request_id, {}};
            }
            return {runtime::command::CommandStatus::Success,
                    std::move(message),
                    request_id,
                    {{"id", entry->id},
                     {"title", entry->title},
                     {"open", entry->visibility.IsOpen()},
                     {"active", IsActivePanel(model, index, *entry)},
                     {"floating", !entry->dock.has_value()},
                     {"locked", model.IsLocked(index)},
                     {"location", PanelLocation(*entry)}}};
        }

        runtime::command::CommandResult ReferenceViewerResult(
            bool open, std::string message, std::uint64_t request_id)
        {
            return {runtime::command::CommandStatus::Success,
                    std::move(message),
                    request_id,
                    {{"id", kEditorAssetReferenceViewerId},
                     {"title", "Asset Reference Viewer"},
                     {"open", open},
                     {"active", open},
                     {"floating", true},
                     {"locked", false},
                     {"location", "floating"}}};
        }
    }

    EditorUI::EditorUI() : asset_reference_model_(asset_browser_model_)
    {
    }

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

    void EditorUI::InitializeUiViewer(
        const EditorUIInitInfo &init_info,
        std::function<void(ui::UiContext &)> viewer_content)
    {
        InitializePresentation(init_info);
        ui_viewer_content_ = std::move(viewer_content);
        viewer_mode_ = true;
        workspace_promoted_ = true;
        loading_components_.clear();
    }

    void EditorUI::InitializePresentation(const EditorUIInitInfo &init_info)
    {
        if (presentation_host_.IsInitialized() || !components_.empty())
        {
            Close();
        }
        try
        {
            init_info_ = init_info;
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
            presentation_host_.Initialize({
                .window = init_info.window,
                .presentation_bridge = init_info.editor_presentation_bridge,
                .renderer_factory = init_info.renderer_factory,
                .wsi_factory = init_info.wsi_factory,
                .background_color = init_info.background_color_override.has_value()
                    ? *init_info.background_color_override : settings.background_color});
            if (init_info.snapshot_store != nullptr)
                presentation_host_.SetSnapshotSource(*init_info.snapshot_store, "editor",
                    init_info.engine != nullptr ? init_info.engine->GetApplicationMode() : "editor", {});

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

        settings_model_ = std::make_unique<SettingsEditorModel>();
        if (init_info_.settings_access != nullptr)
        {
            settings_model_->Refresh(*init_info_.settings_access);
        }
        settings_window_ = std::make_unique<SettingsEditorWindow>(
            *settings_model_, init_info_.settings_access);
        Menu settings_menu{"Settings"};
        settings_menu.items.push_back(MenuItem{
            "Open Settings",
            {},
            true,
            [this]
            {
                if (settings_window_ != nullptr)
                {
                    settings_window_->Open();
                }
            },
            [this]
            {
                return settings_window_ != nullptr && settings_window_->IsVisible();
            },
        });
        menus.push_back(std::move(settings_menu));

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
                        for (const auto &component : components_)
                        {
                            auto *const row = dynamic_cast<EditorToolRowComponent *>(component.get());
                            if (row != nullptr && row->RequestCloseById(id))
                            {
                                break;
                            }
                        }
                    }
                    else
                    {
                        tool_row_model_.ShowInRowById(id);
                    }
                },
                [this, id] { return tool_row_model_.IsOpenById(id); },
            });
        }
        view_menu.items.push_back(MenuItem{
            "Asset Reference Viewer",
            {},
            true,
            [this]
            {
                if (asset_reference_visibility_.IsOpen())
                {
                    asset_reference_visibility_.SetOpen(false);
                }
                else
                {
                    OpenAssetReferenceViewer();
                }
            },
            [this] { return asset_reference_visibility_.IsOpen(); },
        });
        menus.push_back(std::move(view_menu));

        Menu tool_menu{"Tool"};
        tool_menu.items.push_back(MenuItem{
            "Capture Screenshot",
            {},
            screenshot_service_ != nullptr,
            [this] { TriggerScreenshot(); },
            {},
        });
        tool_menu.items.push_back(MenuItem{
            "UI Glow",
            {},
            true,
            [this] { glow_enabled_ = !glow_enabled_; },
            [this] { return glow_enabled_; },
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

    void EditorUI::OpenAssetReferenceViewer()
    {
        if (asset_reference_model_.RootKey().empty())
        {
            // Resolve the runtime-owned level through the captured snapshot. A live asset
            // can be merged into an archive product, so constructing a runtime key directly
            // would miss the canonical product node and lose its dependency edges.
            const asset::AssetCatalogSnapshot *const snapshot = asset_browser_model_.Snapshot();
            if (active_level_asset_pack_.has_value() && snapshot != nullptr)
            {
                for (const asset::AssetCatalogNode &node : snapshot->nodes)
                {
                    if (node.packed_runtime_asset_id.has_value() &&
                        *node.packed_runtime_asset_id == *active_level_asset_pack_ &&
                        node.type == asset::AssetType::KPAT_Level)
                    {
                        asset_reference_model_.SetRoot(node.stable_key);
                        break;
                    }
                }
            }

            // Keep an explicit browser selection useful in test/editor states where no
            // active level has been published. Do not silently select an unrelated material.
            if (asset_reference_model_.RootKey().empty())
            {
                if (const AssetBrowserRow *const selected = asset_browser_model_.SelectedRow();
                    selected != nullptr)
                {
                    asset_reference_model_.SetRoot(selected->stable_key);
                }
            }
        }
        asset_reference_visibility_.SetOpen(true);
    }

    void EditorUI::RegisterPanelCommands()
    {
        if (init_info_.command_registry == nullptr || !panel_command_registrations_.empty())
        {
            return;
        }

        EditorPanelCommandRegistrationResult registration = RegisterEditorPanelCommands(
            *init_info_.command_registry,
            [this](EditorPanelCommandRequest request)
            {
                return EnqueuePanelCommand(std::move(request));
            });
        if (!registration.succeeded)
        {
            KP_LOG("LogEditorUI", LOG_LEVEL_WARNING,
                   "editor panel commands unavailable: %s", registration.diagnostic.c_str());
            return;
        }
        panel_command_registrations_ = std::move(registration.registrations);
    }

    bool EditorUI::EnqueuePanelCommand(EditorPanelCommandRequest request)
    {
        if (!request.completion)
        {
            return false;
        }
        std::scoped_lock lock(panel_command_mutex_);
        if (panel_command_requests_.size() >= kMaximumQueuedPanelCommands)
        {
            return false;
        }
        panel_command_requests_.push_back(std::move(request));
        return true;
    }

    void EditorUI::DrainPanelCommands()
    {
        std::deque<EditorPanelCommandRequest> requests;
        {
            std::scoped_lock lock(panel_command_mutex_);
            requests.swap(panel_command_requests_);
        }

        for (EditorPanelCommandRequest &request : requests)
        {
            runtime::command::CommandResult result;
            if (!workspace_promoted_)
            {
                result = {runtime::command::CommandStatus::Failed,
                          "Editor workspace is not available", 0, {}};
            }
            else if (request.kind == EditorPanelCommandKind::List)
            {
                std::ostringstream message;
                for (std::size_t index = 0; index < tool_row_model_.GetEntryCount(); ++index)
                {
                    if (index != 0)
                    {
                        message << '\n';
                    }
                    message << FormatPanelState(tool_row_model_, index);
                }
                if (tool_row_model_.GetEntryCount() != 0)
                {
                    message << '\n';
                }
                message << "id=" << kEditorAssetReferenceViewerId
                        << " title=\"Asset Reference Viewer\""
                        << " open=" << (asset_reference_visibility_.IsOpen() ? "true" : "false")
                        << " location=floating active="
                        << (asset_reference_visibility_.IsOpen() ? "true" : "false")
                        << " locked=false";
                result = {runtime::command::CommandStatus::Success,
                          message.str(), 0,
                          {{"count", static_cast<std::uint64_t>(
                                         tool_row_model_.GetEntryCount() + 1u)}}};
            }
            else if (request.id == kEditorAssetReferenceViewerId)
            {
                if (request.kind == EditorPanelCommandKind::Show)
                {
                    OpenAssetReferenceViewer();
                    result = ReferenceViewerResult(true, "Editor panel shown", 0);
                }
                else if (!asset_reference_visibility_.IsOpen())
                {
                    result = {runtime::command::CommandStatus::Failed,
                              "Editor panel is not open: " + request.id, 0, {}};
                }
                else
                {
                    result = ReferenceViewerResult(true, "Editor panel focused", 0);
                }
            }
            else
            {
                const std::optional<std::size_t> index = tool_row_model_.IndexOf(request.id);
                if (!index.has_value())
                {
                    result = {runtime::command::CommandStatus::NotFound,
                              "Unknown editor panel: " + request.id, 0, {}};
                }
                else if (request.kind == EditorPanelCommandKind::Show)
                {
                    if (!tool_row_model_.ShowById(request.id))
                    {
                        result = {runtime::command::CommandStatus::Failed,
                                  "Could not show editor panel: " + request.id, 0, {}};
                    }
                    else
                    {
                        result = PanelResult(tool_row_model_, *index,
                                             "Editor panel shown", 0);
                    }
                }
                else if (!tool_row_model_.FocusById(request.id))
                {
                    result = {runtime::command::CommandStatus::Failed,
                              "Editor panel is not open: " + request.id, 0, {}};
                }
                else
                {
                    result = PanelResult(tool_row_model_, *index,
                                         "Editor panel focused", 0);
                }
            }

            runtime::command::CommandCompletionSink completion =
                std::move(request.completion);
            if (completion)
            {
                completion(std::move(result));
            }
        }
    }

    void EditorUI::FailQueuedPanelCommands(std::string_view diagnostic)
    {
        std::deque<EditorPanelCommandRequest> requests;
        {
            std::scoped_lock lock(panel_command_mutex_);
            requests.swap(panel_command_requests_);
        }

        for (EditorPanelCommandRequest &request : requests)
        {
            runtime::command::CommandCompletionSink completion =
                std::move(request.completion);
            if (completion)
            {
                completion({runtime::command::CommandStatus::Failed,
                            std::string{diagnostic}, 0, {}});
            }
        }
    }

    std::unique_ptr<EditorWindowComponent> EditorUI::BuildViewportPanel()
    {
        // No layout slot: this panel is hosted by the dock host, which owns its rectangle.
        // A slot here would put it in the per-frame layout pass as well and draw it twice.
        auto window_component = std::make_unique<EditorWindowComponent>(
            "Viewport", EditorWindowConfig{});
        window_component->AddComponent(std::make_shared<EditorViewportComponent>(
            init_info_.render_system, presentation_host_.Renderer(), init_info_.window_system,
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
                    factory(init_info_.render_system, presentation_host_.Renderer()))
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
        return std::make_unique<DebugViewerPanel>(init_info_.render_system, this);
    }

    std::unique_ptr<EditorWindowComponent> EditorUI::BuildGpuProfilerPanel()
    {
        return std::make_unique<GpuProfilerPanel>(init_info_.render_system,
                                                  init_info_.engine, this);
    }

    std::unique_ptr<EditorWindowComponent> EditorUI::BuildAssetBrowserPanel()
    {
        // The source is borrowed, and SetAssetCatalogSnapshotSource records it on init_info_
        // before promotion. A null source is a supported state: the browser then renders as
        // unavailable rather than not existing.
        asset_browser_model_.SetSource(init_info_.asset_catalog_source);
        // The one capture that is not user-requested, and the plan's only other permitted
        // one. It is synchronous and opens the archive, so it happens once, here, rather
        // than on any frame.
        (void)asset_browser_model_.Refresh();
        auto panel = std::make_unique<EditorAssetBrowserComponent>(asset_browser_model_);
        panel->SetOpenReferences(
            [this](std::string_view stable_key)
            {
                asset_reference_model_.SetRoot(std::string{stable_key});
                OpenAssetReferenceViewer();
            });
        return panel;
    }

    std::unique_ptr<EditorWindowComponent> EditorUI::BuildAssetReferencePanel()
    {
        EditorWindowConfig config;
        config.pos_x_ratio = 0.24f;
        config.pos_y_ratio = 0.14f;
        config.width_ratio = 0.56f;
        config.height_ratio = 0.68f;
        config.locked = false;
        config.extra_flags = ImGuiWindowFlags_HorizontalScrollbar;

        auto panel = std::make_unique<EditorAssetReferenceComponent>(
            asset_reference_model_, config);
        panel->SetVisibility(&asset_reference_visibility_);
        panel->SetLocateInBrowser(
            [this](std::string_view stable_key)
            {
                asset_browser_model_.Select(stable_key);
                (void)tool_row_model_.ShowInRowById(kToolRowAssetBrowserId);
            });
        return panel;
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

        // Closed until asked for, so the default workspace is unchanged: the strip shows
        // the Log alone and View > Asset Browser opens the browser into this dock.
        row->AddPanel(kToolRowAssetBrowserId, "Asset Browser", BuildAssetBrowserPanel(),
                      /*open=*/false);

        row->AddPanel(kToolRowLogId, "Log",
                      BuildLogPanel(log_system, log_colors),
                      /*open=*/true);

        // The console keeps its historical "closed until asked for" state.
        row->AddPanel(kToolRowConsoleId, "Console",
                      BuildConsolePanel(command_registry, input_system, code_font),
                      /*open=*/false);

        // Last, because a placement names a panel id that has to be registered first.
        std::string placement_diagnostic;
        ApplyPlacementState(loaded_layout_state_, tool_row_model_, &placement_diagnostic);
        if (!placement_diagnostic.empty())
        {
            KP_LOG("LogEditorUI", LOG_LEVEL_WARNING, "editor layout: %s",
                   placement_diagnostic.c_str());
        }

        components_.push_back(std::move(row));
        RegisterPanelCommands();
    }

    void EditorUI::BuildProfileBar(runtime::Engine *engine, MemoryStatsSampler *memory_sampler,
                                   render::RenderSystem *render_system)
    {
        auto profile_metrics = CreateEditorProfileMetrics(engine, memory_sampler, render_system);
        if (profile_metrics.empty())
        {
            return;
        }
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
            if (init_info_.asset_catalog_source == nullptr)
            {
                KP_LOG("LogEditorUI", LOG_LEVEL_WARNING,
                       "asset browser unavailable: the asset catalog source is not published");
            }
            BuildRegisteredWorkspaceExtensions();
            BuildProfileBar(init_info_.engine, init_info_.memory_sampler,
                            init_info_.render_system);
            // Every workspace panel is one of the host's entries. That is what makes
            // moving one between docks an assignment rather than a change of owner.
            BuildToolRow(init_info_.log_system, log_colors_, init_info_.command_registry,
                         init_info_.input_system, GetCodeFont(), actor_tools_available);
            // AFTER the host: the View menu is generated from the registered panels, so
            // building it first would produce an empty menu.
            BuildMenuBar(init_info_.render_system);
            asset_reference_panel_ = BuildAssetReferencePanel();
            workspace_promoted_ = true;
            loading_components_.clear();

            // The loaded placements were applied while building the row, so this frame's
            // arrangement is the baseline. Without seeding it, the first render would see
            // a revision it has never saved and write the file straight back.
            saved_placement_revision_ = tool_row_model_.GetPlacementRevision();
        }
        catch (...)
        {
            asset_reference_panel_.reset();
            settings_window_.reset();
            settings_model_.reset();
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
        FailQueuedPanelCommands("Editor UI is closing");
        panel_command_registrations_.clear();
        // Workspace components borrow Runtime services (the console, log, actor
        // tools, and screenshot bridge). Destroy them before RuntimeContext starts
        // releasing those services, while retaining the loading components and
        // ImGui backend for the visible closing stages.
        components_.clear();
        asset_reference_panel_.reset();
        settings_window_.reset();
        settings_model_.reset();
        asset_reference_visibility_.SetOpen(false);
        actor_model_.reset();
        screenshot_service_.reset();
        presentation_host_.BeginClosing();
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

    void EditorUI::SetAssetCatalogSnapshotSource(asset::IAssetCatalogSnapshotSource *source)
    {
        // Same barrier as the actor services, and for the same reason: the browser reads
        // this once while the workspace is being built, so a late change could only mean
        // some panels were built against a different source than others.
        if (workspace_promoted_)
        {
            throw std::runtime_error(
                "the asset catalog source cannot change after workspace promotion");
        }
        init_info_.asset_catalog_source = source;
    }

    void EditorUI::SetActiveLevelAsset(asset::AssetID level_asset) noexcept
    {
        active_level_asset_pack_ = level_asset.IsValid()
                                        ? std::optional<std::uint64_t>{level_asset.Pack()}
                                        : std::nullopt;
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
        FailQueuedPanelCommands("Editor UI is closed");
        panel_command_registrations_.clear();
        components_.clear();
        asset_reference_panel_.reset();
        settings_window_.reset();
        settings_model_.reset();
        asset_reference_visibility_.SetOpen(false);
        asset_reference_model_.Reset();
        active_level_asset_pack_.reset();
        loading_components_.clear();
        actor_model_.reset();
        // The panel rects described a tree that no longer exists.
        layout_.ResetToDefault();
        workspace_promoted_ = false;
        closing_ = false;
        init_info_ = {};
        log_colors_ = {};
        last_render_time_ms_ = 0.0;
        last_imgui_build_time_ms_ = 0.0;
        last_imgui_submit_time_ms_ = 0.0;
        viewer_mode_ = false;
        viewer_content_ = {};
        ui_viewer_content_ = {};
        screenshot_service_.reset();
        presentation_host_.Close();
    }

    void EditorUI::BeginDraw()
    {
        presentation_host_.BeginFrame(ui_observation_enabled_);
    }

    void EditorUI::EndDraw()
    {
        presentation_host_.EndFrame();
    }

    void EditorUI::SetUiObservationEnabled(const bool enabled) noexcept
    {
        ui_observation_enabled_ = enabled;
        if (!enabled)
        {
            presentation_host_.ClearLastObservation();
        }
    }

    void EditorUI::RegisterRenderTargetImage(
        const ui::UiImageId id, const graphics::RenderTargetView &view)
    {
        presentation_host_.RegisterRenderTargetImage(id, view);
    }

    bool EditorUI::Render()
    {
        const bool rendered = RenderActiveTree();
        presentation_host_.ClearFrameImages();
        return rendered;
    }

    void EditorUI::BuildLoadingTree()
    {
        loading_components_.clear();
        loading_components_.push_back(std::make_unique<EditorLoadingComponent>(
            init_info_.startup_snapshot_source, &glow_enabled_));
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
            auto *const participant = dynamic_cast<EditorLayoutParticipant *>(component.get());
            if (participant == nullptr)
            {
                continue;
            }
            const std::optional<EditorLayoutSlot> slot = participant->GetLayoutSlot();
            if (!slot.has_value())
            {
                continue;  // places itself: the menu bar, and anything not yet slotted
            }
            // An empty rect for a hidden region would collapse the panel, so a slot the
            // layout could not resolve pushes nullopt and the panel keeps its own
            // geometry rather than vanishing.
            if (layout_.HasSlot(*slot))
            {
                participant->ApplyLayout(layout_.RectOf(*slot));
            }
            else
            {
                participant->ApplyLayout(std::nullopt);
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

    bool EditorUI::SaveLayoutState()
    {
        try
        {
            EditorLayoutState state = CaptureLayoutState(layout_);
            CapturePlacementState(tool_row_model_, state);
            WriteEditorLayoutState(GetEditorLayoutPath(), state);
            saved_placement_revision_ = tool_row_model_.GetPlacementRevision();
            layout_save_pending_ = false;
            return true;
        }
        catch (const std::exception &e)
        {
            // A layout preference that cannot be saved must not take the editor down.
            KP_LOG("LogEditorUI", LOG_LEVEL_WARNING, "editor layout not saved (%s)", e.what());
            layout_save_pending_ = true;
            return false;
        }
    }

    bool EditorUI::RenderActiveTree()
    {
        if (!presentation_host_.IsInitialized())
        {
            return false;
        }
        DrainPanelCommands();
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
        if (viewer_mode_ && !closing_ && ui_viewer_content_)
        {
            ui_viewer_content_(presentation_host_.Context());
        }
        else if (viewer_mode_ && !closing_ && viewer_content_)
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
                if (auto *const content = dynamic_cast<EditorPanelContent *>(component.get()))
                {
                    auto *const window = dynamic_cast<EditorWindowComponent *>(component.get());
                    content->Update(window == nullptr || window->IsVisible());
                }
                component->RenderWithContext(presentation_host_.Context());
            }
            // Render the floating viewer before the splitter pass so the splitter can
            // exclude its actual ImGui bounds from the foreground seam.
            if (asset_reference_panel_ != nullptr)
            {
                asset_reference_panel_->Update(asset_reference_panel_->IsVisible());
                asset_reference_panel_->RenderWithContext(presentation_host_.Context());
            }
            std::vector<EditorRect> overlay_occlusions;
            if (asset_reference_panel_ != nullptr && asset_reference_panel_->IsVisible())
                overlay_occlusions.push_back(asset_reference_panel_->LastScreenRect());
            if (settings_window_ != nullptr)
            {
                settings_window_->Update(settings_window_->IsVisible());
                settings_window_->RenderWithContext(presentation_host_.Context());
                if (settings_window_->IsVisible())
                {
                    const auto bounds = presentation_host_.Context().WindowBounds("settings.window");
                    if (bounds)
                        overlay_occlusions.push_back({bounds->min.x, bounds->min.y,
                            bounds->max.x - bounds->min.x, bounds->max.y - bounds->min.y});
                }
            }
            for (const auto &component : components_)
            {
                if (auto *const row = dynamic_cast<EditorToolRowComponent *>(component.get()))
                    row->RenderOverlay(overlay_occlusions);
            }
            splitter_handles_.Render(layout_, overlay_occlusions);
            // Persist on release, not per frame. The placement latch lives in the model,
            // which outlives the row, so nothing here holds a component pointer.
            if (splitter_handles_.ConsumeDragJustEnded() ||
                tool_row_model_.GetPlacementRevision() != saved_placement_revision_)
                layout_save_pending_ = true;
            if (layout_save_pending_)
            {
                (void)SaveLayoutState();
            }
        }
        else
        {
            for (const auto &component : loading_components_)
            {
                component->RenderWithContext(presentation_host_.Context());
            }
        }
        const auto submit_started = std::chrono::steady_clock::now();
        const auto [host_build_ms, host_submit_ms] = presentation_host_.SubmitFrame(
            [this](IEditorImguiRenderer &renderer)
            {
                if (init_info_.render_system == nullptr) return;
                const auto prepared = init_info_.render_system->GetEditorPresentationAssets();
                const auto program = prepared != nullptr
                                         ? prepared->GetEditorUiBloomProgram() : nullptr;
                const auto vertex = program != nullptr
                    ? prepared->Get<asset::ShaderResource>(program->GetData(
                          ShaderStage::SHADER_STAGE_VERTEX, ShaderFormat::SHADER_FORMAT_GLSL))
                    : nullptr;
                const auto fragment = program != nullptr
                    ? prepared->Get<asset::ShaderResource>(program->GetData(
                          ShaderStage::SHADER_STAGE_FRAGMENT, ShaderFormat::SHADER_FORMAT_GLSL))
                    : nullptr;
                renderer.SetBloomShaders(vertex != nullptr ? vertex->data.get() : nullptr,
                                         fragment != nullptr ? fragment->data.get() : nullptr);
            });
        const auto imgui_submit_finished = std::chrono::steady_clock::now();
        EndDraw();
        last_imgui_build_time_ms_ =
            std::chrono::duration<double, std::milli>(submit_started - render_started).count() +
            host_build_ms;
        last_imgui_submit_time_ms_ = host_submit_ms;
        last_render_time_ms_ =
            std::chrono::duration<double, std::milli>(imgui_submit_finished - render_started)
                .count();
        return true;
    }

    EditorUI::~EditorUI() = default;

}
