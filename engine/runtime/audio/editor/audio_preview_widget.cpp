#include "audio_preview_widget.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#include <imgui.h>

namespace kpengine::audio_player
{
    namespace
    {
        constexpr ImU32 kBackground = IM_COL32(2, 9, 14, 255);
        constexpr ImU32 kBorder = IM_COL32(12, 67, 82, 255);
        constexpr ImU32 kCyan = IM_COL32(31, 190, 222, 255);
        constexpr ImU32 kCyanDim = IM_COL32(13, 91, 113, 255);
        constexpr ImU32 kAmber = IM_COL32(255, 175, 45, 255);
        constexpr ImU32 kMuted = IM_COL32(133, 160, 169, 255);
        constexpr ImU32 kRed = IM_COL32(245, 85, 78, 255);
        constexpr ImU32 kGreen = IM_COL32(95, 205, 151, 255);
        constexpr float kPi = 3.14159265358979323846f;

        ImU32 BlendColor(const ImU32 from, const ImU32 to, const float amount)
        {
            const ImVec4 a = ImGui::ColorConvertU32ToFloat4(from);
            const ImVec4 b = ImGui::ColorConvertU32ToFloat4(to);
            const float t = std::clamp(amount, 0.0f, 1.0f);
            return ImGui::GetColorU32(ImVec4(a.x + (b.x - a.x) * t,
                a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t,
                a.w + (b.w - a.w) * t));
        }

        ImVec2 PointOnCircle(const ImVec2 center, const float angle, const float radius)
        {
            return {center.x + std::cos(angle) * radius,
                    center.y + std::sin(angle) * radius};
        }

        void DrawDigitalRing(ImDrawList &draw, const ImVec2 center, const float radius,
                             const int point_count, const float point_radius,
                             const ImU32 color)
        {
            for (int i = 0; i < point_count; ++i)
            {
                const float angle = -kPi * 0.5f + 2.0f * kPi *
                    static_cast<float>(i) / static_cast<float>(point_count);
                draw.AddCircleFilled(PointOnCircle(center, angle, radius),
                                     point_radius, color, 6);
            }
        }

        void DrawCorners(ImDrawList &draw, const ImVec2 min, const ImVec2 max)
        {
            constexpr float mark = 8.0f;
            const std::array<ImVec2, 4> corners{{min, {max.x, min.y}, max, {min.x, max.y}}};
            for (std::size_t i = 0; i < corners.size(); ++i)
            {
                const float dx = (i == 0 || i == 3) ? 1.0f : -1.0f;
                const float dy = i < 2 ? 1.0f : -1.0f;
                draw.AddLine(corners[i], {corners[i].x + dx * mark, corners[i].y}, kCyan, 1.4f);
                draw.AddLine(corners[i], {corners[i].x, corners[i].y + dy * mark}, kCyan, 1.4f);
            }
        }

        void DrawScopeFrame(ImDrawList &draw, const ImVec2 min, const float size)
        {
            const ImVec2 max{min.x + size, min.y + size};
            draw.AddRect(min, max, kBorder, 0.0f, 0, 1.0f);
            constexpr float corner_size = 14.0f;
            const std::array<ImVec2, 4> corners{{min, {max.x, min.y}, max, {min.x, max.y}}};
            for (std::size_t i = 0; i < corners.size(); ++i)
            {
                const float dx = (i == 0 || i == 3) ? 1.0f : -1.0f;
                const float dy = i < 2 ? 1.0f : -1.0f;
                draw.AddLine(corners[i],
                             {corners[i].x + dx * corner_size, corners[i].y}, kCyan, 2.0f);
                draw.AddLine(corners[i],
                             {corners[i].x, corners[i].y + dy * corner_size}, kCyan, 2.0f);
            }
        }

        void DrawRadialAudioScope(ImDrawList &draw, const ImVec2 center,
                                  const float radius, const AudioPreviewState &state)
        {
            const float rms = std::clamp(state.rms, 0.0f, 1.0f);
            DrawDigitalRing(draw, center, radius, 64, 1.35f, kCyanDim);
            DrawDigitalRing(draw, center, radius * 0.72f, 48, 1.25f, kCyanDim);
            DrawDigitalRing(draw, center, radius * (0.39f + rms * 0.07f),
                            36, 1.5f, kCyan);
            const float progress = state.has_clip && state.duration > 0.0f
                ? std::clamp(state.current_time / state.duration, 0.0f, 1.0f) : 0.0f;
            if (progress > 0.0f)
            {
                const int count = std::clamp(static_cast<int>(std::ceil(progress * 64.0f)),
                                             1, 64);
                for (int i = 0; i < count; ++i)
                {
                    const float angle = -kPi * 0.5f + progress * 2.0f * kPi *
                        static_cast<float>(i) / static_cast<float>(count);
                    draw.AddCircleFilled(PointOnCircle(center, angle, radius * 1.08f),
                                         2.0f, kAmber, 6);
                }
            }
            if (!state.is_playing || state.spectrum_bins.empty())
            {
                const char *label = state.has_clip ? "IDLE" : "NO SIGNAL";
                const ImVec2 text_size = ImGui::CalcTextSize(label);
                draw.AddText({center.x - text_size.x * 0.5f, center.y - text_size.y * 0.5f},
                             kMuted, label);
                return;
            }
            const std::size_t count = std::min<std::size_t>(state.spectrum_bins.size(), 64);
            float strongest_magnitude = 0.0f;
            for (std::size_t i = 0; i < count; ++i)
                strongest_magnitude = std::max(strongest_magnitude,
                    std::clamp(state.spectrum_bins[i], 0.0f, 1.0f));
            const float accent_threshold = strongest_magnitude * 0.78f;
            for (std::size_t i = 0; i < count; ++i)
            {
                const float magnitude = std::clamp(state.spectrum_bins[i], 0.0f, 1.0f);
                const float angle = -kPi * 0.5f + static_cast<float>(i) * 2.0f * kPi /
                    static_cast<float>(count);
                const float inner = radius * 0.53f;
                const float outer = inner + radius * (0.06f + magnitude * 0.34f);
                const int dot_count = std::clamp(
                    static_cast<int>((outer - inner) / 4.0f) + 1, 2, 10);
                const bool accent_bar = strongest_magnitude > 0.02f &&
                    magnitude >= accent_threshold;
                for (int dot = 0; dot < dot_count; ++dot)
                {
                    const float t = static_cast<float>(dot) /
                        static_cast<float>(dot_count - 1);
                    const ImVec2 position = PointOnCircle(center, angle,
                        inner + (outer - inner) * t);
                    const ImU32 color = accent_bar && dot >= dot_count / 2
                        ? kAmber : kCyan;
                    draw.AddRectFilled({position.x - 1.25f, position.y - 1.25f},
                                       {position.x + 1.25f, position.y + 1.25f}, color);
                }
            }
            char rms_text[16]{};
            std::snprintf(rms_text, sizeof(rms_text), "RMS %02d",
                          static_cast<int>(std::round(rms * 99.0f)));
            const ImVec2 text_size = ImGui::CalcTextSize(rms_text);
            draw.AddText({center.x - text_size.x * 0.5f, center.y - text_size.y * 0.5f},
                         kCyan, rms_text);
        }

        void DrawWaveform(ImDrawList &draw, const ImVec2 origin, const ImVec2 size,
                          const AudioPreviewState &state, AudioPreviewActions &actions)
        {
            ImGui::SetCursorScreenPos(origin);
            ImGui::InvisibleButton("##AudioPreviewSeek", size);
            const ImVec2 end{origin.x + size.x, origin.y + size.y};
            draw.AddRectFilled(origin, end, kBackground);
            constexpr float kBorderDotSpacing = 5.0f;
            for (float x = origin.x; x <= end.x; x += kBorderDotSpacing)
            {
                draw.AddCircleFilled({x, origin.y}, 1.0f, kCyanDim, 4);
                draw.AddCircleFilled({x, end.y}, 1.0f, kCyanDim, 4);
            }
            for (float y = origin.y; y <= end.y; y += kBorderDotSpacing)
            {
                draw.AddCircleFilled({origin.x, y}, 1.0f, kCyanDim, 4);
                draw.AddCircleFilled({end.x, y}, 1.0f, kCyanDim, 4);
            }
            if (!state.has_clip || state.waveform_samples.empty())
            {
                draw.AddText({origin.x + 9.0f, origin.y + size.y * 0.5f - 7.0f},
                             kMuted, "LOAD CLIP FOR WAVEFORM");
                return;
            }
            constexpr float kVisibleSeconds = 18.0f;
            constexpr float kAxisHeight = 22.0f;
            constexpr float kHorizontalInset = 10.0f;
            const float visible_duration = std::min(kVisibleSeconds, state.duration);
            if (visible_duration <= 0.0f)
                return;
            const float maximum_start = std::max(0.0f, state.duration - visible_duration);
            const float window_start = std::clamp(
                state.current_time - visible_duration * 0.5f, 0.0f, maximum_start);
            const float window_end = window_start + visible_duration;
            const float plot_left = origin.x + kHorizontalInset;
            const float plot_right = end.x - kHorizontalInset;
            const float plot_width = std::max(1.0f, plot_right - plot_left);
            const float axis_y = end.y - kAxisHeight;
            const float plot_top = origin.y + 7.0f;
            const float plot_bottom = axis_y - 5.0f;
            const float plot_height = std::max(1.0f, plot_bottom - plot_top);
            const float center_y = plot_top + plot_height * 0.5f;
            constexpr float kGridSpacing = 16.0f;
            constexpr ImU32 kGridPoint = IM_COL32(9, 38, 49, 255);
            for (float y = plot_top; y < plot_bottom; y += kGridSpacing)
            {
                for (float x = plot_left; x < plot_right; x += kGridSpacing)
                    draw.AddRectFilled({x, y}, {x + 1.0f, y + 1.0f}, kGridPoint);
            }
            draw.AddLine({plot_left, center_y}, {plot_right, center_y}, kCyanDim, 1.0f);

            const float minimum_tick_spacing = 72.0f;
            const int tick_interval = std::max(1, static_cast<int>(std::ceil(
                minimum_tick_spacing * visible_duration / plot_width)));
            const int first_tick = static_cast<int>(std::ceil(
                window_start / static_cast<float>(tick_interval))) * tick_interval;
            char tick_label[16]{};
            for (int tick = first_tick; static_cast<float>(tick) <= window_end; tick += tick_interval)
            {
                const float fraction = (static_cast<float>(tick) - window_start) /
                    visible_duration;
                const float x = plot_left + fraction * plot_width;
                draw.AddLine({x, plot_top}, {x, axis_y}, IM_COL32(13, 51, 64, 255), 1.0f);
                draw.AddLine({x, axis_y - 3.0f}, {x, axis_y + 2.0f}, kCyanDim, 1.0f);
                const int minutes = tick / 60;
                const int seconds = tick % 60;
                std::snprintf(tick_label, sizeof(tick_label), "%02d:%02d", minutes, seconds);
                const ImVec2 label_size = ImGui::CalcTextSize(tick_label);
                const float label_x = std::clamp(x - label_size.x * 0.5f,
                    origin.x + 2.0f, end.x - label_size.x - 2.0f);
                draw.AddText({label_x, axis_y + 3.0f}, kMuted, tick_label);
            }
            draw.AddLine({plot_left, axis_y}, {plot_right, axis_y}, kCyanDim, 1.0f);

            const std::size_t count = state.waveform_samples.size();
            constexpr float kBarSpacing = 12.0f;
            const float column_duration = visible_duration * kBarSpacing / plot_width;
            const float column_width = plot_width * column_duration / visible_duration;
            const float bar_width = std::min(2.5f, column_width * 0.5f);
            const std::size_t first_column = static_cast<std::size_t>(
                std::floor(window_start / column_duration));
            const std::size_t last_column = static_cast<std::size_t>(
                std::ceil(window_end / column_duration));
            draw.PushClipRect({plot_left, plot_top}, {plot_right, plot_bottom}, true);
            for (std::size_t column = first_column; column < last_column; ++column)
            {
                const float start_time = static_cast<float>(column) * column_duration;
                const float end_time = std::min(state.duration, start_time + column_duration);
                const std::size_t first_sample = std::min(count, static_cast<std::size_t>(
                    std::floor(start_time / state.duration * static_cast<float>(count))));
                const std::size_t last_sample = std::min(count, std::max(first_sample + 1,
                    static_cast<std::size_t>(std::ceil(
                        end_time / state.duration * static_cast<float>(count)))));
                float amplitude = 0.0f;
                for (std::size_t sample = first_sample; sample < last_sample; ++sample)
                    amplitude = std::max(amplitude,
                        std::clamp(state.waveform_samples[sample], 0.0f, 1.0f));
                const float column_time = (start_time + end_time) * 0.5f;
                const float x = plot_left +
                    (column_time - window_start) / visible_duration * plot_width;
                const float proximity = std::clamp(
                    1.0f - std::abs(column_time - state.current_time) / 2.5f,
                    0.0f, 1.0f);
                const float pulse = state.is_playing
                    ? std::sin(static_cast<float>(ImGui::GetTime()) * 12.0f +
                               static_cast<float>(column) * 0.37f) *
                        std::clamp(state.rms, 0.0f, 1.0f) * proximity * 0.55f
                    : 0.0f;
                const float half = std::max(1.0f,
                    amplitude * plot_height * 0.46f * (1.0f + pulse));
                constexpr float kPlayheadFadeSeconds = 1.5f;
                const float behind_playhead = state.current_time - column_time;
                const float fade_distance = behind_playhead >= 0.0f
                    ? std::clamp(1.0f - behind_playhead / kPlayheadFadeSeconds,
                                 0.0f, 1.0f)
                    : 0.0f;
                const float orange_amount = fade_distance * fade_distance *
                    (3.0f - 2.0f * fade_distance);
                draw.AddLine({x, center_y - half}, {x, center_y + half},
                    BlendColor(kCyan, kAmber, orange_amount), bar_width);
            }
            const float playhead = plot_left + std::clamp(
                (state.current_time - window_start) / visible_duration, 0.0f, 1.0f) * plot_width;
            draw.AddLine({playhead, plot_top}, {playhead, plot_bottom}, kAmber, 1.5f);
            draw.AddCircleFilled({playhead, plot_top}, 3.0f, kAmber, 8);
            draw.PopClipRect();
            if (state.can_seek && ImGui::IsItemActive() &&
                ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                const float fraction = std::clamp(
                    (ImGui::GetIO().MousePos.x - plot_left) / plot_width, 0.0f, 1.0f);
                actions.seek_seconds = window_start + fraction * visible_duration;
            }
            if (state.can_seek && ImGui::IsItemFocused())
            {
                if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
                    actions.seek_seconds = std::max(0.0f, state.current_time - 1.0f);
                if (ImGui::IsKeyPressed(ImGuiKey_RightArrow))
                    actions.seek_seconds = std::min(state.duration, state.current_time + 1.0f);
            }
            if (state.can_seek && ImGui::IsItemHovered())
                ImGui::SetTooltip("Drag or use arrow keys to seek");
        }

        void DrawProgressBar(ImDrawList &draw, const float width,
                             const AudioPreviewState &state, AudioPreviewActions &actions)
        {
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            const auto format_time = [](const float seconds, char *buffer,
                                        const std::size_t buffer_size)
            {
                const float time = std::max(0.0f, seconds);
                const int minutes = static_cast<int>(time / 60.0f);
                const float remainder = std::fmod(time, 60.0f);
                std::snprintf(buffer, buffer_size, "%02d:%04.1f", minutes, remainder);
            };
            char elapsed[20]{};
            char total[20]{};
            format_time(state.current_time, elapsed, sizeof(elapsed));
            format_time(state.duration, total, sizeof(total));
            constexpr float row_height = 24.0f;
            constexpr float bar_height = 8.0f;
            constexpr float time_font_scale = 1.2f;
            const float elapsed_width = ImGui::CalcTextSize(elapsed).x;
            const float separator_width = ImGui::CalcTextSize("/").x;
            const float total_width = ImGui::CalcTextSize(total).x;
            const float time_font_size = ImGui::GetFontSize() * time_font_scale;
            const float time_width =
                (elapsed_width + separator_width + total_width) * time_font_scale + 12.0f;
            const float bar_width = std::max(60.0f, width - time_width - 14.0f);
            ImGui::InvisibleButton("##AudioPreviewProgress", ImVec2(bar_width, row_height));
            const float center_y = origin.y + row_height * 0.5f;
            const ImVec2 min{origin.x, center_y - bar_height * 0.5f};
            const ImVec2 max{origin.x + bar_width, center_y + bar_height * 0.5f};
            draw.AddRectFilled(min, max, kBackground);
            draw.AddRect(min, max, kBorder);
            const float progress = state.has_clip && state.duration > 0.0f
                ? std::clamp(state.current_time / state.duration, 0.0f, 1.0f) : 0.0f;
            if (progress > 0.0f)
            {
                const float playhead_x = min.x + bar_width * progress;
                draw.AddRectFilled(min, {playhead_x, max.y}, kCyan);
            }
            if (state.has_clip && state.duration > 0.0f)
            {
                const float playhead_x = min.x + bar_width * progress;
                draw.AddLine({playhead_x, min.y - 3.0f},
                             {playhead_x, max.y + 3.0f}, kAmber, 2.0f);
            }
            if (state.can_seek && ImGui::IsItemActive() &&
                ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                const float fraction = std::clamp(
                    (ImGui::GetIO().MousePos.x - origin.x) / bar_width, 0.0f, 1.0f);
                actions.seek_seconds = fraction * state.duration;
            }
            const float time_x = origin.x + width - time_width;
            const float time_y = origin.y + (row_height - time_font_size) * 0.5f;
            ImFont *font = ImGui::GetFont();
            draw.AddText(font, time_font_size, {time_x, time_y}, kAmber, elapsed);
            draw.AddText(font, time_font_size,
                         {time_x + elapsed_width * time_font_scale + 5.0f, time_y},
                         kMuted, "/");
            draw.AddText(font, time_font_size,
                         {time_x + (elapsed_width + separator_width) * time_font_scale + 8.0f,
                          time_y}, kCyan, total);
            ImGui::SetCursorScreenPos(origin);
            ImGui::Dummy(ImVec2(width, row_height));
        }

        bool DrawControlButton(const char *id, const char *fallback_label,
                               const char *tooltip, const AudioControlIcon &icon,
                               const bool highlighted = false,
                               const float button_width = 60.0f)
        {
            const ImVec2 button_size{button_width, 36.0f};
            if (highlighted)
            {
                ImGui::PushStyleColor(ImGuiCol_Button,
                    ImGui::ColorConvertU32ToFloat4(IM_COL32(65, 39, 12, 255)));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                    ImGui::ColorConvertU32ToFloat4(IM_COL32(103, 60, 14, 255)));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                    ImGui::ColorConvertU32ToFloat4(IM_COL32(135, 78, 18, 255)));
                ImGui::PushStyleColor(ImGuiCol_Border,
                    ImGui::ColorConvertU32ToFloat4(kAmber));
            }
            const bool pressed = ImGui::Button(
                icon.IsValid() ? id : fallback_label, button_size);
            if (!icon.IsValid())
            {
                if (highlighted)
                    ImGui::PopStyleColor(4);
                return pressed;
            }

            const ImVec2 button_min = ImGui::GetItemRectMin();
            const ImVec2 button_max = ImGui::GetItemRectMax();
            const float icon_size = std::min(button_size.x, button_size.y) * 0.5f;
            const float pixel_width = icon_size / static_cast<float>(icon.width);
            const float pixel_height = icon_size / static_cast<float>(icon.height);
            const ImVec2 icon_min{button_min.x + (button_size.x - icon_size) * 0.5f,
                                  button_min.y + (button_size.y - icon_size) * 0.5f};
            const ImU32 tint = highlighted ? kAmber :
                ImGui::GetColorU32(ImVec4(0.10f, 0.78f, 0.91f, 1.0f));
            ImDrawList *draw = ImGui::GetWindowDrawList();
            draw->PushClipRect(button_min, button_max, true);
            for (std::uint32_t y = 0; y < icon.height; ++y)
            {
                std::uint32_t x = 0;
                while (x < icon.width)
                {
                    while (x < icon.width && icon.alpha[static_cast<std::size_t>(y) *
                           icon.width + x] < 12)
                        ++x;
                    if (x == icon.width)
                        break;

                    const std::uint32_t run_start = x;
                    std::uint32_t alpha_sum = 0;
                    std::uint32_t run_count = 0;
                    while (x < icon.width && icon.alpha[static_cast<std::size_t>(y) *
                           icon.width + x] >= 12)
                    {
                        alpha_sum += icon.alpha[static_cast<std::size_t>(y) * icon.width + x];
                        ++run_count;
                        ++x;
                    }
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
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", tooltip);
            if (highlighted)
                ImGui::PopStyleColor(4);
            return pressed;
        }

        void DrawPlaybackControls(const AudioPreviewState &state,
                                  AudioPreviewActions &actions)
        {
            constexpr float kTransportButtonWidth = 78.0f;
            ImGui::BeginDisabled(!state.has_clip);
            actions.previous = DrawControlButton("##AudioPrevious", "[ PREV ]",
                                                 "Previous track", state.previous_icon,
                                                 false, kTransportButtonWidth);
            ImGui::SameLine(0.0f, 10.0f);
            actions.toggle_play_pause = DrawControlButton(
                "##AudioTogglePlayback", state.is_playing ? "[ PAUSE ]" : "[ PLAY ]",
                state.is_playing ? "Pause playback" : "Play", state.is_playing
                    ? state.pause_icon : state.play_icon, state.is_playing,
                kTransportButtonWidth);
            ImGui::SameLine(0.0f, 10.0f);
            actions.next = DrawControlButton("##AudioNext", "[ NEXT ]", "Next track",
                                             state.next_icon, false, kTransportButtonWidth);
            ImGui::SameLine(0.0f, 10.0f);
            actions.stop = DrawControlButton("##AudioStop", "[ STOP ]", "Stop playback",
                                             state.stop_icon, false, kTransportButtonWidth);
            ImGui::EndDisabled();
        }

        void DrawPlaybackRateControl(const AudioPreviewState &state,
                                     AudioPreviewActions &actions)
        {
            struct RateOption
            {
                float value;
                const char *label;
            };
            constexpr std::array<RateOption, 6> rates{{
                {0.5f, "0.5x"}, {0.75f, "0.75x"}, {1.0f, "1.0x"},
                {1.25f, "1.25x"}, {1.5f, "1.5x"}, {2.0f, "2.0x"}}};
            const RateOption *selected_rate = &rates[2];
            for (const RateOption &rate : rates)
            {
                if (std::abs(state.playback_rate - rate.value) < 0.001f)
                {
                    selected_rate = &rate;
                    break;
                }
            }
            ImGui::SetNextItemWidth(132.0f);
            ImGui::PushStyleColor(ImGuiCol_Text,
                ImGui::ColorConvertU32ToFloat4(kCyan));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                ImVec2(ImGui::GetStyle().FramePadding.x,
                       std::max(0.0f, (36.0f - ImGui::GetFontSize()) * 0.5f)));
            const bool open = ImGui::BeginCombo("##AudioPlaybackRate",
                                                 selected_rate->label,
                                                 ImGuiComboFlags_HeightSmall);
            ImGui::PopStyleVar();
            if (!open)
            {
                ImGui::PopStyleColor();
                return;
            }

            for (const RateOption &rate : rates)
            {
                const bool selected = std::abs(state.playback_rate - rate.value) < 0.001f;
                if (ImGui::Selectable(rate.label, selected))
                    actions.playback_rate = rate.value;
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
            ImGui::PopStyleColor();
        }

        void DrawVolumeControl(const AudioPreviewState &state,
                               AudioPreviewActions &actions, const float width)
        {
            if (DrawControlButton("##AudioMute",
                                  state.is_muted ? "[ MUTE ]" : "[ SOUND ]",
                                  state.is_muted ? "Unmute" : "Mute",
                                  state.is_muted ? state.voice_close_icon :
                                                   state.voice_open_icon))
                actions.muted = !state.is_muted;
            ImGui::SameLine(0.0f, 8.0f);
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##AudioPreviewGain", ImVec2(width, 36.0f));
            const bool hovered = ImGui::IsItemHovered();
            const bool active = ImGui::IsItemActive();
            const float current_volume = std::clamp(state.volume, 0.0f, 1.0f);
            float adjusted_volume = current_volume;
            if (active && ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                adjusted_volume = std::clamp(
                    (ImGui::GetIO().MousePos.x - origin.x) / width, 0.0f, 1.0f);
            }
            else if (ImGui::IsItemFocused())
            {
                if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
                    adjusted_volume = std::max(0.0f, current_volume - 0.05f);
                if (ImGui::IsKeyPressed(ImGuiKey_RightArrow))
                    adjusted_volume = std::min(1.0f, current_volume + 0.05f);
            }
            if (adjusted_volume != current_volume)
                actions.volume = adjusted_volume;

            ImDrawList *draw = ImGui::GetWindowDrawList();
            const ImVec2 track_min{origin.x, origin.y + 14.0f};
            const ImVec2 track_max{origin.x + width, origin.y + 22.0f};
            draw->AddRectFilled(track_min, track_max, kBackground);
            const float fill_x = track_min.x + width * adjusted_volume;
            if (fill_x > track_min.x)
                draw->AddRectFilled(track_min, {fill_x, track_max.y}, kCyan);
            draw->AddRect(track_min, track_max, hovered ? kCyan : kBorder);

            char percent_text[8]{};
            std::snprintf(percent_text, sizeof(percent_text), "%d%%",
                          static_cast<int>(std::round(adjusted_volume * 100.0f)));
            const float percent_width = ImGui::CalcTextSize(percent_text).x;
            draw->AddText({origin.x + width + 10.0f,
                           origin.y + (36.0f - ImGui::GetFontSize()) * 0.5f},
                          kCyan, percent_text);
            if (hovered)
                ImGui::SetTooltip("Volume: %s  (click, drag, or use arrow keys)",
                                  percent_text);
            ImGui::SetCursorScreenPos(origin);
            ImGui::Dummy(ImVec2(width + 10.0f + percent_width, 36.0f));
        }

        void DrawStatus(ImDrawList &draw, const AudioPreviewState &state,
                        const ImVec2 min, const float width)
        {
            const char *label = state.is_error ? "ERROR" :
                state.is_playing ? "PLAYING" : state.has_clip ? "READY" : "NO CLIP";
            const ImU32 color = state.is_error ? kRed :
                state.is_playing ? kAmber : state.has_clip ? kGreen : kMuted;
            const ImVec2 text_size = ImGui::CalcTextSize(label);
            draw.AddText({min.x + width - text_size.x - 12.0f, min.y + 10.0f}, color, label);
        }
    }

    AudioPreviewActions DrawAudioPreview(const AudioPreviewState &state)
    {
        AudioPreviewActions actions{};
        const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
        const bool narrow = width < 620.0f;
        const float height = std::max(narrow ? 335.0f : 310.0f, state.height);
        ImGui::PushID("AudioPreview");
        if (state.font != nullptr)
            ImGui::PushFont(state.font);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(kBackground));
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(kBackground));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::ColorConvertU32ToFloat4(kCyanDim));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::ColorConvertU32ToFloat4(kBorder));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::ColorConvertU32ToFloat4(kBackground));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImGui::ColorConvertU32ToFloat4(kCyan));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
        if (ImGui::BeginChild("AudioPreviewSurface", ImVec2(width, height), false,
                              ImGuiWindowFlags_NoScrollbar))
        {
            const ImVec2 panel_min = ImGui::GetWindowPos();
            const ImVec2 panel_max{panel_min.x + width, panel_min.y + height};
            ImDrawList *draw = ImGui::GetWindowDrawList();
            draw->AddRect(panel_min, panel_max, kBorder);
            DrawCorners(*draw, panel_min, panel_max);
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kCyan), "// AUDIO PREVIEW");
            DrawStatus(*draw, state, panel_min, width);
            ImGui::Dummy(ImVec2(0.0f, 4.0f));

            const float content_width = ImGui::GetContentRegionAvail().x;
            const float gap = 14.0f;
            const ImVec2 visual_top = ImGui::GetCursorScreenPos();
            const float visual_height = std::max(135.0f,
                panel_max.y - visual_top.y - (narrow ? 125.0f : 100.0f));
            const float scope_width = std::max(88.0f, std::min(
                content_width * 0.29f, visual_height * 0.98f));
            const ImVec2 scope_origin{
                visual_top.x,
                visual_top.y + (visual_height - scope_width) * 0.5f};
            DrawScopeFrame(*draw, scope_origin, scope_width);
            DrawRadialAudioScope(*draw,
                                 {scope_origin.x + scope_width * 0.5f,
                                  scope_origin.y + scope_width * 0.5f},
                                 scope_width * 0.46f, state);
            ImGui::SetCursorScreenPos(scope_origin);
            ImGui::Dummy(ImVec2(scope_width, scope_width));
            ImGui::SameLine(0.0f, gap);
            const float waveform_height = std::min(
                std::clamp(visual_height * 0.58f, 68.0f, 350.0f),
                visual_height - 67.0f);
            const float details_height = waveform_height + 67.0f;
            ImGui::SetCursorScreenPos({scope_origin.x + scope_width + gap,
                visual_top.y + (visual_height - details_height) * 0.5f});
            ImGui::BeginGroup();
            const float waveform_width = std::max(100.0f, content_width - scope_width - gap);
            const std::string_view name = state.has_clip ? state.clip_name : "NO CLIP LOADED";
            const ImVec2 title_origin = ImGui::GetCursorScreenPos();
            const float title_font_size = ImGui::GetFontSize() * 1.7f;
            const ImVec4 title_clip{title_origin.x, title_origin.y,
                                    title_origin.x + waveform_width,
                                    title_origin.y + title_font_size + 2.0f};
            draw->AddText(ImGui::GetFont(), title_font_size, title_origin, kCyan,
                          name.data(), name.data() + name.size(), 0.0f, &title_clip);
            ImGui::Dummy(ImVec2(0.0f, title_font_size + 2.0f));
            const ImVec2 format_origin = ImGui::GetCursorScreenPos();
            const float format_font_size = ImGui::GetFontSize() * 1.18f;
            draw->AddText(ImGui::GetFont(), format_font_size, format_origin,
                          kMuted, state.format_label.data(),
                          state.format_label.data() + state.format_label.size());
            ImGui::Dummy(ImVec2(0.0f, format_font_size));
            ImGui::Dummy(ImVec2(0.0f, 3.0f));
            DrawWaveform(*draw, ImGui::GetCursorScreenPos(),
                         ImVec2(waveform_width, waveform_height), state, actions);
            ImGui::EndGroup();
            constexpr float progress_padding_x = 12.0f;
            ImGui::SetCursorScreenPos({visual_top.x + progress_padding_x,
                                       visual_top.y + visual_height + 8.0f});
            DrawProgressBar(*draw, content_width - progress_padding_x * 2.0f,
                            state, actions);
            ImGui::Dummy(ImVec2(0.0f, 8.0f));
            ImGui::Separator();
            DrawPlaybackControls(state, actions);
            if (!narrow)
            {
                ImGui::SameLine(0.0f, 12.0f);
            }
            DrawPlaybackRateControl(state, actions);
            ImGui::SameLine(0.0f, 12.0f);
            constexpr float volume_icon_button_width = 60.0f;
            constexpr float volume_icon_gap = 8.0f;
            constexpr float volume_percent_gap = 10.0f;
            const float volume_percent_width = ImGui::CalcTextSize("100%").x;
            const float volume_fixed_width = volume_icon_button_width + volume_icon_gap +
                volume_percent_gap + volume_percent_width;
            const float volume_bar_width = std::max(
                48.0f, ImGui::GetContentRegionAvail().x - volume_fixed_width);
            DrawVolumeControl(state, actions, volume_bar_width);
            if (!state.status.empty() && state.is_error)
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kRed), "%.*s",
                                   static_cast<int>(state.status.size()), state.status.data());
        }
        ImGui::EndChild();
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(6);
        if (state.font != nullptr)
            ImGui::PopFont();
        ImGui::PopID();
        return actions;
    }
}
