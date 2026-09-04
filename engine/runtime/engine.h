#ifndef KPENGINE_RUNTIME_ENGINE_H
#define KPENGINE_RUNTIME_ENGINE_H

#include <chrono>
#include <thread>
#include <condition_variable>
#include <mutex>
#include <memory>
#include <atomic>
#include <cstdint>
#include <optional>
#include <string>

#include "base/type.h"
#include "command/command_local_transport.h"
#include "runtime_startup.h"

namespace kpengine
{
    namespace render
    {
        enum class RenderSystemLifecycleState : uint8_t;
    }

    namespace editor
    {
        class Editor;
    }

    namespace runtime
    {
        enum class RenderFrameBeginDisposition : uint8_t
        {
            Record,
            SkipRecoverable,
            Fatal,
        };

        // Runtime's frame-loop policy is kept separate from RenderSystem so the
        // recoverable no-context path is testable without constructing a window.
        RenderFrameBeginDisposition ClassifyRenderFrameBegin(
            bool began, render::RenderSystemLifecycleState state);

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
            void SetStartupLevelOverride(std::string authored_or_normalized_path);
            StartupSnapshot GetStartupSnapshot() const;

            inline int GetFPS() const { return measured_fps; }
            const int *GetFPSRef() const { return &measured_fps; }

        private:
            // [thread model] Unreal-style split: the game thread runs on the main OS
            // thread (Run/GameTick), the render thread is spawned (RenderThreadFunc/
            // RenderTick) and owns the window + GPU context — it creates them in
            // RenderThreadFunc, then presents every frame. The game thread produces a
            // frame (flips is_game_thread_loaded_ + notifies), the render thread waits
            // on it, consumes it, and resets the flag.
            enum class StartupDecision : uint8_t;
            void GameTick();
            void RenderThreadFunc();
            void RenderTick();
            void RenderLoadingTick();
            void PublishStartupDecision(StartupDecision decision) noexcept;
            void AbortStartupTransaction() noexcept;
            void EndStartupAccess() noexcept;
            void WaitForStartupAccessToEnd() noexcept;
            void SealStartupObservation() noexcept;
            float CalculateDeltaTime();
            void CalculateFPS(float delta_time);

            // Load and validate the one startup level before creating the render
            // thread. Its Asset dependency closure is ready before Runtime commits.
            void LoadStartupLevel(const asset::AssetLoadSession &session);

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
            bool render_start_succeeded_ = false;
            std::string render_start_diagnostic_;

            enum class StartupDecision : uint8_t
            {
                Pending,
                Promote,
                Commit,
                Abort,
            };
            std::condition_variable startup_decision_cv_;
            std::mutex startup_decision_mutex_;
            StartupDecision startup_decision_ = StartupDecision::Pending;

            std::condition_variable render_promotion_cv_;
            std::mutex render_promotion_mutex_;
            bool render_promotion_finished_ = false;
            bool render_promotion_succeeded_ = false;
            std::string render_promotion_diagnostic_;

            std::condition_variable editor_promotion_cv_;
            std::mutex editor_promotion_mutex_;
            bool editor_promotion_finished_ = false;
            bool editor_promotion_succeeded_ = false;
            std::string editor_promotion_diagnostic_;

            // The render thread may request cancellation while the game thread
            // is inside synchronous RuntimeContext work. Teardown waits for
            // this lane to leave before destroying the shared RuntimeContext.
            StartupAccessBarrier startup_access_barrier_;

            std::thread render_thread_;
            std::atomic_bool shutdown_requested_{false};

            // RuntimeContext::Clear() tears down the process-wide composition
            // root. Engine instances are therefore terminal after Clear(); a
            // new initialize cycle uses a new Engine instance.
            bool startup_level_loaded_ = false;
            bool initialization_started_ = false;
            bool cleared_ = false;
            bool editor_attached_ = false;

            // Immutable for one Engine lifetime. The entry point supplies the
            // parser-normalized Asset-root-relative path before Initialize().
            std::optional<std::string> startup_level_override_;
            std::optional<asset::AssetLoadSession> startup_asset_session_;
            StartupCoordinator startup_coordinator_;

            command::LocalCommandTransportConfig command_transport_config_{};
            std::unique_ptr<command::CommandLocalTransport> command_transport_;

            // Engine-owned editor. Its UI (ImGui) is initialized and ticked on the
            // render thread where the GL/Vulkan context lives (see InitEditorUI).
            std::unique_ptr<editor::Editor> editor_;
        };
    }
}

#endif
