#include "editor/ui/editor_theme.h"

#include <cstdlib>
#include <filesystem>

#include <imgui.h>

namespace kpengine::editor
{
    namespace
    {
        constexpr ImVec4 Color(unsigned char red, unsigned char green,
                                unsigned char blue, float alpha = 1.0f)
        {
            constexpr float kChannelScale = 1.0f / 255.0f;
            return ImVec4(static_cast<float>(red) * kChannelScale,
                          static_cast<float>(green) * kChannelScale,
                          static_cast<float>(blue) * kChannelScale, alpha);
        }

        constexpr ImVec4 WithAlpha(ImVec4 color, float alpha)
        {
            color.w = alpha;
            return color;
        }

        constexpr ImVec4 kSurface = Color(0x09, 0x0B, 0x14);
        constexpr ImVec4 kInk = Color(0xD7, 0xF9, 0xFF);
        constexpr ImVec4 kAccent = Color(0x00, 0xE5, 0xFF);
        constexpr ImVec4 kAdded = Color(0x39, 0xFF, 0x88);
        constexpr ImVec4 kRemoved = Color(0xFF, 0x4D, 0x8D);
        constexpr ImVec4 kSkill = Color(0xB9, 0x67, 0xFF);

        ImFont *LoadWindowsFont(const char *file_name, float size)
        {
#ifdef _WIN32
            std::filesystem::path font_directory("C:/Windows/Fonts");
            char *windows_directory = nullptr;
            size_t windows_directory_length = 0;
            if (_dupenv_s(&windows_directory, &windows_directory_length, "WINDIR") == 0 &&
                windows_directory && windows_directory_length > 1)
            {
                font_directory = std::filesystem::path(windows_directory) / "Fonts";
            }
            std::free(windows_directory);
            const std::filesystem::path font_path = font_directory / file_name;
            std::error_code error;
            if (std::filesystem::exists(font_path, error) && !error)
            {
                return ImGui::GetIO().Fonts->AddFontFromFileTTF(
                    font_path.string().c_str(), size);
            }
#else
            (void)file_name;
            (void)size;
#endif
            return nullptr;
        }
    }

    ImFont *ApplyCodexTheme()
    {
        ImGuiStyle &style = ImGui::GetStyle();
        ImVec4 *const colors = style.Colors;

        ImGuiIO &io = ImGui::GetIO();
        if (ImFont *const ui_font = LoadWindowsFont("segoeui.ttf", 15.0f))
        {
            io.FontDefault = ui_font;
        }
        ImFont *const code_font = LoadWindowsFont("CascadiaMono.ttf", 14.0f);

        // Surfaces stay close to the Codex #090b14 base while related layers
        // gain just enough separation for the editor's overlapping panels.
        colors[ImGuiCol_Text] = kInk;
        colors[ImGuiCol_TextDisabled] = WithAlpha(kInk, 0.52f);
        colors[ImGuiCol_WindowBg] = kSurface;
        colors[ImGuiCol_ChildBg] = Color(0x0D, 0x10, 0x1D);
        colors[ImGuiCol_PopupBg] = Color(0x10, 0x14, 0x24);
        colors[ImGuiCol_Border] = Color(0x25, 0x38, 0x4D);
        colors[ImGuiCol_BorderShadow] = WithAlpha(kSurface, 0.0f);

        colors[ImGuiCol_FrameBg] = Color(0x12, 0x1A, 0x2B);
        colors[ImGuiCol_FrameBgHovered] = Color(0x18, 0x2A, 0x40);
        colors[ImGuiCol_FrameBgActive] = Color(0x20, 0x3B, 0x52);
        colors[ImGuiCol_TitleBg] = Color(0x0C, 0x10, 0x1C);
        colors[ImGuiCol_TitleBgActive] = Color(0x11, 0x1B, 0x30);
        colors[ImGuiCol_TitleBgCollapsed] = Color(0x0B, 0x0E, 0x19);
        colors[ImGuiCol_MenuBarBg] = Color(0x0B, 0x0F, 0x1B);

        colors[ImGuiCol_ScrollbarBg] = Color(0x07, 0x09, 0x10);
        colors[ImGuiCol_ScrollbarGrab] = Color(0x27, 0x38, 0x53);
        colors[ImGuiCol_ScrollbarGrabHovered] = Color(0x35, 0x58, 0x70);
        colors[ImGuiCol_ScrollbarGrabActive] = kAccent;
        colors[ImGuiCol_CheckMark] = kAccent;
        colors[ImGuiCol_SliderGrab] = Color(0x00, 0xB8, 0xD4);
        colors[ImGuiCol_SliderGrabActive] = kAccent;

        colors[ImGuiCol_Button] = Color(0x14, 0x24, 0x38);
        colors[ImGuiCol_ButtonHovered] = Color(0x16, 0x40, 0x54);
        colors[ImGuiCol_ButtonActive] = Color(0x00, 0x6C, 0x7A);
        colors[ImGuiCol_Header] = Color(0x13, 0x24, 0x3C);
        colors[ImGuiCol_HeaderHovered] = Color(0x1B, 0x3F, 0x55);
        colors[ImGuiCol_HeaderActive] = Color(0x00, 0x5F, 0x6D);
        colors[ImGuiCol_Separator] = Color(0x26, 0x3C, 0x55);
        colors[ImGuiCol_SeparatorHovered] = WithAlpha(kAccent, 0.78f);
        colors[ImGuiCol_SeparatorActive] = kAccent;

        colors[ImGuiCol_ResizeGrip] = WithAlpha(kAccent, 0.28f);
        colors[ImGuiCol_ResizeGripHovered] = WithAlpha(kAccent, 0.67f);
        colors[ImGuiCol_ResizeGripActive] = kAccent;
        colors[ImGuiCol_Tab] = Color(0x10, 0x18, 0x2A);
        colors[ImGuiCol_TabHovered] = Color(0x1E, 0x43, 0x53);
        colors[ImGuiCol_TabSelected] = Color(0x13, 0x3A, 0x4E);
        colors[ImGuiCol_TabSelectedOverline] = kAccent;
        colors[ImGuiCol_TabDimmed] = Color(0x0D, 0x12, 0x20);
        colors[ImGuiCol_TabDimmedSelected] = Color(0x12, 0x2B, 0x3B);
        colors[ImGuiCol_TabDimmedSelectedOverline] = WithAlpha(kAccent, 0.52f);

        colors[ImGuiCol_PlotLines] = kAccent;
        colors[ImGuiCol_PlotLinesHovered] = kRemoved;
        colors[ImGuiCol_PlotHistogram] = kAdded;
        colors[ImGuiCol_PlotHistogramHovered] = kSkill;
        colors[ImGuiCol_TableHeaderBg] = Color(0x12, 0x20, 0x35);
        colors[ImGuiCol_TableBorderStrong] = Color(0x2B, 0x43, 0x5D);
        colors[ImGuiCol_TableBorderLight] = Color(0x1B, 0x2B, 0x40);
        colors[ImGuiCol_TableRowBg] = WithAlpha(kSurface, 0.0f);
        colors[ImGuiCol_TableRowBgAlt] = WithAlpha(Color(0x17, 0x23, 0x35), 0.40f);
        colors[ImGuiCol_TextSelectedBg] = WithAlpha(kAccent, 0.30f);
        colors[ImGuiCol_DragDropTarget] = kAccent;
        colors[ImGuiCol_NavHighlight] = kAccent;
        colors[ImGuiCol_NavWindowingHighlight] = kInk;
        colors[ImGuiCol_NavWindowingDimBg] = WithAlpha(kSurface, 0.32f);
        colors[ImGuiCol_ModalWindowDimBg] = WithAlpha(kSurface, 0.72f);

        style.Alpha = 1.0f;
        style.DisabledAlpha = 0.55f;
        style.WindowPadding = ImVec2(10.0f, 9.0f);
        style.FramePadding = ImVec2(8.0f, 5.0f);
        style.CellPadding = ImVec2(6.0f, 4.0f);
        style.ItemSpacing = ImVec2(8.0f, 6.0f);
        style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
        style.IndentSpacing = 18.0f;
        style.ScrollbarSize = 12.0f;
        style.GrabMinSize = 10.0f;
        style.WindowBorderSize = 1.0f;
        style.ChildBorderSize = 1.0f;
        style.PopupBorderSize = 1.0f;
        style.FrameBorderSize = 1.0f;
        style.TabBorderSize = 1.0f;
        style.WindowRounding = 6.0f;
        style.ChildRounding = 5.0f;
        style.FrameRounding = 4.0f;
        style.PopupRounding = 5.0f;
        style.ScrollbarRounding = 6.0f;
        style.GrabRounding = 4.0f;
        style.TabRounding = 4.0f;
        style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
        style.SeparatorTextBorderSize = 2.0f;
        style.SeparatorTextAlign = ImVec2(0.0f, 0.5f);

        return code_font;
    }
}
