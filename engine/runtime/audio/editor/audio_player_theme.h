#ifndef KPENGINE_RUNTIME_AUDIO_EDITOR_AUDIO_PLAYER_THEME_H
#define KPENGINE_RUNTIME_AUDIO_EDITOR_AUDIO_PLAYER_THEME_H

#include <array>
#include <utility>

#include <imgui.h>

namespace kpengine::audio_player
{
    struct AudioPlayerTheme final
    {
        static constexpr ImU32 surface = IM_COL32(2, 9, 14, 255);
        static constexpr ImU32 panel = IM_COL32(4, 17, 23, 255);
        static constexpr ImU32 raised = IM_COL32(7, 32, 42, 255);
        static constexpr ImU32 border = IM_COL32(16, 82, 101, 255);
        static constexpr ImU32 grid = IM_COL32(11, 48, 62, 255);
        static constexpr ImU32 cyan = IM_COL32(48, 211, 239, 255);
        static constexpr ImU32 cyan_dim = IM_COL32(20, 111, 137, 255);
        static constexpr ImU32 amber = IM_COL32(255, 184, 58, 255);
        static constexpr ImU32 muted = IM_COL32(133, 160, 169, 255);
        static constexpr ImU32 text = IM_COL32(220, 242, 248, 255);
        static constexpr ImU32 error = IM_COL32(245, 85, 78, 255);
        static constexpr ImU32 ready = IM_COL32(95, 205, 151, 255);
        static constexpr ImU32 selected = IM_COL32(51, 39, 17, 255);

        static ImVec4 Vec(const ImU32 color)
        {
            return ImGui::ColorConvertU32ToFloat4(color);
        }
    };

    class ScopedAudioPlayerTheme final
    {
    public:
        ScopedAudioPlayerTheme()
        {
            constexpr std::array<std::pair<ImGuiCol, ImU32>, 18> colors{{
                {ImGuiCol_Text, AudioPlayerTheme::text},
                {ImGuiCol_TextDisabled, AudioPlayerTheme::muted},
                {ImGuiCol_ChildBg, AudioPlayerTheme::surface},
                {ImGuiCol_Border, AudioPlayerTheme::border},
                {ImGuiCol_Separator, AudioPlayerTheme::border},
                {ImGuiCol_Header, AudioPlayerTheme::raised},
                {ImGuiCol_HeaderHovered, AudioPlayerTheme::border},
                {ImGuiCol_HeaderActive, AudioPlayerTheme::selected},
                {ImGuiCol_Button, AudioPlayerTheme::raised},
                {ImGuiCol_ButtonHovered, AudioPlayerTheme::border},
                {ImGuiCol_ButtonActive, AudioPlayerTheme::selected},
                {ImGuiCol_FrameBg, AudioPlayerTheme::panel},
                {ImGuiCol_FrameBgHovered, AudioPlayerTheme::raised},
                {ImGuiCol_FrameBgActive, AudioPlayerTheme::border},
                {ImGuiCol_SliderGrab, AudioPlayerTheme::cyan},
                {ImGuiCol_SliderGrabActive, AudioPlayerTheme::amber},
                {ImGuiCol_TableHeaderBg, AudioPlayerTheme::raised},
                {ImGuiCol_TableRowBgAlt, AudioPlayerTheme::panel},
            }};
            for (const auto &[slot, color] : colors)
                ImGui::PushStyleColor(slot, AudioPlayerTheme::Vec(color));
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
        }

        ~ScopedAudioPlayerTheme()
        {
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(18);
        }

        ScopedAudioPlayerTheme(const ScopedAudioPlayerTheme &) = delete;
        ScopedAudioPlayerTheme &operator=(const ScopedAudioPlayerTheme &) = delete;
    };
}

#endif
