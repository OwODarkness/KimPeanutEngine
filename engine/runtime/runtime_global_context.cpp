#include "runtime_global_context.h"
#include "runtime/core/system/window_system.h"
#include "runtime/core/system/scene_system.h"
namespace kpengine
{
    namespace runtime{

       RuntimeContext global_runtime_context;

        void RuntimeContext::Initialize()
        {
            window_system_ = std::make_shared<WindowSystem>();
            window_system_->Initialize(WindowInitInfo::GetDefaultWindowInfo());

            scene_system_ = std::make_shared<SceneSystem>();
            scene_system_->Initialize();
        }

        void RuntimeContext::Clear()
        {
            window_system_.reset();
            scene_system_.reset();
        }
    }
}