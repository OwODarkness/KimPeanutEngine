#ifndef KPENGINE_RUNTIME_ENGINE_H
#define KPENGINE_RUNTIME_ENGINE_H

#include <chrono>
#include <thread>


namespace kpengine{
namespace runtime{
    class Engine{
        public:
        Engine() = default;

        void Initialize();

        bool Tick();

        void RenderThreadFunc();

        void OnRenderThreadBegin();

        float CalculateDeltaTime();

        void CalculateFPS(float delta_time);

        inline int GetFPS() const {return fps;} 

        private:
        std::chrono::steady_clock::time_point last_time{std::chrono::steady_clock::now()};
        int frame_count = 0;
        float avg_time_cost = 0.f;
        int fps = 0;

        bool is_game_thread_loaded_ = true;

        std::thread render_thread_;
    };
}
}

#endif