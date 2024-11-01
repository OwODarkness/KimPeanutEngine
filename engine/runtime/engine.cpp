#include "engine.h"
#include "runtime/runtime_global_context.h"
#include "runtime/core/system/window_system.h"
#include "runtime/core/system/render_system.h"
#include "runtime/core/log/logger.h"
#include <iostream>
namespace kpengine{
    namespace runtime{
        constexpr float fps_alpha = 1.f / 100.f;
        void Engine::Initialize()
        {
            global_runtime_context.Initialize();
            KP_LOG("EngineLog", LOG_LEVEL_DISPLAY, "Engine Initialize Successfully");
        }

        bool Engine::Tick()
        {
            if(global_runtime_context.window_system_->ShouldClose())
            {
                return false;
            }
            CalculateDeltaTime();
            global_runtime_context.window_system_->Tick();
            global_runtime_context.render_system_->Tick();
            std::cout << fps << std::endl;
            return true;
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

        void Engine::CalculateFPS(float delta_time)
        {
            frame_count++;
            if(frame_count == 1)
            {
                avg_time_cost = delta_time;
            }
            else
            {
                avg_time_cost = (1.f - fps_alpha) *  avg_time_cost + fps_alpha * delta_time;
            }
             fps = static_cast<int>(1.f / avg_time_cost);           
        }
    }
}