#ifndef KPENGINE_RUNTIME_ENGINE_H
#define KPENGINE_RUNTIME_ENGINE_H

#include <chrono>
#include <thread>
#include <condition_variable>
#include <mutex>
#include <memory>
#include <atomic>

#include "base/type.h"
#include "command/command_local_transport.h"

namespace kpengine
{
    namespace editor
    {
        class Editor;
    }

    namespace runtime
    {
        class Engine
        {
        public:
            Engine();
            ~Engine();

            void Initialize();
            void Clear();
            void Run();
            void SetCommandTransportConfig(command::LocalCommandTransportConfig config);
            void SetGraphicsAPI(GraphicsAPIType api_type);

            inline int GetFPS() const { return measured_fps; }
            const int *GetFPSRef() const { return &measured_fps; }

        private:
            // [thread model] Unreal-style split: the game thread runs on the main OS
            // thread (Run/GameTick), the render thread is spawned (RenderThreadFunc/
            // RenderTick) and owns the window + GPU context — it creates them in
            // RenderThreadFunc, then presents every frame. The game thread produces a
            // frame (flips is_game_thread_loaded_ + notifies), the render thread waits
            // on it, consumes it, and resets the flag.
            void GameTick();
            void RenderThreadFunc();
            void RenderTick();
            float CalculateDeltaTime();
            void CalculateFPS(float delta_time);

            // The bootstrap preload is a startup one-shot (docs/status.md item 6):
            // read the need-list once and enqueue it on the async queue's incoming
            // leg, before the main loop. Guarded by bootstrap_loaded_ so a second
            // Initialize() can never enqueue the batch twice.
            void PreloadBootstrap();

        private:
            std::chrono::steady_clock::time_point last_time{std::chrono::steady_clock::now()};
            int frame_count = 0;
            float avg_time_cost = 0.f;
            int target_fps = 120; // fixed update rate
            int measured_fps = 0; // for display

            // game (main) → render (spawned) frame handshake.
            std::condition_variable game_ready_cv_;
            std::mutex game_ready_mutex_;
            bool is_game_thread_loaded_ = false;

            // render-thread startup handshake: Initialize() waits until the render
            // thread has created the window/context so Run() can query it safely.
            std::condition_variable render_start_cv_;
            std::mutex render_start_mutex_;
            bool is_render_thread_loaded_ = false;

            std::thread render_thread_;
            std::atomic_bool shutdown_requested_{false};

            // One-shot guard for PreloadBootstrap(). Set only after a successful
            // enqueue, so a failed read (missing bootstrap.json) can be retried.
            bool bootstrap_loaded_ = false;

            command::LocalCommandTransportConfig command_transport_config_{};
            std::unique_ptr<command::CommandLocalTransport> command_transport_;

            // Engine-owned editor. Its UI (ImGui) is initialized and ticked on the
            // render thread where the GL/Vulkan context lives (see InitEditorUI).
            std::unique_ptr<editor::Editor> editor_;
        };
    }
}

#endif
