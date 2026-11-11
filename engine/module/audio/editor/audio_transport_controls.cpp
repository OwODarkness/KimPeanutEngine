#include "audio_transport_controls.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace kpengine::audio_editor
{
    namespace
    {
        using namespace editor::ui;

        float FiniteUnit(const float value) noexcept
        {
            return std::isfinite(value) ? std::clamp(value, 0.0F, 1.0F) : 0.0F;
        }

        std::string TimeLabel(const float value)
        {
            const double finite = std::isfinite(value) ? std::clamp<double>(value, 0.0, 3.6e8) : 0.0;
            const auto tenths = static_cast<std::uint64_t>(finite * 10.0);
            char label[32]{};
            std::snprintf(label, sizeof(label), "%02llu:%02llu.%llu",
                static_cast<unsigned long long>(tenths / 600),
                static_cast<unsigned long long>(tenths / 10 % 60),
                static_cast<unsigned long long>(tenths % 10));
            return label;
        }

        bool IsActive(const TransportPlaybackState state) noexcept
        {
            return state == TransportPlaybackState::Queued || state == TransportPlaybackState::Generating ||
                   state == TransportPlaybackState::Playing || state == TransportPlaybackState::Buffering ||
                   state == TransportPlaybackState::Draining;
        }
    }

    const char *TransportPlaybackStateLabel(const TransportPlaybackState state) noexcept
    {
        switch (state)
        {
        case TransportPlaybackState::Idle: return "NO CLIP";
        case TransportPlaybackState::Ready: return "READY";
        case TransportPlaybackState::Queued: return "QUEUED";
        case TransportPlaybackState::Generating: return "GENERATING";
        case TransportPlaybackState::Playing: return "PLAYING";
        case TransportPlaybackState::Buffering: return "BUFFERING";
        case TransportPlaybackState::Draining: return "DRAINING";
        case TransportPlaybackState::Paused: return "PAUSED";
        case TransportPlaybackState::Finished: return "FINISHED";
        case TransportPlaybackState::Cancelled: return "CANCELLED";
        case TransportPlaybackState::Failed: return "ERROR";
        }
        return "UNKNOWN";
    }

    std::optional<float> DrawAudioProgressControl(
        UiContext &ui, const UiControlDescriptor &control, const float fraction,
        const UiVec2 size, const std::string_view label, const AudioProgressStyle &style,
        const bool indeterminate)
    {
        if (!std::isfinite(size.x) || !std::isfinite(size.y) || size.x <= 0.0F || size.y <= 0.0F ||
            !std::isfinite(style.label_width) || style.label_width < 0.0F)
            throw std::invalid_argument("Audio progress requires a positive finite extent and label width");
        std::vector<UiCanvasPrimitive> marks;
        const auto point = [size](const float x, const float y) { return UiVec2{x / size.x, y / size.y}; };
        const auto box = [&](const float left, const float top, const float right, const float bottom,
                             const UiColor color, const bool fill = true)
        {
            marks.push_back({fill ? UiCanvasPrimitiveKind::FilledRect : UiCanvasPrimitiveKind::Rect,
                point(left, top), point(right, bottom), color});
        };
        const float inset = std::min(4.0F, size.x * 0.15F);
        const float reserve = label.empty() ? 0.0F : std::min(style.label_width, size.x * 0.5F);
        const float end = std::max(inset + 0.1F, size.x - reserve - inset);
        const float center = size.y * 0.5F;
        const float half = std::min(3.5F, size.y * 0.25F);
        box(inset, center - half, end, center + half, style.background);
        box(inset, center - half, end, center + half, style.border, false);
        float start = inset;
        float finish = std::lerp(inset, end, FiniteUnit(fraction));
        if (indeterminate)
        {
            const auto seconds = std::chrono::duration<double>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            const float phase = static_cast<float>(std::fmod(seconds * 0.45, 1.0));
            const float width = end - inset;
            start = std::clamp(inset + width * (phase * 1.24F - 0.24F), inset, end);
            finish = std::clamp(inset + width * phase * 1.24F, start, end);
        }
        if (finish > start)
        {
            if (style.glow)
            {
                auto faint = style.fill;
                faint.a *= 0.16F;
                box(start, center - half * 2.0F, finish, center + half * 2.0F, faint);
                faint.a = style.fill.a * 0.45F;
                box(start, center - half * 1.5F, finish, center + half * 1.5F, faint);
            }
            box(start, center - half, finish, center + half, style.fill);
        }
        if (!label.empty() && reserve > 0.0F)
            marks.push_back({.kind = UiCanvasPrimitiveKind::Text,
                .first = point(size.x - inset, center), .color = style.fill,
                .text = std::string{label}, .text_anchor = {1.0F, 0.5F}});
        const auto hit = ui.Canvas(control, size, std::move(marks));
        if (control.enabled && !indeterminate && (hit.pressed || hit.dragging))
            return FiniteUnit((hit.normalized_pointer.x * size.x - inset) / (end - inset));
        return std::nullopt;
    }

    std::optional<float> DrawAudioVolumeProgressControl(
        UiContext &ui, const std::string_view id, const float volume, const float width)
    {
        const float value = FiniteUnit(volume);
        const auto label = std::to_string(static_cast<int>(std::round(value * 100.0F))) + "%";
        AudioProgressStyle style;
        style.fill = {48.0F / 255.0F, 211.0F / 255.0F, 239.0F / 255.0F, 1.0F};
        style.label_width = 37.0F;
        return DrawAudioProgressControl(ui, {.id = id, .label = "Volume"}, value,
            {std::max(1.0F, width), 36.0F}, label, style);
    }

    TransportStripActions DrawTransportStrip(UiContext &ui, const TransportStripState &state,
        const editor::EditorBuiltinIcons &icons, const TransportStripStyle &style)
    {
        if (state.instance_id.empty()) throw std::invalid_argument("Audio transport requires a stable ID");
        TransportStripActions actions;
        const std::string prefix{state.instance_id};
        const bool known = state.duration_seconds && std::isfinite(*state.duration_seconds) &&
                           *state.duration_seconds > 0.0F;
        const bool active = IsActive(state.playback_state);
        const std::string label = TimeLabel(state.elapsed_seconds) + " / " +
            (known ? TimeLabel(*state.duration_seconds) : active ? "LIVE" : "--:--");
        if (style.show_state) ui.Text(TransportPlaybackStateLabel(state.playback_state), UiTextRole::Muted);
        const std::string seek_id = prefix + ".seek";
        if (const auto edit = DrawAudioProgressControl(ui,
                {.id = seek_id, .label = "Position", .enabled = state.controls_enabled && state.capabilities.seek && known},
                known ? state.elapsed_seconds / *state.duration_seconds : 0.0F,
                {std::max(1.0F, ui.AvailableContent().x), 24.0F}, label, style.progress, !known && active))
            actions.seek_seconds = *edit * *state.duration_seconds;

        const auto &cap = state.capabilities;
        const int count = static_cast<int>(cap.previous) + static_cast<int>(cap.toggle_play_pause) +
            static_cast<int>(cap.next) + static_cast<int>(cap.stop_voice) +
            static_cast<int>(cap.cancel_job) + static_cast<int>(cap.toggle_loop);
        if (count == 0) return actions;
        const float width = std::min(style.button_width,
            std::max(1.0F, (ui.AvailableContent().x - 5.0F * (count - 1)) / count));
        UiTheme theme;
        theme.accent = {48.0F / 255.0F, 211.0F / 255.0F, 239.0F / 255.0F, 1.0F};
        theme.accent_active = style.progress.fill;
        theme.item_spacing_x = 5.0F;
        const auto colors = ui.Theme(theme);
        bool first = true;
        const auto button = [&](const bool present, const char *suffix, const char *name,
                                const editor::EditorIcon icon, const bool highlighted = false)
        {
            if (!present) return false;
            if (!first) ui.SameLine();
            first = false;
            const std::string id = prefix + suffix;
            UiControlDescriptor control{id, name};
            control.enabled = state.controls_enabled;
            control.width = width;
            control.height = style.button_height;
            control.text_role = highlighted ? UiTextRole::Warning : UiTextRole::Accent;
            control.emphasized = highlighted;
            control.icon = icons.Get(icon);
            return ui.Button(control).activated;
        };
        using Icon = editor::EditorIcon;
        const bool playing = state.playback_state == TransportPlaybackState::Playing ||
                             state.playback_state == TransportPlaybackState::Buffering;
        actions.previous = button(cap.previous, ".previous", "Previous", Icon::SkipPrevious);
        if (style.stop_before_play) actions.stop_voice = button(cap.stop_voice, ".stop", "Stop", Icon::Stop);
        actions.toggle_play_pause = button(cap.toggle_play_pause, ".toggle", playing ? "Pause" : "Play",
                                           playing ? Icon::Pause : Icon::Play, playing);
        if (!style.stop_before_play) actions.stop_voice = button(cap.stop_voice, ".stop", "Stop", Icon::Stop);
        actions.next = button(cap.next, ".next", "Next", Icon::SkipNext);
        actions.cancel_job = button(cap.cancel_job, ".cancel", "Cancel", Icon::Stop);
        actions.toggle_loop = button(cap.toggle_loop, ".loop", "Loop", Icon::Repeat, state.loop_enabled);
        return actions;
    }
}
