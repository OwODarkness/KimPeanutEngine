#ifndef KPENGINE_MODULE_AUDIO_EDITOR_TRANSPORT_CONTROLS_H
#define KPENGINE_MODULE_AUDIO_EDITOR_TRANSPORT_CONTROLS_H

#include <optional>
#include <string_view>

#include "editor/ui/editor_icons.h"
#include "editor_ui/ui_backend.h"

namespace kpengine::audio_editor
{
    enum class TransportPlaybackState : std::uint8_t
    {
        Idle, Ready, Queued, Generating, Playing, Buffering, Draining, Paused,
        Finished, Cancelled, Failed
    };

    struct TransportStripCapabilities
    {
        bool previous = false;
        bool toggle_play_pause = false;
        bool next = false;
        bool stop_voice = false;
        bool cancel_job = false;
        bool toggle_loop = false;
        bool seek = false;
    };

    struct TransportStripState
    {
        std::string_view instance_id;
        TransportPlaybackState playback_state = TransportPlaybackState::Idle;
        float elapsed_seconds = 0.0F;
        std::optional<float> duration_seconds;
        bool loop_enabled = false;
        bool controls_enabled = true;
        TransportStripCapabilities capabilities;
    };

    struct AudioProgressStyle
    {
        editor::ui::UiColor background{2.0F / 255.0F, 9.0F / 255.0F, 14.0F / 255.0F, 1.0F};
        editor::ui::UiColor border{16.0F / 255.0F, 82.0F / 255.0F, 101.0F / 255.0F, 1.0F};
        editor::ui::UiColor fill{1.0F, 184.0F / 255.0F, 58.0F / 255.0F, 1.0F};
        editor::ui::UiColor secondary_text{48.0F / 255.0F, 211.0F / 255.0F, 239.0F / 255.0F, 1.0F};
        float label_width = 150.0F;
        float label_scale = 1.2F;
        bool glow = false;
    };

    struct TransportStripStyle
    {
        AudioProgressStyle progress{};
        bool show_state = true;
        bool stop_before_play = false;
        float button_width = 60.0F;
        float button_height = 34.0F;
    };

    struct TransportStripActions
    {
        bool previous = false;
        bool toggle_play_pause = false;
        bool next = false;
        bool stop_voice = false;
        bool cancel_job = false;
        bool toggle_loop = false;
        std::optional<float> seek_seconds;
    };

    const char *TransportPlaybackStateLabel(TransportPlaybackState state) noexcept;
    std::optional<float> DrawAudioProgressControl(
        editor::ui::UiContext &ui, const editor::ui::UiControlDescriptor &control,
        float fraction, editor::ui::UiVec2 size, std::string_view label,
        const AudioProgressStyle &style = {}, bool indeterminate = false);
    std::optional<float> DrawAudioVolumeProgressControl(
        editor::ui::UiContext &ui, std::string_view id, float volume, float width);
    TransportStripActions DrawTransportStrip(
        editor::ui::UiContext &ui, const TransportStripState &state,
        const editor::EditorBuiltinIcons &icons, const TransportStripStyle &style = {});
}

#endif
