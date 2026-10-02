#ifndef KPENGINE_RUNTIME_AUDIO_EDITOR_AUDIO_PREVIEW_WIDGET_H
#define KPENGINE_RUNTIME_AUDIO_EDITOR_AUDIO_PREVIEW_WIDGET_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "editor/ui/audio_transport_strip.h"

struct ImFont;

namespace kpengine::audio_player
{
    using AudioControlIcon = editor::EditorControlIcon;

    struct AudioPreviewState
    {
        std::string_view clip_name;
        std::string_view format_label;
        std::string_view status;
        std::string_view subtitle_text;
        editor::TransportPlaybackState playback_state = editor::TransportPlaybackState::Idle;
        float current_time = 0.0f;
        float duration = 0.0f;
        float volume = 0.8f;
        float playback_rate = 1.0f;
        float rms = 0.0f;
        float peak = 0.0f;
        float height = 230.0f;
        std::span<const float> waveform_samples;
        std::span<const float> spectrum_bins;
        AudioControlIcon previous_icon;
        AudioControlIcon play_icon;
        AudioControlIcon pause_icon;
        AudioControlIcon next_icon;
        AudioControlIcon stop_icon;
        AudioControlIcon loop_icon;
        AudioControlIcon voice_open_icon;
        AudioControlIcon voice_close_icon;
        ImFont *font = nullptr;
        bool has_clip = false;
        bool is_muted = false;
        bool is_error = false;
        bool can_seek = false;
        bool has_subtitle_track = false;
        bool loop_enabled = false;
    };

    struct AudioPreviewActions
    {
        bool previous = false;
        bool toggle_play_pause = false;
        bool next = false;
        bool stop_voice = false;
        bool toggle_loop = false;
        bool cancel_job = false;
        std::optional<float> seek_seconds;
        std::optional<float> volume;
        std::optional<float> playback_rate;
        std::optional<bool> muted;
    };

    // Rendering consumes a snapshot and returns intent; playback stays outside ImGui.
    AudioPreviewActions DrawAudioPreview(const AudioPreviewState &state,
                                         std::string_view instance_id);
}

#endif
