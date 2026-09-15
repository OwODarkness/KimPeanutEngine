#ifndef KPENGINE_MODULE_PANEL_COMMAND_PROVIDER_H
#define KPENGINE_MODULE_PANEL_COMMAND_PROVIDER_H

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "command/command_registry.h"

namespace kpengine::panel
{
    // What the panel viewer reports about the state it owns. Runtime cannot name
    // any of it -- the loaded glyph product, the dot extent, the lit count -- so
    // the host that owns that state supplies the reader.
    struct PanelRuntimeReport final
    {
        bool glyphs_loaded = false;
        // Asset-root-relative spelling of the product actually loaded, so the
        // answer names its own fixture rather than echoing what was requested.
        std::string glyph_product;
        std::uint32_t first_codepoint = 0u;
        std::uint32_t glyph_count = 0u;
        std::uint32_t columns = 0u;
        std::uint32_t rows = 0u;
        std::uint32_t dot_width = 0u;
        std::uint32_t dot_height = 0u;
        std::uint32_t lit_dots = 0u;
        bool has_dot_mask = false;
        std::uint32_t live_gpu_handles = 0u;
        std::string text;
    };

    // Resolved per dispatch: the renderer is created on the render thread after
    // these commands are registered. Returns false when no panel is loaded.
    using PanelReportResolver = std::function<bool(PanelRuntimeReport &)>;

    // Applies new first-row text. Returns false and fills the diagnostic when
    // the panel cannot accept it.
    using PanelSetTextResolver =
        std::function<bool(std::string_view text, std::string &diagnostic)>;

    struct PanelCommandRegistrationResult final
    {
        bool succeeded = false;
        std::string diagnostic;
        // Holding every token is what keeps the entries installed; the registry
        // releases them when the caller drops the result.
        std::vector<runtime::command::CommandRegistration> registrations;
    };

    // Registers panel.report and panel.set_text. The report command exists
    // because a capture shows pixels and nothing else: it cannot say which
    // product was loaded or how much of the panel is lit, so a run that quietly
    // fell back to a different product must not read as evidence.
    PanelCommandRegistrationResult RegisterPanelCommands(
        runtime::command::CommandRegistry &registry, PanelReportResolver report,
        PanelSetTextResolver set_text);
}

#endif
