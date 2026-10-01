#ifndef KPENGINE_MODULE_AUDIO_AUDIO_PREVIEW_WIDGET_H
#define KPENGINE_MODULE_AUDIO_AUDIO_PREVIEW_WIDGET_H

#include <optional>
#include <span>
#include <string_view>

struct ImFont;

namespace kpengine::audio_player
{
    struct AudioPreviewState
    {
        std::string_view clip_name;
        std::string_view format_label;
        std::string_view status;
        float current_time = 0.0f;
        float duration = 0.0f;
        float volume = 0.8f;
        float rms = 0.0f;
        float peak = 0.0f;
        float height = 230.0f;
        std::span<const float> waveform_samples;
        std::span<const float> spectrum_bins;
        ImFont *font = nullptr;
        bool has_clip = false;
        bool is_playing = false;
        bool is_error = false;
        bool can_seek = false;
    };

    struct AudioPreviewActions
    {
        bool previous = false;
        bool toggle_play_pause = false;
        bool next = false;
        bool stop = false;
        std::optional<float> seek_seconds;
        std::optional<float> volume;
    };

    // Rendering consumes a snapshot and returns intent; playback stays outside ImGui.
    AudioPreviewActions DrawAudioPreview(const AudioPreviewState &state);
}

#endif
