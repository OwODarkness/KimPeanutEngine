#include "runtime_global_context.h"
#include "runtime/core/system/window_system.h"
#include "runtime/core/system/render_system.h"
namespace kpengine
{
    namespace runtime{

       RuntimeContext global_runtime_context;

        void RuntimeContext::Initialize()
        {
            window_system_ = std::make_shared<WindowSystem>();
            window_system_->Initialize(WindowInitInfo::GetDefaultWindowInfo());

            render_system_ = std::make_shared<RenderSystem>();
            render_system_->Initialize();
        }

        void RuntimeContext::Clear()
        {
            window_system_.reset();
            render_system_.reset();
        }
    }
}