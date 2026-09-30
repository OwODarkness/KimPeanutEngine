#include "module/module_registration.h"

#include <memory>
#include <stdexcept>
#include <string>

#include "module/panel/panel_viewer_host.h"
#include "runtime/engine.h"

namespace kpengine::panel
{
    namespace
    {
        void RegisterPanel(runtime::Engine &engine)
        {
            if (engine.GetApplicationMode() != runtime::ApplicationMode::PanelViewer)
            {
                return;
            }

            std::string diagnostic;
            if (!engine.RegisterApplicationHostProvider(
                    runtime::ApplicationMode::PanelViewer,
                    [](runtime::Engine &)
                    {
                        return std::make_unique<PanelViewerHost>();
                    },
                    diagnostic))
            {
                throw std::runtime_error("Panel viewer host registration failed: " +
                                         diagnostic);
            }
        }

        const module::ModuleRegistration registration(&RegisterPanel);
    }
}
