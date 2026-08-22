#include "engine.h"
#include <cassert>
#include <utility>
#include <vector>
#include "runtime_global_context.h"
#include "window/window_system.h"
#include "log/log_system.h"
#include "log/logger.h"
#include "config/path.h"
#include "bootstrap/bootstrap.h"
#include "render/render_system.h"
#include "editor/editor.h"

// [reconstruction] The legacy render/world systems are still being reconstructed;
// the editor is back (minimal — a single ImGui window). The render module remains
// the primary reconstruction target — docs/render/render_module.md.
// #include "render/render_system.h"
// #include "game_framework/world_system.h"

namespace kpengine
{
    namespace runtime
    {

        constexpr float fps_alpha = 0.1f;

        Engine::Engine() : editor_(std::make_unique<editor::Editor>())
        {
        }

        Engine::~Engine()
        {
            editor_.reset();
        }

        void Engine::Initialize()
        {
            KP_LOG("EngineLog", LOG_LEVEL_INFO, "Engine initializing...");
            shutdown_requested_.store(false);

            // Startup one-shot: read the need-list and enqueue it once (guarded
            // inside). Missing bootstrap.json is a hard boot error — fail fast so
            // a misconfigured project is obvious.
            PreloadBootstrap();

            // Editor setup (pointers into the runtime context, no GPU state) is safe
            // on the main thread; its ImGui UI is built on the render thread by
            // InitEditorUI, where the GL/Vulkan context exists.
            editor_->Initialize(this);

            // [thread model] Main OS thread = game thread. The render thread is spawned
            // below; it creates the window/context itself (RenderThreadFunc) so the GPU
            // context lives on the render thread.
            global_runtime_context.game_thread_id_ = std::this_thread::get_id();
            render_thread_ = std::thread(&Engine::RenderThreadFunc, this);

            // Wait for the render thread to finish window/context setup before
            // returning, so the game thread can start producing frames safely.
            {
                std::unique_lock<std::mutex> lock(render_start_mutex_);
                render_start_cv_.wait(lock, [this]
                                      { return is_render_thread_loaded_; });
            }

            KP_LOG("EngineLog", LOG_LEVEL_INFO, "Engine initialize successfully");
        }

        void Engine::PreloadBootstrap()
        {
            // "load once": a second Initialize() (or a re-run of this method) is a
            // no-op once the batch is on the queue. bootstrap_loaded_ flips only
            // after a successful enqueue, so a failed read stays retryable.
            if (bootstrap_loaded_)
            {
                return;
            }

            const bootstrap::BootstrapConfig config = bootstrap::ReadBootstrap(GetBootstrapPath());
            render::BootstrapSceneInfo scene_info{};
            if (config.scene.IsComplete())
            {
                scene_info.shader_program_path = GetAssetDirectory() + config.scene.shader_program;
                scene_info.model_path = GetAssetDirectory() + config.scene.model;
                scene_info.texture_path = GetAssetDirectory() + config.scene.texture;
            }
            global_runtime_context.SetBootstrapScene(std::move(scene_info));
            std::vector<asset::AssetLoadRequest> requests = bootstrap::BuildLoadRequests(config);
            for (auto &request : requests)
            {
                global_runtime_context.async_load_queue_.Push(std::move(request));
            }

            KP_LOG("EngineLog", LOG_LEVEL_INFO, "Bootstrap preload: enqueued %zu asset request(s) for async loading",
                   requests.size());
            bootstrap_loaded_ = true;
        }

        void Engine::Clear()
        {
            // Runs after the render thread joined, so the editor's ImGui state was
            // already shut down on that thread (CloseUI); this only clears the
            // editor-side context.
            editor_->Clear();

            // A fresh engine cycle re-preloads: Initialize() after Clear() must not
            // be treated as a no-op bootstrap.
            bootstrap_loaded_ = false;
        }

        void Engine::Run()
        {
            // [thread model] This runs on the main OS thread — the game thread.
            while (!shutdown_requested_.load())
            {
                GameTick();
            }

            // Wake the render thread out of its frame wait so it can observe
            // ShouldClose() and exit; otherwise join() below would hang on the CV wait.
            {
                std::lock_guard<std::mutex> lock(game_ready_mutex_);
                is_game_thread_loaded_ = true;
            }
            game_ready_cv_.notify_all();

            render_thread_.join();
        }

        void Engine::GameTick()
        {
            using clock = std::chrono::steady_clock;
            const double target_frame_time = 1.0 / target_fps;
            auto frame_start = clock::now();

            // [reconstruction] Old design — the legacy game framework was ticked here;
            // re-wired by the reconstruction.
            // global_runtime_context.world_system_->Tick(1.f / target_fps);

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
            global_runtime_context.Initialize();
            global_runtime_context.PostInitialize();
            global_runtime_context.render_thread_id_ = std::this_thread::get_id();

            {
                std::lock_guard<std::mutex> lock(render_start_mutex_);
                is_render_thread_loaded_ = true;
            }
            render_start_cv_.notify_one();

            // ImGui must be built and used on the thread that owns the GL/Vulkan
            // context — that is this thread, so the editor UI lives here.
            editor_->InitEditorUI();

            while (!shutdown_requested_.load())
            {
                RenderTick();
                // GLFW event handling and its close flag are render-thread-owned.
                // Publish close to the game thread instead of reading GLFW there.
                if (global_runtime_context.window_system_->ShouldClose())
                {
                    shutdown_requested_.store(true);
                }
            }

            // Shut ImGui down on the same thread that built it, before the window
            // teardown at exit.
            editor_->CloseUI();
            global_runtime_context.Clear();
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
            global_runtime_context.render_system_->BeginFrame(delta);

            // Editor UI frames between input polling and presentation: PollEvents feeds
            // ImGui, the UI renders into the back buffer, SwapBuffers presents it. The
            // legacy GL scene renderer (ticked here pre-reconstruction) is still being
            // rebuilt — docs/render/render_module.md.
            global_runtime_context.window_system_->PollEvents();
            editor_->Tick();
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
