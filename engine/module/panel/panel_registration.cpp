#include <memory>
#include <utility>

#include "panel_viewer_host.h"
#include "runtime/host/application_host.h"
#include "runtime/launch_options.h"

namespace kpengine::panel
{
    namespace
    {
        const runtime::ApplicationHostRegistration registration{
            {"panel-viewer",
             [](const runtime::RuntimeLaunchOptions &options)
             {
                 PanelViewerConfiguration configuration{};
                 configuration.content = {
                     options.panel_glyph_product,
                     options.panel_text};
                 configuration.dot_color = options.panel_dot_color;
                 configuration.accent_color = options.panel_accent_color;
                 configuration.gradient_amount = options.panel_gradient.value_or(0.0f);
                 configuration.launch =
                     runtime::BuildStandaloneViewerLaunchConfiguration(options);
                 return std::make_unique<PanelViewerHost>(std::move(configuration));
             },
             runtime::ApplicationHostLayout::OwnedWindow,
             false}};
    }
}
