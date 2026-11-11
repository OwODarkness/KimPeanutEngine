#ifndef KPENGINE_MODULE_AUDIO_AUDIO_PREVIEW_WIDGET_H
#define KPENGINE_MODULE_AUDIO_AUDIO_PREVIEW_WIDGET_H

#include <optional>

#include "module/audio/audio_player_controller.h"
#include "editor/ui/editor_icons.h"
#include "editor/ui/api/editor_ui/ui_backend.h"

namespace kpengine::audio_player
{
    struct AudioPreviewActions
    {
        bool previous = false;
        bool toggle_play_pause = false;
        bool next = false;
        bool stop = false;
        bool toggle_loop = false;
        std::optional<float> seek_seconds;
        std::optional<float> volume;
        std::optional<float> playback_rate;
        std::optional<bool> muted;
    };

    AudioPreviewActions DrawAudioPreview(editor::ui::UiContext &ui,
                                         const PlaybackView &playback,
                                         const editor::EditorBuiltinIcons &icons);
    void DrawAudioSpectrum(editor::ui::UiContext &ui, const PlaybackView &playback);
}

#endif
