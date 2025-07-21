#include "runtime_global_context.h"

#include <iostream>

#include "runtime/core/system/window_system.h"
#include "runtime/core/system/render_system.h"
#include "runtime/core/system/log_system.h"
#include "runtime/core/system/asset_system.h"
#include "runtime/core/system/level_system.h"

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
        runtime_mode_(RuntimeMode::Editor)
        {
        }
        void RuntimeContext::Initialize()
        {
            window_system_->Initialize(WindowInitInfo::GetDefaultWindowInfo());
            window_system_->MakeContext();
            asset_system_->Initialize();

            render_system_->Initialize();
            
            level_system_->Initialize();
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