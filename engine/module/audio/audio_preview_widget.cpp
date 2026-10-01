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

        ImVec2 PointOnCircle(const ImVec2 center, const float angle, const float radius)
        {
            return {center.x + std::cos(angle) * radius,
                    center.y + std::sin(angle) * radius};
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

        void DrawRadialAudioScope(ImDrawList &draw, const ImVec2 center,
                                  const float radius, const AudioPreviewState &state)
        {
            const float rms = std::clamp(state.rms, 0.0f, 1.0f);
            const float peak = std::clamp(state.peak, 0.0f, 1.0f);
            draw.AddCircle(center, radius, kCyanDim, 64, 1.0f);
            draw.AddCircle(center, radius * 0.72f, kCyanDim, 64, 1.0f);
            draw.AddCircle(center, radius * (0.39f + rms * 0.07f), kCyan, 64, 1.6f);
            const float progress = state.has_clip && state.duration > 0.0f
                ? std::clamp(state.current_time / state.duration, 0.0f, 1.0f) : 0.0f;
            if (progress > 0.0f)
            {
                std::array<ImVec2, 65> points{};
                const int count = std::clamp(
                    static_cast<int>(std::ceil(progress * 64.0f)) + 1, 2, 65);
                for (int i = 0; i < count; ++i)
                {
                    const float angle = -kPi * 0.5f + progress * 2.0f * kPi *
                        static_cast<float>(i) / static_cast<float>(count - 1);
                    points[static_cast<std::size_t>(i)] =
                        PointOnCircle(center, angle, radius * 1.08f);
                }
                draw.AddPolyline(points.data(), count, kAmber, 0, 1.5f);
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
            for (std::size_t i = 0; i < count; ++i)
            {
                const float magnitude = std::clamp(state.spectrum_bins[i], 0.0f, 1.0f);
                const float angle = -kPi * 0.5f + static_cast<float>(i) * 2.0f * kPi /
                    static_cast<float>(count);
                const float inner = radius * 0.53f;
                const float outer = inner + radius * (0.06f + magnitude * 0.34f);
                draw.AddLine(PointOnCircle(center, angle, inner),
                             PointOnCircle(center, angle, outer),
                             peak > 0.65f && magnitude > 0.72f ? kAmber : kCyan, 1.8f);
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
            draw.AddRect(origin, end, kBorder, 0.0f, 0, 1.0f);
            draw.AddLine({origin.x + 1.0f, origin.y + size.y * 0.5f},
                         {end.x - 1.0f, origin.y + size.y * 0.5f}, kCyanDim, 1.0f);
            if (!state.has_clip || state.waveform_samples.empty())
            {
                draw.AddText({origin.x + 9.0f, origin.y + size.y * 0.5f - 7.0f},
                             kMuted, "LOAD CLIP FOR WAVEFORM");
                return;
            }
            const float progress = state.duration > 0.0f
                ? std::clamp(state.current_time / state.duration, 0.0f, 1.0f) : 0.0f;
            const std::size_t count = state.waveform_samples.size();
            const float step = size.x / static_cast<float>(count);
            draw.PushClipRect(origin, end, true);
            for (std::size_t i = 0; i < count; ++i)
            {
                const float x = origin.x + (static_cast<float>(i) + 0.5f) * step;
                const float amplitude = std::clamp(state.waveform_samples[i], 0.0f, 1.0f);
                const float half = std::max(1.0f, amplitude * size.y * 0.42f);
                draw.AddLine({x, origin.y + size.y * 0.5f - half},
                             {x, origin.y + size.y * 0.5f + half},
                             static_cast<float>(i) / static_cast<float>(count) <= progress
                                 ? kCyan : kCyanDim,
                             std::max(1.0f, step * 0.62f));
            }
            if (state.duration > 0.0f)
            {
                const float playhead = origin.x + progress * size.x;
                draw.AddLine({playhead, origin.y + 2.0f},
                             {playhead, end.y - 2.0f}, kAmber, 1.5f);
            }
            draw.PopClipRect();
            if (state.can_seek && ImGui::IsItemActive() &&
                ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                const float fraction = std::clamp(
                    (ImGui::GetIO().MousePos.x - origin.x) / size.x, 0.0f, 1.0f);
                actions.seek_seconds = fraction * state.duration;
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
            ImGui::InvisibleButton("##AudioPreviewProgress", ImVec2(width, 12.0f));
            const ImVec2 min{origin.x, origin.y + 3.0f};
            const ImVec2 max{origin.x + width, origin.y + 9.0f};
            draw.AddRectFilled(min, max, kCyanDim);
            draw.AddRect(min, max, kBorder);
            const float progress = state.has_clip && state.duration > 0.0f
                ? std::clamp(state.current_time / state.duration, 0.0f, 1.0f) : 0.0f;
            if (progress > 0.0f)
            {
                draw.AddRectFilled(min, {min.x + width * progress, max.y}, kAmber);
            }
            if (state.can_seek && ImGui::IsItemActive() &&
                ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                const float fraction = std::clamp(
                    (ImGui::GetIO().MousePos.x - origin.x) / width, 0.0f, 1.0f);
                actions.seek_seconds = fraction * state.duration;
            }
        }

        void DrawPlaybackControls(const AudioPreviewState &state,
                                  AudioPreviewActions &actions)
        {
            ImGui::BeginDisabled(!state.has_clip);
            actions.previous = ImGui::Button("[ PREV ]");
            ImGui::SameLine();
            actions.toggle_play_pause = ImGui::Button(
                state.is_playing ? "[ PAUSE ]" : "[ PLAY ]");
            ImGui::SameLine();
            actions.next = ImGui::Button("[ NEXT ]");
            ImGui::SameLine();
            actions.stop = ImGui::Button("[ STOP ]");
            ImGui::EndDisabled();
        }

        void DrawVolumeControl(const AudioPreviewState &state,
                               AudioPreviewActions &actions, const float width)
        {
            ImGui::TextUnformatted("GAIN");
            ImGui::SameLine();
            int percent = static_cast<int>(std::round(std::clamp(state.volume, 0.0f, 1.0f) * 100.0f));
            ImGui::SetNextItemWidth(width);
            if (ImGui::SliderInt("##AudioPreviewGain", &percent, 0, 100, "%d%%"))
                actions.volume = static_cast<float>(percent) / 100.0f;
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
                content_width * 0.22f, visual_height * 0.78f));
            const ImVec2 scope_origin{
                visual_top.x,
                visual_top.y + (visual_height - scope_width) * 0.5f};
            DrawRadialAudioScope(*draw,
                                 {scope_origin.x + scope_width * 0.5f,
                                  scope_origin.y + scope_width * 0.5f},
                                 scope_width * 0.43f, state);
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
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kCyan), "%.*s",
                               static_cast<int>(name.size()), name.data());
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kMuted), "%.*s",
                               static_cast<int>(state.format_label.size()), state.format_label.data());
            ImGui::Dummy(ImVec2(0.0f, 3.0f));
            DrawWaveform(*draw, ImGui::GetCursorScreenPos(),
                         ImVec2(waveform_width, waveform_height), state, actions);
            char time_text[48]{};
            std::snprintf(time_text, sizeof(time_text), "%04.1f / %04.1f",
                          std::max(0.0f, state.current_time),
                          std::max(0.0f, state.duration));
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kAmber), "%s", time_text);
            ImGui::EndGroup();
            ImGui::SetCursorScreenPos({visual_top.x, visual_top.y + visual_height + 4.0f});
            DrawProgressBar(*draw, content_width, state, actions);
            ImGui::Dummy(ImVec2(0.0f, 4.0f));
            ImGui::Separator();
            DrawPlaybackControls(state, actions);
            if (!narrow)
            {
                ImGui::SameLine(0.0f, 16.0f);
                ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(),
                    ImGui::GetWindowContentRegionMax().x - 215.0f));
            }
            DrawVolumeControl(state, actions, narrow ? 150.0f : 160.0f);
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
