#include "engine.h"
#include <cassert>
#include <exception>
#include <stdexcept>
#include <utility>
#include <vector>
#include "runtime_global_context.h"
#include "window/window_system.h"
#include "log/log_system.h"
#include "log/logger.h"
#include "config/path.h"
#include "bootstrap/bootstrap.h"
#include "asset/asset_manager.h"
#include "asset/utility.h"
#include "render/render_system.h"
#include "gameplay/world/gameplay_world.h"
#include "editor/editor.h"

// [reconstruction] The legacy render/world systems are still being reconstructed;
// the editor is back (minimal — a single ImGui window). The render module remains
// the primary reconstruction target — docs/render/overview.md.
// #include "render/render_system.h"
// #include "game_framework/world_system.h"

namespace kpengine
{
    namespace runtime
    {

        RenderFrameBeginDisposition ClassifyRenderFrameBegin(
            bool began, render::RenderSystemLifecycleState state)
        {
            if (began && state == render::RenderSystemLifecycleState::FrameActive)
            {
                return RenderFrameBeginDisposition::Record;
            }
            if (!began && state == render::RenderSystemLifecycleState::Ready)
            {
                return RenderFrameBeginDisposition::SkipRecoverable;
            }
            return RenderFrameBeginDisposition::Fatal;
        }

        template <typename Callback>
        class ScopeGuard final
        {
        public:
            explicit ScopeGuard(Callback callback) : callback_(std::move(callback)) {}
            ~ScopeGuard() noexcept
            {
                if (active_)
                {
                    callback_();
                }
            }

            void Dismiss() noexcept { active_ = false; }

        private:
            Callback callback_;
            bool active_ = true;
        };

        constexpr float fps_alpha = 0.1f;

        Engine::Engine() : editor_(std::make_unique<editor::Editor>())
        {
        }

        Engine::~Engine()
        {
            if (render_thread_.joinable())
            {
                AbortStartupTransaction();
            }
            if (command_transport_)
            {
                command_transport_->Stop();
                command_transport_.reset();
            }
            editor_.reset();
        }

        void Engine::SetCommandTransportConfig(command::LocalCommandTransportConfig config)
        {
            command_transport_config_ = std::move(config);
        }

        void Engine::SetGraphicsAPI(GraphicsAPIType api_type)
        {
            if (api_type != GraphicsAPIType::GRAPHICS_API_UNKNOW)
            {
                global_runtime_context.graphics_api_type_ = api_type;
            }
        }

        void Engine::SetStartupLevelOverride(std::string authored_or_normalized_path)
        {
            if (initialization_started_ || startup_level_loaded_ || render_thread_.joinable() ||
                cleared_)
            {
                throw std::runtime_error(
                    "startup level override must be set before Engine::Initialize");
            }

            std::string normalized_path;
            if (!asset::NormalizeAssetRootRelativePath(
                    authored_or_normalized_path, asset::AssetType::KPAT_Level, normalized_path) ||
                normalized_path == "level" || normalized_path.rfind("level/", 0) != 0)
            {
                throw std::runtime_error(
                    "startup level override must be an Asset-root-relative level/*.level path");
            }
            startup_level_override_ = std::move(normalized_path);
        }

        StartupSnapshot Engine::GetStartupSnapshot() const
        {
            return startup_coordinator_.GetSnapshot();
        }

        void Engine::Initialize()
        {
            if (cleared_)
            {
                throw std::runtime_error(
                    "Engine cannot be initialized after Clear(); create a new Engine");
            }
            if (render_thread_.joinable())
            {
                throw std::runtime_error("Engine is already initialized");
            }
            KP_LOG("EngineLog", LOG_LEVEL_INFO, "Engine initializing...");
            initialization_started_ = true;
            shutdown_requested_.store(false);
            startup_coordinator_.Begin();
            startup_coordinator_.SetPhase(StartupPhase::PresentationStarting,
                                          "Starting presentation");
            auto startup_guard = ScopeGuard{[this]() noexcept { AbortStartupTransaction(); }};

            // Editor setup (pointers into the runtime context, no GPU state) is safe
            // on the main thread; its ImGui UI is built on the render thread by
            // InitEditorUI, where the GL/Vulkan context exists.
            try
            {
                editor_->Initialize(this);
                editor_attached_ = true;
                startup_asset_session_ = asset::AssetManager::GetInstance().BeginLoadObservation();
            }
            catch (const std::exception &error)
            {
                startup_coordinator_.Fail(error.what());
                throw;
            }
            catch (...)
            {
                startup_coordinator_.Fail("Unknown exception attaching startup services");
                throw;
            }
            startup_coordinator_.SetAssetSession(*startup_asset_session_);

            // [thread model] Main OS thread = game thread. The render thread is spawned
            // below; it creates the window/context itself (RenderThreadFunc) so the GPU
            // context lives on the render thread.
            global_runtime_context.game_thread_id_ = std::this_thread::get_id();
            {
                std::lock_guard<std::mutex> lock(render_start_mutex_);
                is_render_thread_loaded_ = false;
                render_start_succeeded_ = false;
                render_start_diagnostic_.clear();
            }
            {
                std::lock_guard<std::mutex> lock(startup_decision_mutex_);
                startup_decision_ = StartupDecision::Pending;
            }
            {
                std::lock_guard<std::mutex> lock(render_promotion_mutex_);
                render_promotion_finished_ = false;
                render_promotion_succeeded_ = false;
                render_promotion_diagnostic_.clear();
            }
            {
                std::lock_guard<std::mutex> lock(editor_promotion_mutex_);
                editor_promotion_finished_ = false;
                editor_promotion_succeeded_ = false;
                editor_promotion_diagnostic_.clear();
            }
            {
                startup_access_barrier_.Begin();
            }
            render_thread_ = std::thread(&Engine::RenderThreadFunc, this);

            // Wait for the render thread to finish window/context setup before
            // returning, so the game thread can start producing frames safely.
            {
                std::unique_lock<std::mutex> lock(render_start_mutex_);
                render_start_cv_.wait(lock, [this]
                                      { return is_render_thread_loaded_; });
            }

            bool render_start_succeeded = false;
            std::string render_start_diagnostic;
            {
                std::lock_guard<std::mutex> lock(render_start_mutex_);
                render_start_succeeded = render_start_succeeded_;
                render_start_diagnostic = render_start_diagnostic_;
            }
            if (!render_start_succeeded)
            {
                if (render_thread_.joinable())
                {
                    render_thread_.join();
                }
                throw std::runtime_error(
                    "RenderSystem startup failed: " + render_start_diagnostic);
            }

            startup_coordinator_.SetPhase(StartupPhase::LoadingAssets,
                                          "Loading startup assets");
            try
            {
                LoadStartupLevel(*startup_asset_session_);
            }
            catch (const std::exception &error)
            {
                startup_coordinator_.Fail(error.what());
                throw;
            }
            catch (...)
            {
                startup_coordinator_.Fail("Unknown exception loading startup assets");
                throw;
            }
            startup_asset_session_->Seal();
            if (shutdown_requested_.load(std::memory_order_acquire))
            {
                throw std::runtime_error("Startup cancelled while loading assets");
            }
            startup_coordinator_.SetPhase(StartupPhase::PreparingCpuArtifacts,
                                          "Preparing CPU render artifacts");
            RuntimeContext::StartupResult render_asset_result;
            try
            {
                render_asset_result = global_runtime_context.PrepareRenderAssets();
            }
            catch (const std::exception &error)
            {
                startup_coordinator_.Fail(error.what());
                throw;
            }
            catch (...)
            {
                startup_coordinator_.Fail("Unknown exception preparing CPU render artifacts");
                throw;
            }
            if (!render_asset_result)
            {
                startup_coordinator_.Fail(render_asset_result.diagnostic);
                throw std::runtime_error("Render asset preparation failed: " +
                                         render_asset_result.diagnostic);
            }
            if (shutdown_requested_.load(std::memory_order_acquire))
            {
                throw std::runtime_error("Startup cancelled while preparing CPU artifacts");
            }

            startup_coordinator_.SetPhase(StartupPhase::PromotingSceneRenderer,
                                          "Promoting scene renderer");
            PublishStartupDecision(StartupDecision::Promote);
            {
                std::unique_lock<std::mutex> lock(render_promotion_mutex_);
                render_promotion_cv_.wait(lock,
                                          [this]
                                          {
                                              return render_promotion_finished_ ||
                                                     shutdown_requested_.load(std::memory_order_acquire);
                                          });
                if (!render_promotion_succeeded_)
                {
                    startup_coordinator_.Fail(render_promotion_diagnostic_);
                    throw std::runtime_error("Render scene promotion failed: " +
                                             render_promotion_diagnostic_);
                }
                if (shutdown_requested_.load(std::memory_order_acquire))
                {
                    throw std::runtime_error("Startup cancelled during scene promotion");
                }
            }

            startup_coordinator_.SetPhase(StartupPhase::InstantiatingLevel,
                                          "Instantiating startup level");
            const RuntimeContext::StartupResult startup_result =
                global_runtime_context.FinalizeGameStartup();
            if (!startup_result)
            {
                startup_coordinator_.Fail(startup_result.diagnostic);
                PublishStartupDecision(StartupDecision::Abort);
                throw std::runtime_error("Runtime startup failed: " + startup_result.diagnostic);
            }

            startup_coordinator_.SetPhase(StartupPhase::ActivatingEditorWorkspace,
                                          "Activating editor workspace");
            editor_->ActivateWorkspace();

            // Complete all game-thread work that may throw before publishing
            // Commit. Once Commit is visible, the render thread may begin
            // teardown independently, so no further RuntimeContext access is
            // allowed on this thread during Initialize().
            if (command_transport_config_.enabled)
            {
                command::CommandRegistry *registry = global_runtime_context.GetCommandRegistry();
                if (registry == nullptr)
                {
                    KP_LOG("EngineLog", LOG_LEVEL_ERROR,
                           "Local command transport was requested, but Runtime has no command registry");
                }
                else
                {
                    command_transport_ = std::make_unique<command::CommandLocalTransport>(
                        *registry, command_transport_config_);
                    std::string diagnostic;
                    if (!command_transport_->Start(diagnostic))
                    {
                        KP_LOG("EngineLog", LOG_LEVEL_ERROR,
                               "Local command transport did not start: %s", diagnostic.c_str());
                        command_transport_.reset();
                    }
                    else
                    {
                        KP_LOG("EngineLog", LOG_LEVEL_INFO,
                               "Local command transport listening on 127.0.0.1:%u",
                               command_transport_->BoundPort());
                    }
                }
            }

            PublishStartupDecision(StartupDecision::Commit);
            {
                std::unique_lock<std::mutex> lock(editor_promotion_mutex_);
                editor_promotion_cv_.wait(lock,
                                          [this]
                                          {
                                              return editor_promotion_finished_ ||
                                                     shutdown_requested_.load(std::memory_order_acquire);
                                          });
                if (!editor_promotion_succeeded_)
                {
                    startup_coordinator_.Fail(editor_promotion_diagnostic_);
                    throw std::runtime_error("Editor workspace promotion failed: " +
                                             editor_promotion_diagnostic_);
                }
            }
            if (shutdown_requested_.load(std::memory_order_acquire))
            {
                startup_coordinator_.Cancel("Startup cancelled during editor promotion");
                throw std::runtime_error("Startup cancelled during editor promotion");
            }
            startup_coordinator_.SetReady();
            EndStartupAccess();
            KP_LOG("EngineLog", LOG_LEVEL_INFO, "Engine initialize successfully");
            startup_guard.Dismiss();
        }

        void Engine::PublishStartupDecision(const StartupDecision decision) noexcept
        {
            {
                std::lock_guard<std::mutex> lock(startup_decision_mutex_);
                if (decision == StartupDecision::Abort ||
                    startup_decision_ == StartupDecision::Pending ||
                    (startup_decision_ == StartupDecision::Promote &&
                     decision == StartupDecision::Commit))
                {
                    startup_decision_ = decision;
                }
            }
            startup_decision_cv_.notify_all();
        }

        void Engine::AbortStartupTransaction() noexcept
        {
            SealStartupObservation();
            EndStartupAccess();
            shutdown_requested_.store(true, std::memory_order_release);
            {
                std::lock_guard<std::mutex> lock(game_ready_mutex_);
                is_game_thread_loaded_ = true;
            }
            game_ready_cv_.notify_all();
            PublishStartupDecision(StartupDecision::Abort);
            try
            {
                startup_coordinator_.Cancel("Startup aborted");
            }
            catch (...)
            {
            }
            startup_coordinator_.Rollback();
            if (render_thread_.joinable())
            {
                render_thread_.join();
            }
            if (editor_attached_ && editor_)
            {
                try
                {
                    editor_->Clear();
                }
                catch (...)
                {
                }
                editor_attached_ = false;
            }
        }

        void Engine::EndStartupAccess() noexcept
        {
            startup_access_barrier_.End();
        }

        void Engine::WaitForStartupAccessToEnd() noexcept
        {
            startup_access_barrier_.WaitForEnd();
        }

        void Engine::SealStartupObservation() noexcept
        {
            if (startup_asset_session_)
            {
                startup_asset_session_->Seal();
            }
        }

        void Engine::LoadStartupLevel(const asset::AssetLoadSession &session)
        {
            if (startup_level_loaded_)
            {
                return;
            }

            const bootstrap::BootstrapConfig config = bootstrap::ReadBootstrap(GetBootstrapPath());
            const bool has_override = startup_level_override_.has_value();
            const std::string &selected_level =
                has_override ? *startup_level_override_ : config.startup_level;
            const char *selection_source = has_override ? "CLI --startup-level" : "Bootstrap";
            asset::AssetID level_asset{};
            try
            {
                level_asset = asset::AssetManager::GetInstance().LoadSync(
                    GetAssetDirectory() + selected_level, session);
            }
            catch (const std::exception &error)
            {
                throw std::runtime_error(std::string{selection_source} +
                                         " startup level failed to load '" + selected_level +
                                         "': " + error.what());
            }
            if (!level_asset.IsValid() || level_asset.type != asset::AssetType::KPAT_Level)
            {
                throw std::runtime_error(std::string{selection_source} +
                                         " startup level could not be loaded: " + selected_level);
            }
            global_runtime_context.SetStartupLevel(level_asset);
            KP_LOG("EngineLog", LOG_LEVEL_INFO,
                   "Startup level loaded from %s: %s", selection_source, selected_level.c_str());
            startup_level_loaded_ = true;
        }

        void Engine::Clear()
        {
            if (render_thread_.joinable())
            {
                AbortStartupTransaction();
            }
            if (command_transport_)
            {
                command_transport_->Stop();
                command_transport_.reset();
            }
            // Runs after the render thread joined, so the editor's ImGui state was
            // already shut down on that thread (CloseUI); this only clears the
            // editor-side context.
            editor_->Clear();
            editor_attached_ = false;

            // RuntimeContext::Clear() is terminal: it releases the global
            // composition root rather than leaving a reconstructible shell.
            startup_level_loaded_ = false;
            cleared_ = true;
        }

        void Engine::Run()
        {
            // [thread model] This runs on the main OS thread — the game thread.
            while (!shutdown_requested_.load())
            {
                GameTick();
            }

            if (command_transport_)
            {
                command_transport_->Stop();
                command_transport_.reset();
            }

            // Wake the render thread out of its frame wait so it can observe
            // ShouldClose() and exit; otherwise join() below would hang on the CV wait.
            {
                std::lock_guard<std::mutex> lock(game_ready_mutex_);
                is_game_thread_loaded_ = true;
            }
            game_ready_cv_.notify_all();

            if (render_thread_.joinable())
            {
                render_thread_.join();
            }
        }

        void Engine::GameTick()
        {
            using clock = std::chrono::steady_clock;
            const double target_frame_time = 1.0 / target_fps;
            auto frame_start = clock::now();

            if (command_transport_)
            {
                command_transport_->PumpGameThread();
            }

            if (global_runtime_context.command_registry_)
            {
                global_runtime_context.command_registry_->PumpGameThread();
            }

            if (global_runtime_context.gameplay_world_)
            {
                global_runtime_context.TickGameplay(1.0f / target_fps);
            }

            {
                std::lock_guard<std::mutex> lock(game_ready_mutex_);
                is_game_thread_loaded_ = true;
            }
            game_ready_cv_.notify_one();

            auto frame_end = clock::now();
            std::chrono::duration<double> elapsed = frame_end - frame_start;
            double sleep_seconds = target_frame_time - elapsed.count();

            if (sleep_seconds > 0.0)
            {
                std::this_thread::sleep_for(std::chrono::duration<double>(sleep_seconds));
            }
        }

        void Engine::RenderThreadFunc()
        {
            // [thread model] The render thread owns the window + GPU context: it creates
            // them here (context.Initialize builds the window on this thread), then
            // presents every frame.
            bool render_start_signaled = false;
            bool presentation_ready_signaled = false;
            bool editor_ui_initialized = false;
            bool startup_committed = false;
            bool context_cleared = false;
            const auto clear_context = [this, &context_cleared]() noexcept
            {
                if (context_cleared)
                {
                    return;
                }
                context_cleared = true;
                try
                {
                    global_runtime_context.Clear();
                }
                catch (...)
                {
                    // A thread entry point must never let cleanup escape into
                    // std::thread; the owning Engine still joins this thread.
                }
            };
            const auto signal_render_start =
                [this, &render_start_signaled](bool succeeded, const char *diagnostic) noexcept
            {
                if (render_start_signaled)
                {
                    return;
                }
                {
                    std::lock_guard<std::mutex> lock(render_start_mutex_);
                    if (is_render_thread_loaded_)
                    {
                        render_start_signaled = true;
                        return;
                    }
                    render_start_diagnostic_ = diagnostic != nullptr ? diagnostic : "";
                    render_start_succeeded_ = succeeded;
                    is_render_thread_loaded_ = true;
                    render_start_signaled = true;
                }
                render_start_cv_.notify_all();
            };

            try
            {
                global_runtime_context.InitializePresentation();
                global_runtime_context.render_thread_id_ = std::this_thread::get_id();
            }
            catch (const std::exception &error)
            {
                signal_render_start(false, error.what());
                startup_coordinator_.Fail(error.what());
                PublishStartupDecision(StartupDecision::Abort);
                // Main is only waiting for the presentation result here; it
                // has not entered a RuntimeContext startup stage yet.
                EndStartupAccess();
                shutdown_requested_.store(true, std::memory_order_release);
                clear_context();
                return;
            }
            catch (...)
            {
                signal_render_start(false, "unknown RenderSystem startup exception");
                startup_coordinator_.Fail("unknown RenderSystem startup exception");
                PublishStartupDecision(StartupDecision::Abort);
                EndStartupAccess();
                shutdown_requested_.store(true, std::memory_order_release);
                clear_context();
                return;
            }

            try
            {
                // ImGui must be built and used on the thread that owns the GL/Vulkan
                // context — that is this thread, so the editor UI lives here.
                editor_->InitEditorUI();
                editor_ui_initialized = true;
                startup_coordinator_.SetPhase(StartupPhase::PresentationReady,
                                              "Presentation ready");
                signal_render_start(true, "");
                presentation_ready_signaled = true;

                while (!shutdown_requested_.load(std::memory_order_acquire))
                {
                    StartupDecision decision = StartupDecision::Pending;
                    {
                        std::lock_guard<std::mutex> lock(startup_decision_mutex_);
                        decision = startup_decision_;
                    }
                    if (decision == StartupDecision::Abort)
                    {
                        break;
                    }

                    if (decision == StartupDecision::Promote)
                    {
                        bool already_finished = false;
                        {
                            std::lock_guard<std::mutex> lock(render_promotion_mutex_);
                            already_finished = render_promotion_finished_;
                        }
                        if (!already_finished)
                        {
                            const RuntimeContext::StartupResult promotion =
                                global_runtime_context.PromoteRenderAssets();
                            {
                                std::lock_guard<std::mutex> lock(render_promotion_mutex_);
                                render_promotion_succeeded_ = promotion.success;
                                render_promotion_diagnostic_ = promotion.diagnostic;
                                render_promotion_finished_ = true;
                            }
                            render_promotion_cv_.notify_all();
                            if (!promotion)
                            {
                                startup_coordinator_.Fail(promotion.diagnostic);
                                PublishStartupDecision(StartupDecision::Abort);
                                shutdown_requested_.store(true, std::memory_order_release);
                                break;
                            }
                        }
                    }

                    if (decision == StartupDecision::Commit)
                    {
                        bool already_finished = false;
                        {
                            std::lock_guard<std::mutex> lock(editor_promotion_mutex_);
                            already_finished = editor_promotion_finished_;
                        }
                        if (!already_finished)
                        {
                            editor_->PromoteEditorWorkspace();
                            {
                                std::lock_guard<std::mutex> lock(editor_promotion_mutex_);
                                editor_promotion_succeeded_ = true;
                                editor_promotion_finished_ = true;
                            }
                            editor_promotion_cv_.notify_all();
                            startup_committed = true;
                        }
                        RenderTick();
                    }
                    else
                    {
                        RenderLoadingTick();
                    }
                    // GLFW event handling and its close flag are render-thread-owned.
                    // Publish close to the game thread instead of reading GLFW there.
                    if (global_runtime_context.window_system_->ShouldClose())
                    {
                        if (!startup_committed)
                        {
                            startup_coordinator_.Cancel("Window closed during startup");
                        }
                        {
                            std::lock_guard<std::mutex> lock(render_promotion_mutex_);
                            if (!render_promotion_finished_)
                            {
                                render_promotion_succeeded_ = false;
                                render_promotion_diagnostic_ = "Window closed during startup";
                                render_promotion_finished_ = true;
                            }
                        }
                        render_promotion_cv_.notify_all();
                        {
                            std::lock_guard<std::mutex> lock(editor_promotion_mutex_);
                            if (!editor_promotion_finished_)
                            {
                                editor_promotion_succeeded_ = false;
                                editor_promotion_diagnostic_ = "Window closed during startup";
                                editor_promotion_finished_ = true;
                            }
                        }
                        editor_promotion_cv_.notify_all();
                        shutdown_requested_.store(true, std::memory_order_release);
                        PublishStartupDecision(StartupDecision::Abort);
                    }
                }

                // Shut ImGui down on the same thread that built it, before the window
                // teardown at exit.
                WaitForStartupAccessToEnd();
                editor_->CloseUI();
                editor_ui_initialized = false;
                clear_context();
            }
            catch (const std::exception &error)
            {
                signal_render_start(false, error.what());
                {
                    std::lock_guard<std::mutex> lock(render_promotion_mutex_);
                    render_promotion_diagnostic_ = error.what();
                    render_promotion_finished_ = true;
                }
                render_promotion_cv_.notify_all();
                {
                    std::lock_guard<std::mutex> lock(editor_promotion_mutex_);
                    editor_promotion_succeeded_ = false;
                    editor_promotion_diagnostic_ = error.what();
                    editor_promotion_finished_ = true;
                }
                editor_promotion_cv_.notify_all();
                shutdown_requested_.store(true, std::memory_order_release);
                PublishStartupDecision(StartupDecision::Abort);
                if (presentation_ready_signaled)
                {
                    WaitForStartupAccessToEnd();
                }
                else
                {
                    EndStartupAccess();
                }
                if (editor_ui_initialized)
                {
                    try
                    {
                        editor_->CloseUI();
                    }
                    catch (...)
                    {
                    }
                }
                clear_context();
                KP_LOG("EngineLog", LOG_LEVEL_ERROR,
                       "Render thread failed after startup: %s", error.what());
            }
            catch (...)
            {
                signal_render_start(false, "unknown render startup exception");
                {
                    std::lock_guard<std::mutex> lock(render_promotion_mutex_);
                    render_promotion_diagnostic_ = "unknown render startup exception";
                    render_promotion_finished_ = true;
                }
                render_promotion_cv_.notify_all();
                {
                    std::lock_guard<std::mutex> lock(editor_promotion_mutex_);
                    editor_promotion_succeeded_ = false;
                    editor_promotion_diagnostic_ = "unknown render startup exception";
                    editor_promotion_finished_ = true;
                }
                editor_promotion_cv_.notify_all();
                shutdown_requested_.store(true, std::memory_order_release);
                PublishStartupDecision(StartupDecision::Abort);
                if (presentation_ready_signaled)
                {
                    WaitForStartupAccessToEnd();
                }
                else
                {
                    EndStartupAccess();
                }
                if (editor_ui_initialized)
                {
                    try
                    {
                        editor_->CloseUI();
                    }
                    catch (...)
                    {
                    }
                }
                clear_context();
                KP_LOG("EngineLog", LOG_LEVEL_ERROR,
                       "Render thread failed after startup: unknown exception");
            }
        }

        void Engine::RenderTick()
        {
            // Consume the game thread's produced frame. The game thread paces at
            // target_fps, so this blocking wait wakes ~once per frame.
            {
                std::unique_lock<std::mutex> lock(game_ready_mutex_);
                game_ready_cv_.wait(lock, [this]
                                    { return is_game_thread_loaded_; });
                is_game_thread_loaded_ = false;
            }

            float delta_time = CalculateDeltaTime();

            using clock = std::chrono::steady_clock;
            const double target_frame_time = 1.0 / target_fps;
            auto frame_start = clock::now();

            float delta = 1.f / target_fps;
            global_runtime_context.log_system_->Tick(delta);
            const bool frame_began = global_runtime_context.render_system_->BeginFrame(delta);
            switch (ClassifyRenderFrameBegin(
                frame_began, global_runtime_context.render_system_->GetLifecycleState()))
            {
            case RenderFrameBeginDisposition::Record:
                break;
            case RenderFrameBeginDisposition::SkipRecoverable:
                // A backend may legitimately skip a frame while rebuilding its
                // swapchain. Do not construct ImGui, record a terminal pass, or
                // present an unrelated buffer without an active Render bracket.
                global_runtime_context.window_system_->PollEvents();
                return;
            case RenderFrameBeginDisposition::Fatal:
                throw std::runtime_error(
                    "Render frame begin failed: " +
                    global_runtime_context.render_system_->GetLastDiagnostic());
            }

            // Polling must precede ImGui frame construction. RenderSystem owns the
            // terminal pass position; this callback supplies the editor's external
            // ImGui recording without creating a Render -> Editor dependency.
            global_runtime_context.window_system_->PollEvents();
            if (!global_runtime_context.render_system_->ExecuteEditorCompositePass(
                    [this] { editor_->Tick(); }))
            {
                editor_->Tick();
            }
            global_runtime_context.render_system_->EndFrame();
            if(global_runtime_context.graphics_api_type_ == GraphicsAPIType::GRAPHICS_API_OPENGL)
            {
                global_runtime_context.window_system_->SwapBuffers();
            }

            auto frame_end = clock::now();
            std::chrono::duration<double> elapsed = frame_end - frame_start;
            double sleep_seconds = target_frame_time - elapsed.count();

            if (sleep_seconds > 0.0)
            {
                std::this_thread::sleep_for(std::chrono::duration<double>(sleep_seconds));
            }
        }

        void Engine::RenderLoadingTick()
        {
            const float delta = 1.0f / static_cast<float>(target_fps);
            const bool frame_began = global_runtime_context.render_system_->BeginFrame(delta);
            if (!frame_began)
            {
                global_runtime_context.window_system_->PollEvents();
                return;
            }
            global_runtime_context.window_system_->PollEvents();
            if (!global_runtime_context.render_system_->ExecuteEditorCompositePass(
                    [this] { editor_->TickPresentation(); }))
            {
                editor_->TickPresentation();
            }
            if (!global_runtime_context.render_system_->EndFrame())
            {
                throw std::runtime_error("Loading frame could not be closed");
            }
            if (global_runtime_context.graphics_api_type_ == GraphicsAPIType::GRAPHICS_API_OPENGL)
            {
                global_runtime_context.window_system_->SwapBuffers();
            }
        }

        void Engine::CalculateFPS(float delta_time)
        {
            frame_count++;
            if (frame_count == 1)
            {
                avg_time_cost = delta_time;
            }
            else
            {
                avg_time_cost = (1.f - fps_alpha) * avg_time_cost + fps_alpha * delta_time;
            }
            measured_fps = static_cast<int>(1.f / avg_time_cost);
        }

        float Engine::CalculateDeltaTime()
        {
            std::chrono::steady_clock::time_point current_time{std::chrono::steady_clock::now()};

            std::chrono::duration<float> duration = std::chrono::duration_cast<std::chrono::duration<float>>(current_time - last_time);

            last_time = current_time;
            float delta_time = duration.count();
            CalculateFPS(delta_time);

            return delta_time;
        }

    }
}
