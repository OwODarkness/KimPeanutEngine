#include "runtime_global_context.h"
#include "runtime/core/system/window_system.h"
#include "runtime/core/system/render_system.h"
#include "runtime/core/system/log_system.h"
#include "runtime/core/system/asset_system.h"
#include "runtime/core/system/level_system.h"

namespace kpengine
{
    namespace runtime{

       RuntimeContext global_runtime_context;


        void RuntimeContext::Initialize()
        {
            log_system_ = std::make_unique<LogSystem>();

            window_system_ = std::make_unique<WindowSystem>();
            window_system_->Initialize(WindowInitInfo::GetDefaultWindowInfo());

            asset_system_ = std::make_unique<AssetSystem>();
            asset_system_->Initialize();

            render_system_ = std::make_unique<RenderSystem>();
            render_system_->Initialize();
            
            level_system_ = std::make_unique<LevelSystem>();
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