#ifndef KPENGINE_RUNTIME_ENGINE_H
#define KPENGINE_RUNTIME_ENGINE_H

#include <chrono>
#include <thread>
#include <condition_variable>
#include <mutex>
#include <memory>
#include <atomic>

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

            inline int GetFPS() const { return measured_fps; }
            const int *GetFPSRef() const { return &measured_fps; }

        private:
            bool Tick();
            void GameThreadFunc();
            float CalculateDeltaTime();
            void CalculateFPS(float delta_time);

        private:
            std::chrono::steady_clock::time_point last_time{std::chrono::steady_clock::now()};
            int frame_count = 0;
            float avg_time_cost = 0.f;
            int target_fps = 120; // fixed update rate
            int measured_fps = 0; // for display

            std::condition_variable game_ready_cv_;
            std::mutex game_ready_mutex_;
            bool is_game_thread_loaded_ = false;
            std::thread game_thread_;

            std::unique_ptr<editor::Editor> editor_;
        };
    }
}

#endif