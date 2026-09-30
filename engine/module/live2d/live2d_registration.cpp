#include "module/module_registration.h"

#include <memory>
#include <stdexcept>
#include <string>

#include "asset/asset_manager.h"
#include "live2d_viewer_host.h"
#include "runtime/engine.h"
#include "runtime/live2d_registration.h"

namespace kpengine::live2d
{
    namespace
    {
        void RegisterLive2D(runtime::Engine &engine)
        {
            if (engine.GetApplicationMode() != runtime::ApplicationMode::Live2DViewer)
            {
                return;
            }

            std::string diagnostic;
            if (!RegisterLive2DAssetTypes(asset::AssetManager::GetInstance(), diagnostic))
            {
                throw std::runtime_error("Live2D Asset registration failed: " + diagnostic);
            }
            if (!engine.RegisterApplicationHostProvider(
                    runtime::ApplicationMode::Live2DViewer,
                    [](runtime::Engine &)
                    {
                        return std::make_unique<Live2DViewerHost>();
                    },
                    diagnostic))
            {
                throw std::runtime_error("Live2D viewer host registration failed: " +
                                         diagnostic);
            }
        }

        const module::ModuleRegistration registration(&RegisterLive2D);
    }
}
