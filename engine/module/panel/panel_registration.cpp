#include <memory>

#include "panel_viewer_host.h"
#include "runtime/host/application_host.h"

namespace kpengine::panel
{
    namespace
    {
        const runtime::ApplicationHostRegistration registration{
            {"panel-viewer",
             [](runtime::Engine &) { return std::make_unique<PanelViewerHost>(); },
             runtime::ApplicationHostLayout::OwnedWindow,
             false}};
    }
}
