#ifndef KPENGINE_EDITOR_AUDIO_AUDIO_PLAYER_THEME_H
#define KPENGINE_EDITOR_AUDIO_AUDIO_PLAYER_THEME_H

#include "editor/ui/api/editor_ui/ui_theme.h"

namespace kpengine::audio_player
{
    struct AudioPlayerTheme final
    {
        using UiColor = editor::ui::UiColor;

        static constexpr UiColor surface{2.0F / 255.0F, 9.0F / 255.0F,
                                         14.0F / 255.0F, 1.0F};
        static constexpr UiColor panel{4.0F / 255.0F, 17.0F / 255.0F,
                                       23.0F / 255.0F, 1.0F};
        static constexpr UiColor raised{7.0F / 255.0F, 32.0F / 255.0F,
                                        42.0F / 255.0F, 1.0F};
        static constexpr UiColor border{16.0F / 255.0F, 82.0F / 255.0F,
                                        101.0F / 255.0F, 1.0F};
        static constexpr UiColor grid{11.0F / 255.0F, 48.0F / 255.0F,
                                       62.0F / 255.0F, 1.0F};
        static constexpr UiColor cyan{48.0F / 255.0F, 211.0F / 255.0F,
                                       239.0F / 255.0F, 1.0F};
        static constexpr UiColor cyan_dim{20.0F / 255.0F, 111.0F / 255.0F,
                                           137.0F / 255.0F, 1.0F};
        static constexpr UiColor amber{1.0F, 184.0F / 255.0F, 58.0F / 255.0F, 1.0F};
        static constexpr UiColor muted{133.0F / 255.0F, 160.0F / 255.0F,
                                        169.0F / 255.0F, 1.0F};
        static constexpr UiColor text{220.0F / 255.0F, 242.0F / 255.0F,
                                       248.0F / 255.0F, 1.0F};
        static constexpr UiColor error{245.0F / 255.0F, 85.0F / 255.0F,
                                        78.0F / 255.0F, 1.0F};
        static constexpr UiColor ready{95.0F / 255.0F, 205.0F / 255.0F,
                                        151.0F / 255.0F, 1.0F};
        static constexpr UiColor selected{51.0F / 255.0F, 39.0F / 255.0F,
                                           17.0F / 255.0F, 1.0F};
        static constexpr UiColor selected_hovered{70.0F / 255.0F, 49.0F / 255.0F,
                                                   18.0F / 255.0F, 1.0F};
        static constexpr UiColor background = panel;
        static constexpr UiColor waveform = cyan;
        static constexpr UiColor played = amber;
        static constexpr UiColor spectrum = cyan;

        static constexpr editor::ui::UiTheme style{
            .surface = surface,
            .panel = panel,
            .raised = raised,
            .border = border,
            .text = text,
            .muted = muted,
            .accent = cyan,
            .accent_active = amber,
            .selected = selected,
            .selected_hovered = selected_hovered,
            .error = error,
            .success = ready,
            .window_padding_x = 6.0F,
            .window_padding_y = 5.0F,
            .frame_padding_x = 6.0F,
            .frame_padding_y = 3.0F,
            .item_spacing_x = 5.0F,
            .item_spacing_y = 4.0F,
            .inner_spacing_x = 4.0F,
            .inner_spacing_y = 3.0F,
            .window_rounding = 1.0F,
            .frame_rounding = 0.0F,
            .window_border_size = 1.0F,
            .frame_border_size = 1.0F,
            .font = editor::ui::UiFontRole::Code};
    };
}

#endif
