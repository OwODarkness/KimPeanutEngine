#ifndef KPENGINE_RUNTIME_AUDIO_EDITOR_AUDIO_PREVIEW_WIDGET_H
#define KPENGINE_RUNTIME_AUDIO_EDITOR_AUDIO_PREVIEW_WIDGET_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

struct ImFont;

namespace kpengine::audio_player
{
    struct AudioControlIcon
    {
        std::span<const std::uint8_t> alpha;
        std::uint32_t width = 0;
        std::uint32_t height = 0;

        bool IsValid() const noexcept
        {
            return width > 0 && height > 0 && alpha.size() >=
                static_cast<std::size_t>(width) * height;
        }
    };

    struct AudioPreviewState
    {
        std::string_view clip_name;
        std::string_view format_label;
        std::string_view status;
        std::string_view subtitle_text;
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
        AudioControlIcon voice_open_icon;
        AudioControlIcon voice_close_icon;
        ImFont *font = nullptr;
        bool has_clip = false;
        bool is_playing = false;
        bool is_muted = false;
        bool is_error = false;
        bool can_seek = false;
        bool has_subtitle_track = false;
    };

    struct AudioPreviewActions
    {
        bool previous = false;
        bool toggle_play_pause = false;
        bool next = false;
        bool stop = false;
        std::optional<float> seek_seconds;
        std::optional<float> volume;
        std::optional<float> playback_rate;
        std::optional<bool> muted;
    };

    // Rendering consumes a snapshot and returns intent; playback stays outside ImGui.
    AudioPreviewActions DrawAudioPreview(const AudioPreviewState &state);
}

#endif
