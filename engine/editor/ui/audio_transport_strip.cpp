#include "audio_transport_strip.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "editor/ui/editor_ui_glow.h"

namespace kpengine::editor
{
    namespace
    {
        bool IsActive(const TransportPlaybackState state) noexcept
        {
            return state == TransportPlaybackState::Queued ||
                state == TransportPlaybackState::Generating ||
                state == TransportPlaybackState::Playing ||
                state == TransportPlaybackState::Buffering ||
                state == TransportPlaybackState::Draining;
        }

        bool IsPlaying(const TransportPlaybackState state) noexcept
        {
            return state == TransportPlaybackState::Playing ||
                state == TransportPlaybackState::Buffering;
        }

        void FormatTime(const float seconds, char *buffer, const std::size_t size)
        {
            const float time = std::isfinite(seconds) ? std::max(0.0f, seconds) : 0.0f;
            const int minutes = static_cast<int>(time / 60.0f);
            const float remainder = std::fmod(time, 60.0f);
            std::snprintf(buffer, size, "%02d:%04.1f", minutes, remainder);
        }

        ImU32 Darken(const ImU32 color, const float amount)
        {
            const ImVec4 value = ImGui::ColorConvertU32ToFloat4(color);
            return ImGui::ColorConvertFloat4ToU32(
                {value.x * amount, value.y * amount, value.z * amount, value.w});
        }

        bool DrawButton(const char *id, const char *label, const char *tooltip,
                        const EditorControlIcon &icon, const ImU32 tint,
                        const float width, const float height, const bool highlighted)
        {
            if (highlighted)
            {
                ImGui::PushStyleColor(ImGuiCol_Button,
                    ImGui::ColorConvertU32ToFloat4(Darken(tint, 0.18f)));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                    ImGui::ColorConvertU32ToFloat4(Darken(tint, 0.3f)));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                    ImGui::ColorConvertU32ToFloat4(Darken(tint, 0.42f)));
                ImGui::PushStyleColor(ImGuiCol_Border,
                    ImGui::ColorConvertU32ToFloat4(tint));
            }

            char stable_label[96]{};
            std::snprintf(stable_label, sizeof(stable_label), "%s%s", label, id);
            const ImVec2 button_size{width, height};
            const bool pressed = ImGui::Button(icon.IsValid() ? id : stable_label,
                                               button_size);
            if (icon.IsValid())
            {
                const ImVec2 button_min = ImGui::GetItemRectMin();
                const ImVec2 button_max = ImGui::GetItemRectMax();
                const float icon_size = std::min(button_size.x, button_size.y) * 0.5f;
                const float pixel_width = icon_size / static_cast<float>(icon.width);
                const float pixel_height = icon_size / static_cast<float>(icon.height);
                const ImVec2 icon_min{
                    button_min.x + (button_size.x - icon_size) * 0.5f,
                    button_min.y + (button_size.y - icon_size) * 0.5f};
                ImDrawList *const draw = ImGui::GetWindowDrawList();
                draw->PushClipRect(button_min, button_max, true);
                for (std::uint32_t y = 0; y < icon.height; ++y)
                {
                    std::uint32_t x = 0;
                    while (x < icon.width)
                    {
                        while (x < icon.width && icon.alpha[
                            static_cast<std::size_t>(y) * icon.width + x] < 12)
                            ++x;
                        const std::uint32_t run_start = x;
                        std::uint32_t alpha_sum = 0;
                        std::uint32_t run_count = 0;
                        while (x < icon.width && icon.alpha[
                            static_cast<std::size_t>(y) * icon.width + x] >= 12)
                        {
                            alpha_sum += icon.alpha[
                                static_cast<std::size_t>(y) * icon.width + x];
                            ++run_count;
                            ++x;
                        }
                        if (run_count == 0)
                            continue;
                        const std::uint32_t source_alpha = alpha_sum / run_count;
                        const std::uint32_t tint_alpha = tint >> IM_COL32_A_SHIFT;
                        const ImU32 color = (tint & 0x00ffffffu) |
                            (((source_alpha * tint_alpha) / 255u) << IM_COL32_A_SHIFT);
                        draw->AddRectFilled(
                            {icon_min.x + static_cast<float>(run_start) * pixel_width,
                             icon_min.y + static_cast<float>(y) * pixel_height},
                            {icon_min.x + static_cast<float>(x) * pixel_width,
                             icon_min.y + static_cast<float>(y + 1) * pixel_height}, color);
                    }
                }
                draw->PopClipRect();
            }

            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", tooltip);
            if (highlighted)
                ImGui::PopStyleColor(4);
            return pressed;
        }

        void DrawProgress(const TransportStripState &state,
                          const TransportStripStyle &style,
                          TransportStripActions &actions)
        {
            char elapsed[20]{};
            char total[20]{};
            FormatTime(state.elapsed_seconds, elapsed, sizeof(elapsed));
            const bool known_duration = state.duration_seconds.has_value() &&
                std::isfinite(*state.duration_seconds) && *state.duration_seconds > 0.0f;
            if (known_duration)
                FormatTime(*state.duration_seconds, total, sizeof(total));
            else
                std::snprintf(total, sizeof(total), "%s",
                              IsActive(state.playback_state) ? "LIVE" : "--:--");

            const ImVec2 origin = ImGui::GetCursorScreenPos();
            const float row_height = 24.0f;
            const float bar_height = 8.0f;
            const float time_font_scale = 1.2f;
            const float elapsed_width = ImGui::CalcTextSize(elapsed).x;
            const float separator_width = ImGui::CalcTextSize("/").x;
            const float total_width = ImGui::CalcTextSize(total).x;
            const float time_font_size = ImGui::GetFontSize() * time_font_scale;
            const float time_width =
                (elapsed_width + separator_width + total_width) * time_font_scale + 12.0f;
            const char *const state_label = style.show_state
                ? TransportPlaybackStateLabel(state.playback_state) : "";
            const float state_width = state_label[0] == '\0'
                ? 0.0f : ImGui::CalcTextSize(state_label).x + 12.0f;
            const float full_width = ImGui::GetContentRegionAvail().x;
            const float bar_width = std::max(1.0f, full_width - state_width - time_width - 14.0f);
            const ImVec2 bar_origin{origin.x + state_width, origin.y};
            if (state_width > 0.0f)
            {
                ImGui::GetWindowDrawList()->AddText(
                    {origin.x, origin.y + (row_height - ImGui::GetFontSize()) * 0.5f},
                    style.secondary_text, state_label);
            }

            ImGui::SetCursorScreenPos(bar_origin);
            ImGui::BeginDisabled(!state.capabilities.seek || !known_duration);
            ImGui::InvisibleButton("##TransportSeek", {bar_width, row_height});
            ImGui::EndDisabled();
            const bool can_seek = state.capabilities.seek && known_duration;

            ImDrawList *const draw = ImGui::GetWindowDrawList();
            const float center_y = bar_origin.y + row_height * 0.5f;
            const ImVec2 bar_min{bar_origin.x, center_y - bar_height * 0.5f};
            const ImVec2 bar_max{bar_origin.x + bar_width, center_y + bar_height * 0.5f};
            draw->AddRectFilled(bar_min, bar_max, style.track_background);
            draw->AddRect(bar_min, bar_max, style.track_border);

            float fill_start = bar_min.x;
            float fill_end = bar_min.x;
            if (known_duration)
            {
                const float elapsed = std::isfinite(state.elapsed_seconds)
                    ? state.elapsed_seconds : 0.0f;
                const float fraction = std::clamp(
                    elapsed / *state.duration_seconds, 0.0f, 1.0f);
                fill_end += bar_width * fraction;
            }
            else if (IsActive(state.playback_state))
            {
                const float segment_width = std::min(bar_width * 0.24f, 72.0f);
                const float travel = bar_width + segment_width;
                const float phase = static_cast<float>(std::fmod(ImGui::GetTime() * 0.45, 1.0));
                fill_start = bar_min.x + travel * phase - segment_width;
                fill_end = fill_start + segment_width;
                fill_start = std::max(fill_start, bar_min.x);
                fill_end = std::min(fill_end, bar_max.x);
            }
            if (fill_end > fill_start)
            {
                const ImVec2 fill_min{fill_start, bar_min.y};
                const ImVec2 fill_max{fill_end, bar_max.y};
                if (style.bloom_played)
                {
                    const std::uint32_t region = ImGui::GetID("##TransportGlowRegion");
                    BeginEditorGlowRegion(draw, region, fill_min, fill_max, 8.0f);
                    PushEditorGlowEmission(draw, region, style.played, 1.0f);
                }
                draw->AddRectFilled(fill_min, fill_max, style.played);
                if (known_duration)
                    draw->AddLine({fill_end, bar_min.y - 3.0f},
                                  {fill_end, bar_max.y + 3.0f}, style.played, 2.0f);
                if (style.bloom_played)
                {
                    const std::uint32_t region = ImGui::GetID("##TransportGlowRegion");
                    PopEditorGlowEmission(draw, region);
                    EndEditorGlowRegion(draw, region);
                }
            }

            if (can_seek && ImGui::IsItemActive() &&
                ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                const float fraction = std::clamp(
                    (ImGui::GetIO().MousePos.x - bar_origin.x) / bar_width, 0.0f, 1.0f);
                actions.seek_seconds = fraction * *state.duration_seconds;
            }
            if (can_seek && ImGui::IsItemFocused())
            {
                if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
                    actions.seek_seconds = std::max(0.0f, state.elapsed_seconds - 1.0f);
                if (ImGui::IsKeyPressed(ImGuiKey_RightArrow))
                    actions.seek_seconds = std::min(
                        *state.duration_seconds, state.elapsed_seconds + 1.0f);
            }
            if (can_seek && ImGui::IsItemHovered())
                ImGui::SetTooltip("Drag or use arrow keys to seek");

            const float time_x = origin.x + full_width - time_width;
            const float time_y = origin.y + (row_height - time_font_size) * 0.5f;
            ImFont *const font = ImGui::GetFont();
            draw->AddText(font, time_font_size, {time_x, time_y},
                          style.elapsed_text, elapsed);
            draw->AddText(font, time_font_size,
                          {time_x + elapsed_width * time_font_scale + 5.0f, time_y},
                          style.secondary_text, "/");
            draw->AddText(font, time_font_size,
                          {time_x + (elapsed_width + separator_width) * time_font_scale + 8.0f,
                           time_y}, style.total_text, total);
            ImGui::SetCursorScreenPos(origin);
            ImGui::Dummy({full_width, row_height});
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

    TransportStripActions DrawTransportStrip(const TransportStripState &state,
                                             const TransportStripStyle &style)
    {
        IM_ASSERT(!state.instance_id.empty());
        const char *const id_begin = state.instance_id.empty()
            ? "InvalidTransportStripInstance" : state.instance_id.data();
        const char *const id_end = state.instance_id.empty()
            ? id_begin + std::char_traits<char>::length(id_begin)
            : id_begin + state.instance_id.size();
        ImGui::PushID(id_begin, id_end);

        TransportStripActions actions{};
        DrawProgress(state, style, actions);
        const int button_count = static_cast<int>(state.capabilities.previous) +
            static_cast<int>(state.capabilities.toggle_play_pause) +
            static_cast<int>(state.capabilities.next) +
            static_cast<int>(state.capabilities.stop_voice) +
            static_cast<int>(state.capabilities.cancel_job);
        if (button_count > 0)
        {
            ImGui::Separator();
            ImGui::BeginDisabled(!state.controls_enabled);
            const float gap = 8.0f;
            const float available_width = ImGui::GetContentRegionAvail().x;
            const float button_width = std::min(style.button_width,
                std::max(32.0f, (available_width - gap * (button_count - 1)) / button_count));
            bool first_button = true;
            const auto draw_capability_button = [&](const bool enabled, const char *id,
                                                    const char *label, const char *tooltip,
                                                    const EditorControlIcon &icon,
                                                    const bool highlighted)
            {
                if (!enabled)
                    return false;
                if (!first_button)
                    ImGui::SameLine(0.0f, gap);
                first_button = false;
                return DrawButton(id, label, tooltip, icon, style.active_button,
                                  button_width, style.button_height, highlighted);
            };

            actions.previous = draw_capability_button(
                state.capabilities.previous, "##Previous", "[ PREV ]", "Previous track",
                state.icons.previous, false);
            const bool playing = IsPlaying(state.playback_state);
            actions.toggle_play_pause = draw_capability_button(
                state.capabilities.toggle_play_pause, "##Toggle", playing ? "[ PAUSE ]" : "[ PLAY ]",
                playing ? "Pause playback" : "Play", playing ? state.icons.pause : state.icons.play,
                playing);
            actions.next = draw_capability_button(
                state.capabilities.next, "##Next", "[ NEXT ]", "Next track",
                state.icons.next, false);
            actions.stop_voice = draw_capability_button(
                state.capabilities.stop_voice, "##StopVoice", "[ STOP ]", "Stop voice",
                state.icons.stop, false);
            actions.cancel_job = draw_capability_button(
                state.capabilities.cancel_job, "##CancelJob", "[ CANCEL ]", "Cancel job",
                state.icons.cancel, false);
            ImGui::EndDisabled();
        }

        ImGui::PopID();
        return actions;
    }
}
