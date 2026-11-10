#include <memory>
#include <stdexcept>

#include "host/application_host.h"
#include "launch_options.h"
#include "scene_3d_host.h"

namespace kpengine::runtime::application
{
    void RegisterBuiltInHosts(ApplicationHostRegistry &registry)
    {
        std::string diagnostic;
        const bool registered = registry.Register(
            {"scene3d",
             [](const RuntimeLaunchOptions &) { return std::make_unique<Scene3DHost>(); },
             ApplicationHostLayout::SceneEditor,
             true},
            diagnostic);
        if (!registered)
        {
            throw std::runtime_error("Built-in scene3d host registration failed: " + diagnostic);
        }
    }
}
