#include "engine.h"
#include "runtime/runtime_global_context.h"
#include "runtime/core/system/window_system.h"
#include "runtime/core/log/logger.h"
namespace kpengine{
    namespace runtime{

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
            global_runtime_context.window_system_->Update();
            return true;
        }
    }
}