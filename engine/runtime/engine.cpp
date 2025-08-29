#include "engine.h"
#include <cassert>
#include "runtime_global_context.h"
#include "window/window_system.h"
#include "render/render_system.h"
#include "game_framework/world_system.h"
#include "log/log_system.h"
#include "log/logger.h"
#include "editor/include/editor_global_context.h"
#include "editor/include/editor.h"

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
            global_runtime_context.Initialize();
            assert(editor_);
            editor_->Initialize(this);
            global_runtime_context.PostInitialize();
            global_runtime_context.render_thread_id_ = std::this_thread::get_id();
            game_thread_ = std::thread(&Engine::GameThreadFunc, this);
            KP_LOG("EngineLog", LOG_LEVEL_INFO, "Engine initialize successfully");
        }

        void Engine::Clear()
        {
            editor_->Clear();
        }

        void Engine::Run()
        {
            global_runtime_context.game_thread_id_ = game_thread_.get_id();
            while (Tick())
            {
            }
            game_thread_.join();
        }

        void Engine::GameThreadFunc()
        {
            using clock = std::chrono::steady_clock;
            const double target_frame_time = 1.0 / target_fps;

            while (!global_runtime_context.window_system_->ShouldClose())
            {
                auto frame_start = clock::now();

                global_runtime_context.world_system_->Tick(1.f / target_fps);

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
        }

        bool Engine::Tick()
        {
            if (global_runtime_context.window_system_->ShouldClose())
            {
                return false;
            }
            float delta_time = CalculateDeltaTime();

            {
                std::unique_lock<std::mutex> lock(game_ready_mutex_);
                game_ready_cv_.wait(lock, [this]
                                    { return is_game_thread_loaded_; });
                is_game_thread_loaded_ = false;
            }

            using clock = std::chrono::steady_clock;
            const double target_frame_time = 1.0 / target_fps;
            auto frame_start = clock::now();

            float delta = 1.f / target_fps;
            global_runtime_context.log_system_->Tick(delta);
            global_runtime_context.window_system_->Tick(delta);
            global_runtime_context.render_system_->Tick(delta);
            editor_->Tick();

            auto frame_end = clock::now();
            std::chrono::duration<double> elapsed = frame_end - frame_start;
            double sleep_seconds = target_frame_time - elapsed.count();

            if (sleep_seconds > 0.0)
            {
                std::this_thread::sleep_for(std::chrono::duration<double>(sleep_seconds));
            }

            return true;
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