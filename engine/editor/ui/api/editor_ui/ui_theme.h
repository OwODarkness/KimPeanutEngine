#ifndef KPENGINE_EDITOR_UI_API_UI_THEME_H
#define KPENGINE_EDITOR_UI_API_UI_THEME_H

#include "ui_types.h"

namespace kpengine::editor::ui
{
    enum class UiFontRole : std::uint8_t { Body, Code };

    struct UiTheme
    {
        UiColor surface{0.10F, 0.11F, 0.14F, 1.0F};
        UiColor panel{0.14F, 0.15F, 0.19F, 1.0F};
        UiColor raised{0.19F, 0.21F, 0.25F, 1.0F};
        UiColor border{0.30F, 0.33F, 0.38F, 1.0F};
        UiColor text{0.94F, 0.95F, 0.97F, 1.0F};
        UiColor muted{0.62F, 0.66F, 0.72F, 1.0F};
        UiColor accent{0.25F, 0.76F, 0.88F, 1.0F};
        UiColor accent_active{0.98F, 0.68F, 0.22F, 1.0F};
        UiColor selected{0.28F, 0.24F, 0.16F, 1.0F};
        UiColor selected_hovered{0.38F, 0.30F, 0.16F, 1.0F};
        UiColor error{0.95F, 0.33F, 0.30F, 1.0F};
        UiColor success{0.37F, 0.80F, 0.59F, 1.0F};
        float window_padding_x = 8.0F;
        float window_padding_y = 6.0F;
        float frame_padding_x = 6.0F;
        float frame_padding_y = 3.0F;
        float item_spacing_x = 6.0F;
        float item_spacing_y = 4.0F;
        float inner_spacing_x = 4.0F;
        float inner_spacing_y = 4.0F;
        float window_rounding = 1.0F;
        float frame_rounding = 0.0F;
        float window_border_size = 1.0F;
        float frame_border_size = 1.0F;
        UiFontRole font = UiFontRole::Body;
    };
}

#endif
