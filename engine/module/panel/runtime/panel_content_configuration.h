#ifndef KPENGINE_PANEL_RUNTIME_PANEL_CONTENT_CONFIGURATION_H
#define KPENGINE_PANEL_RUNTIME_PANEL_CONTENT_CONFIGURATION_H

#include <optional>
#include <string>

namespace kpengine::panel
{
    struct PanelContentConfiguration
    {
        std::optional<std::string> glyph_product;
        std::optional<std::string> text;
    };
}

#endif // KPENGINE_PANEL_RUNTIME_PANEL_CONTENT_CONFIGURATION_H
