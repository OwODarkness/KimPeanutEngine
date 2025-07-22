#include "runtime_global_context.h"

#include <iostream>

#include "runtime/core/system/window_system.h"
#include "runtime/core/system/render_system.h"
#include "runtime/core/system/log_system.h"
#include "runtime/core/system/asset_system.h"
#include "runtime/core/system/level_system.h"
#include "runtime/core/system/input_system.h"
namespace kpengine
{
    namespace runtime{

       RuntimeContext global_runtime_context;

        RuntimeContext::RuntimeContext():
        window_system_(std::make_unique<WindowSystem>()),        
        render_system_(std::make_unique<RenderSystem>()),
        log_system_(std::make_unique<LogSystem>()),
        asset_system_(std::make_unique<AssetSystem>()),
        level_system_(std::make_unique<LevelSystem>()),
        input_system_(std::make_unique<input::InputSystem>()),
        runtime_mode_(RuntimeMode::Editor)
        {
        }
        void RuntimeContext::Initialize()
        {
            window_system_->Initialize(WindowInitInfo::GetDefaultWindowInfo());
            input_system_->Initialize(window_system_->GetOpenGLWndow());
            asset_system_->Initialize();

            render_system_->Initialize();
            
            level_system_->Initialize();
        }

        void RuntimeContext::PostInitialize()
        {
            render_system_->PostInitialize();   
        }

        void RuntimeContext::Clear()
        {
            window_system_.reset();
            render_system_.reset();
            asset_system_.reset();
            log_system_.reset();
        }

        RuntimeContext::~RuntimeContext() = default;

    }
}