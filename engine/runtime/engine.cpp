#include "engine.h"

#include<iostream>

#include "runtime/runtime_global_context.h"
#include "runtime/core/system/window_system.h"
#include "runtime/core/system/asset_system.h"
#include "runtime/core/system/render_system.h"
#include "runtime/core/log/logger.h"
#include "runtime/core/system/level_system.h"
#include "editor/include/editor_global_context.h"
#include "editor/include/editor.h"

namespace kpengine{
    namespace runtime{
        
        constexpr float fps_alpha = 0.1f;

        Engine::Engine():editor_(std::make_unique<editor::Editor>())
        {

        }

        Engine::~Engine()
        {
            editor_.reset();
        }

        void Engine::Initialize()
        {
            global_runtime_context.Initialize();
            assert(editor_);
            editor_->Initialize(this);
            global_runtime_context.render_thread_id_ = std::this_thread::get_id();
            render_thread_ = std::thread(&Engine::GameThreadFunc, this);
            KP_LOG("EngineLog", LOG_LEVEL_DISPLAY, "Engine Initialize Successfully");
        }

        void Engine::Clear()
        {
            editor_->Clear();
        }

        void Engine::Run()
        {
            while(true)
            {
                if(Tick() == false)
                {
                    break;
                }
                
            }
            OnGameThreadBegin();
        }

        void Engine::OnGameThreadBegin()
        {
            
            render_thread_.join();
            global_runtime_context.game_thread_id_ = render_thread_.get_id();
        }

        void Engine::GameThreadFunc()
        {
            while(!global_runtime_context.window_system_->ShouldClose())
            {
                is_game_thread_loaded_ = false;
                global_runtime_context.level_system_->Tick(1.f/fps);
                is_game_thread_loaded_ = true;
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        }

        bool Engine::Tick()
        {
            if(global_runtime_context.window_system_->ShouldClose())
            {
                return false;
            }
            float delta_time = CalculateDeltaTime();
            
            if(is_game_thread_loaded_ == false)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            global_runtime_context.render_system_->Tick(1.f/fps);
            global_runtime_context.window_system_->Tick(1.f/fps);
            editor_->Tick();


            std::this_thread::sleep_for(std::chrono::milliseconds(1));
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