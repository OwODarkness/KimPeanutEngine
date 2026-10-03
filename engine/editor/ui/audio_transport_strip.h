#ifndef KPENGINE_EDITOR_UI_AUDIO_TRANSPORT_STRIP_H
#define KPENGINE_EDITOR_UI_AUDIO_TRANSPORT_STRIP_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include <imgui.h>

namespace kpengine::editor
{
    struct EditorControlIcon
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

    enum class TransportPlaybackState : std::uint8_t
    {
        Idle,
        Ready,
        Queued,
        Generating,
        Playing,
        Buffering,
        Draining,
        Paused,
        Finished,
        Cancelled,
        Failed
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

    struct TransportStripIcons
    {
        EditorControlIcon previous;
        EditorControlIcon play;
        EditorControlIcon pause;
        EditorControlIcon next;
        EditorControlIcon stop;
        EditorControlIcon cancel;
        EditorControlIcon loop;
    };

    struct TransportStripState
    {
        // Must be unique among strips drawn in the same ImGui parent.
        std::string_view instance_id;
        TransportPlaybackState playback_state = TransportPlaybackState::Idle;
        float elapsed_seconds = 0.0f;
        std::optional<float> duration_seconds;
        bool loop_enabled = false;
        bool controls_enabled = true;
        TransportStripCapabilities capabilities;
        TransportStripIcons icons;
    };

    struct TransportStripStyle
    {
        ImU32 track_background = IM_COL32(16, 20, 25, 255);
        ImU32 track_border = IM_COL32(70, 90, 100, 255);
        ImU32 played = IM_COL32(255, 184, 58, 255);
        ImU32 elapsed_text = IM_COL32(255, 184, 58, 255);
        ImU32 secondary_text = IM_COL32(133, 160, 169, 255);
        ImU32 total_text = IM_COL32(48, 211, 239, 255);
        ImU32 active_button = IM_COL32(255, 184, 58, 255);
        ImU32 button_icon = IM_COL32(48, 211, 239, 255);
        bool bloom_played = false;
        bool show_state = true;
        float button_width = 60.0f;
        float button_height = 34.0f;
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
    void DrawEditorControlIcon(const EditorControlIcon &icon, ImVec2 origin,
                               float size, ImU32 tint);
    TransportStripActions DrawTransportStrip(
        const TransportStripState &state,
        const TransportStripStyle &style = {});
}

#endif
