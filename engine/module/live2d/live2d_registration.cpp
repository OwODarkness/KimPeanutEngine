#include "module/module_registration.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include "asset/asset_manager.h"
#include "live2d_viewer_host.h"
#include "module/module_registration.h"
#include "runtime/engine.h"
#include "runtime/host/application_host.h"
#include "runtime/launch_options.h"
#include "runtime/live2d_registration.h"

namespace kpengine::live2d
{
    namespace
    {
        void RegisterLive2DAssets(runtime::Engine &engine)
        {
            if (engine.GetApplicationMode() != "live2d-viewer")
            {
                return;
            }

            std::string diagnostic;
            if (!RegisterLive2DAssetTypes(asset::AssetManager::GetInstance(), diagnostic))
            {
                throw std::runtime_error("Live2D Asset registration failed: " + diagnostic);
            }
        }

        const runtime::ApplicationHostRegistration host_registration{
            {"live2d-viewer",
             [](const runtime::RuntimeLaunchOptions &options)
             {
                 Live2DViewerConfiguration configuration{};
                 configuration.model_override = options.live2d_model_override;
                 configuration.bubble_content = {
                     options.panel_glyph_product,
                     options.panel_text};
                 configuration.launch =
                     runtime::BuildStandaloneViewerLaunchConfiguration(options);
                 return std::make_unique<Live2DViewerHost>(std::move(configuration));
             },
             runtime::ApplicationHostLayout::OwnedWindow,
             false}};
        const module::ModuleRegistration registration(&RegisterLive2DAssets);
    }
}
