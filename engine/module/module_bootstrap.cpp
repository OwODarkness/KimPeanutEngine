#include "module/module_bootstrap.h"

#include "runtime/engine.h"

#if KPENGINE_ENABLE_LIVE2D
#include "asset/asset_manager.h"
#include "module/live2d/live2d_viewer_host.h"
#include "module/live2d/runtime/live2d_registration.h"
#endif

#include <memory>
#include <stdexcept>

namespace kpengine::module
{
    void RegisterModules(kpengine::runtime::Engine &engine)
    {
#if KPENGINE_ENABLE_LIVE2D
        if (engine.GetApplicationMode() == runtime::ApplicationMode::Live2DViewer)
        {
            std::string diagnostic;
            if (!kpengine::live2d::RegisterLive2DAssetTypes(
                    kpengine::asset::AssetManager::GetInstance(), diagnostic))
            {
                throw std::runtime_error("Live2D Asset registration failed: " + diagnostic);
            }
            if (!engine.RegisterApplicationHostProvider(
                    runtime::ApplicationMode::Live2DViewer,
                    [](runtime::Engine &) {
                        return std::make_unique<kpengine::live2d::Live2DViewerHost>();
                    },
                    diagnostic))
            {
                throw std::runtime_error("Live2D viewer host registration failed: " +
                                         diagnostic);
            }
        }
#else
        (void)engine;
#endif
    }
}
