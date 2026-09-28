#include "editor/ui/component/editor_loading_component.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

#include <imgui.h>

#include "core/config/path.h"
#include "image_io/image_io.h"

namespace kpengine::editor
{
    namespace
    {
        constexpr ImU32 kBackground = IM_COL32(7, 11, 18, 255);
        constexpr ImU32 kPanel = IM_COL32(12, 20, 31, 245);
        constexpr ImU32 kAccent = IM_COL32(64, 219, 232, 255);
        constexpr ImU32 kAccentDim = IM_COL32(32, 118, 139, 255);
        constexpr ImU32 kText = IM_COL32(224, 237, 242, 255);
        constexpr ImU32 kMuted = IM_COL32(122, 151, 166, 255);
        constexpr ImU32 kDisplayGold = IM_COL32(255, 190, 73, 255);

        std::array<uint8_t, 7> DigitalGlyphRows(const char character) noexcept
        {
            switch (character)
            {
            case 'A': return {0b01110, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001};
            case 'E': return {0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b11111};
            case 'G': return {0b01111, 0b10000, 0b10000, 0b10111, 0b10001, 0b10001, 0b01111};
            case 'I': return {0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b11111};
            case 'K': return {0b10001, 0b10010, 0b10100, 0b11000, 0b10100, 0b10010, 0b10001};
            case 'M': return {0b10001, 0b11011, 0b10101, 0b10101, 0b10001, 0b10001, 0b10001};
            case 'N': return {0b10001, 0b11001, 0b10101, 0b10011, 0b10001, 0b10001, 0b10001};
            case 'P': return {0b11110, 0b10001, 0b10001, 0b11110, 0b10000, 0b10000, 0b10000};
            case 'T': return {0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100};
            case 'U': return {0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110};
            default: return {};
            }
        }

        void AddText(ImDrawList *const draw_list, const ImVec2 &position,
                     const ImU32 color, const char *const text)
        {
            draw_list->AddText(position, color, text);
        }

        void AddText(ImDrawList *const draw_list, const ImVec2 &position,
                     const ImU32 color, const std::string &text)
        {
            draw_list->AddText(position, color, text.c_str());
        }

        void DrawCornerMarks(ImDrawList *const draw_list, const ImVec2 &min,
                             const ImVec2 &max)
        {
            constexpr float length = 20.0f;
            constexpr float thickness = 2.0f;
            const ImU32 color = IM_COL32(64, 219, 232, 190);
            draw_list->AddLine(min, ImVec2(min.x + length, min.y), color, thickness);
            draw_list->AddLine(min, ImVec2(min.x, min.y + length), color, thickness);
            draw_list->AddLine(ImVec2(max.x - length, min.y), ImVec2(max.x, min.y), color,
                               thickness);
            draw_list->AddLine(ImVec2(max.x, min.y), ImVec2(max.x, min.y + length), color,
                               thickness);
            draw_list->AddLine(ImVec2(min.x, max.y - length), ImVec2(min.x, max.y), color,
                               thickness);
            draw_list->AddLine(ImVec2(min.x, max.y), ImVec2(min.x + length, max.y), color,
                               thickness);
            draw_list->AddLine(ImVec2(max.x - length, max.y), max, color, thickness);
            draw_list->AddLine(ImVec2(max.x, max.y - length), max, color, thickness);
        }

        const char *StatusLabel(const EditorLoadingViewModel &model, const bool compact)
        {
            if (model.failed)
            {
                return "FAULT";
            }
            if (model.closing)
            {
                return compact ? "STOP" : "SHUTDOWN";
            }
            if (model.ready)
            {
                return "ONLINE";
            }
            return compact ? "BOOT" : "BOOT SEQUENCE";
        }
    }

    EditorLoadingComponent::EditorLoadingComponent(
        std::function<runtime::StartupSnapshot()> snapshot_source)
        : snapshot_source_(std::move(snapshot_source))
    {
        LoadIconPixels();
    }

    void EditorLoadingComponent::LoadIconPixels()
    {
        const std::filesystem::path icon_path =
            project_root / "resouce" / "icon" / "icon-kimpeanut.png";
        const image_io::ImageDecodeResult decoded =
            image_io::DecodeImageFile(icon_path.generic_string());
        if (!decoded.result.success || !decoded.image.IsValid() ||
            decoded.image.format != image_io::ImagePixelFormat::Rgba8)
        {
            return;
        }

        const image_io::ImageBuffer &image = decoded.image;
        if (image.width == 0U || image.height == 0U)
        {
            return;
        }

        icon_pixels_.assign(kIconSize * kIconSize, 0U);
        for (uint32_t y = 0; y < kIconSize; ++y)
        {
            const uint32_t source_y_from_top =
                ((2U * y + 1U) * image.height) / (2U * kIconSize);
            const uint32_t source_y = image.height - 1U - source_y_from_top;
            for (uint32_t x = 0; x < kIconSize; ++x)
            {
                const uint32_t source_x = ((2U * x + 1U) * image.width) / (2U * kIconSize);
                const std::size_t offset =
                    (static_cast<std::size_t>(source_y) * image.width + source_x) * 4U;
                const uint8_t red = image.pixels[offset];
                const uint8_t green = image.pixels[offset + 1U];
                const uint8_t blue = image.pixels[offset + 2U];
                const uint8_t alpha = image.pixels[offset + 3U];
                if (alpha < 16U || (red < 22U && green < 22U && blue < 22U))
                {
                    continue;
                }
                icon_pixels_[static_cast<std::size_t>(y) * kIconSize + x] =
                    IM_COL32(red, green, blue, alpha);
            }
        }
    }

    void EditorLoadingComponent::Render()
    {
        const runtime::StartupSnapshot snapshot =
            snapshot_source_ ? snapshot_source_() : runtime::StartupSnapshot{};
        last_view_model_ = BuildEditorLoadingViewModel(snapshot);

        const ImGuiViewport *const viewport = ImGui::GetMainViewport();
        const ImVec2 work_pos = viewport->WorkPos;
        const ImVec2 work_size = viewport->WorkSize;
        ImGui::SetNextWindowPos(work_pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(work_size, ImGuiCond_Always);
        constexpr ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration |
                                                   ImGuiWindowFlags_NoMove |
                                                   ImGuiWindowFlags_NoSavedSettings |
                                                   ImGuiWindowFlags_NoBringToFrontOnFocus |
                                                   ImGuiWindowFlags_NoBackground |
                                                   ImGuiWindowFlags_NoNav;
        ImGui::Begin("##StartupLoading", nullptr, window_flags);
        ImDrawList *const draw_list = ImGui::GetWindowDrawList();
        const ImVec2 screen_min = ImGui::GetWindowPos();
        const ImVec2 screen_size = ImGui::GetWindowSize();
        const ImVec2 screen_max(screen_min.x + screen_size.x,
                                screen_min.y + screen_size.y);
        draw_list->AddRectFilled(screen_min, screen_max, kBackground);

        const float grid_offset = std::fmod(static_cast<float>(ImGui::GetTime()) * 4.0f, 44.0f);
        for (float x = screen_min.x + grid_offset; x < screen_max.x; x += 44.0f)
        {
            draw_list->AddLine(ImVec2(x, screen_min.y), ImVec2(x, screen_max.y),
                               IM_COL32(70, 135, 160, 17));
        }
        for (float y = screen_min.y; y < screen_max.y; y += 44.0f)
        {
            draw_list->AddLine(ImVec2(screen_min.x, y), ImVec2(screen_max.x, y),
                               IM_COL32(70, 135, 160, 17));
        }

        const float card_width = std::min(780.0f, std::max(320.0f, work_size.x - 48.0f));
        const float card_height = std::min(478.0f, std::max(390.0f, work_size.y - 48.0f));
        const ImVec2 card_min(work_pos.x + (work_size.x - card_width) * 0.5f,
                              work_pos.y + (work_size.y - card_height) * 0.5f);
        const ImVec2 card_max(card_min.x + card_width, card_min.y + card_height);
        draw_list->AddRectFilled(card_min, card_max, kPanel, 12.0f);
        draw_list->AddRect(card_min, card_max, IM_COL32(53, 113, 137, 155), 12.0f,
                           ImDrawFlags_RoundCornersAll, 1.0f);
        DrawCornerMarks(draw_list, card_min, card_max);

        const float pad = std::min(34.0f, card_width * 0.07f);
        const float content_left = card_min.x + pad;
        const float content_right = card_max.x - pad;
        const float content_width = content_right - content_left;
        const float top = card_min.y + 27.0f;
        const bool compact = content_width < 420.0f;
        AddText(draw_list, ImVec2(content_left, top), kAccent,
                compact ? "KPE  /  RUNTIME" : "KIMPEANUT ENGINE  /  RUNTIME");
        const char *const status = StatusLabel(last_view_model_, compact);
        const ImVec2 status_size = ImGui::CalcTextSize(status);
        const ImVec2 status_position(content_right - status_size.x, top);
        const ImU32 status_color = last_view_model_.failed
                                       ? IM_COL32(255, 101, 116, 255)
                                       : last_view_model_.ready
                                             ? IM_COL32(120, 238, 185, 255)
                                             : kAccent;
        draw_list->AddCircleFilled(ImVec2(status_position.x - 13.0f,
                                          status_position.y + 6.0f),
                                   3.0f, status_color);
        AddText(draw_list, status_position, status_color, status);
        draw_list->AddLine(ImVec2(content_left, top + 22.0f),
                           ImVec2(content_right, top + 22.0f),
                           IM_COL32(64, 111, 133, 100));

        const float brand_width = std::min(content_width, 510.0f);
        constexpr float icon_extent = 88.0f;
        const ImVec2 brand_min(content_left, top + 39.0f);
        const ImVec2 icon_min(brand_min.x, brand_min.y + 7.0f);
        if (!icon_pixels_.empty())
        {
            for (uint32_t y = 0; y < kIconSize; ++y)
            {
                for (uint32_t x = 0; x < kIconSize; ++x)
                {
                    const ImU32 pixel = icon_pixels_[static_cast<std::size_t>(y) * kIconSize + x];
                    if ((pixel & 0xFF000000U) == 0U)
                    {
                        continue;
                    }
                    const ImVec2 pixel_min(
                        icon_min.x + icon_extent * static_cast<float>(x) / kIconSize,
                        icon_min.y + icon_extent * static_cast<float>(y) / kIconSize);
                    const ImVec2 pixel_max(
                        icon_min.x + icon_extent * static_cast<float>(x + 1U) / kIconSize + 0.25f,
                        icon_min.y + icon_extent * static_cast<float>(y + 1U) / kIconSize + 0.25f);
                    draw_list->AddRectFilled(pixel_min, pixel_max, pixel);
                }
            }
        }
        else
        {
            draw_list->AddCircle(ImVec2(icon_min.x + icon_extent * 0.5f,
                                        icon_min.y + icon_extent * 0.5f),
                                 icon_extent * 0.42f, kDisplayGold, 64, 2.0f);
        }

        const ImVec2 display_min(brand_min.x + 103.0f, brand_min.y + 5.0f);
        const ImVec2 display_max(brand_min.x + brand_width, brand_min.y + 99.0f);
        draw_list->AddRectFilled(display_min, display_max, IM_COL32(8, 15, 22, 255), 5.0f);
        draw_list->AddRect(display_min, display_max, IM_COL32(157, 108, 45, 185), 5.0f,
                           ImDrawFlags_RoundCornersAll, 1.0f);
        draw_list->AddLine(ImVec2(display_min.x + 1.0f, display_min.y + 1.0f),
                           ImVec2(display_max.x - 1.0f, display_min.y + 1.0f),
                           IM_COL32(255, 190, 73, 115), 1.0f);

        constexpr std::array<std::string_view, 2> brand_lines{"KIMPEANUT", "ENGINE"};
        const float display_width = display_max.x - display_min.x;
        const float pitch = std::min(5.0f, (display_width - 20.0f) / 61.0f);
        const float text_height = pitch * 15.0f;
        const float text_top = display_min.y + ((display_max.y - display_min.y) - text_height) * 0.5f;
        for (std::size_t line_index = 0; line_index < brand_lines.size(); ++line_index)
        {
            const std::string_view line = brand_lines[line_index];
            const float line_width = (line.size() * 5.0f + (line.size() - 1U) * 1.5f) * pitch;
            const float line_left = display_min.x + (display_width - line_width) * 0.5f;
            const float line_top = text_top + static_cast<float>(line_index) * pitch * 8.0f;
            for (std::size_t character_index = 0; character_index < line.size(); ++character_index)
            {
                const std::array<uint8_t, 7> rows = DigitalGlyphRows(line[character_index]);
                const float character_left = line_left +
                    static_cast<float>(character_index) * pitch * 6.5f;
                for (std::size_t row = 0; row < rows.size(); ++row)
                {
                    for (uint32_t column = 0; column < 5U; ++column)
                    {
                        const ImVec2 dot_center(
                            character_left + (static_cast<float>(column) + 0.5f) * pitch,
                            line_top + (static_cast<float>(row) + 0.5f) * pitch);
                        const bool lit = (rows[row] & (1U << (4U - column))) != 0U;
                        const float radius = pitch * (lit ? 0.34f : 0.17f);
                        const ImU32 color = lit ? kDisplayGold : IM_COL32(108, 78, 38, 100);
                        if (lit)
                        {
                            draw_list->AddCircleFilled(dot_center, radius + pitch * 0.18f,
                                                       IM_COL32(255, 174, 50, 45), 8);
                        }
                        draw_list->AddCircleFilled(dot_center, radius, color, 8);
                    }
                }
            }
        }

        const float details_top = brand_min.y + 114.0f;
        AddText(draw_list, ImVec2(content_left, details_top), kMuted, "INITIALIZING WORKSPACE");
        AddText(draw_list, ImVec2(content_left, details_top + 23.0f), kText,
                last_view_model_.stage_label);
        const float line_y = details_top + 52.0f;
        draw_list->AddLine(ImVec2(content_left, line_y), ImVec2(content_right, line_y),
                           IM_COL32(64, 111, 133, 75));

        const float progress_y = line_y + 20.0f;
        AddText(draw_list, ImVec2(content_left, progress_y), kMuted,
                last_view_model_.closing ? "SHUTDOWN PROGRESS" : "STARTUP PROGRESS");
        char percent_text[16]{};
        if (last_view_model_.determinate)
        {
            std::snprintf(percent_text, sizeof(percent_text), "%02.0f%%",
                          last_view_model_.fraction * 100.0f);
        }
        else
        {
            const int dots = static_cast<int>(ImGui::GetTime() * 2.0) % 4;
            std::snprintf(percent_text, sizeof(percent_text), "SYNC%.*s", dots, "...");
        }
        const ImVec2 percent_size = ImGui::CalcTextSize(percent_text);
        AddText(draw_list, ImVec2(content_right - percent_size.x, progress_y), kAccent,
                percent_text);

        const float bar_y = progress_y + 20.0f;
        constexpr int segment_count = 48;
        const float gap = 3.0f;
        const float segment_width = (content_width - gap * (segment_count - 1)) / segment_count;
        const float fill_fraction = last_view_model_.determinate
                                        ? last_view_model_.fraction
                                        : std::fmod(static_cast<float>(ImGui::GetTime()) * 0.18f, 0.72f);
        const int filled_count = static_cast<int>(fill_fraction * segment_count);
        const float pulse_x = content_left +
                              std::fmod(static_cast<float>(ImGui::GetTime()) * 135.0f,
                                        std::max(1.0f, content_width));
        for (int index = 0; index < segment_count; ++index)
        {
            const float x = content_left + index * (segment_width + gap);
            const ImVec2 segment_min(x, bar_y);
            const ImVec2 segment_max(x + segment_width, bar_y + 8.0f);
            ImU32 color = IM_COL32(22, 42, 54, 255);
            if (index < filled_count)
            {
                color = kAccent;
            }
            else if (!last_view_model_.determinate && std::abs(x - pulse_x) < 28.0f)
            {
                color = kAccentDim;
            }
            draw_list->AddRectFilled(segment_min, segment_max, color, 2.0f);
        }

        const float telemetry_y = bar_y + 23.0f;
        AddText(draw_list, ImVec2(content_left, telemetry_y), kMuted,
                last_view_model_.counts_label);
        const std::string phase_text = last_view_model_.current_item.empty()
                                           ? "SYSTEM CHECKS RUNNING"
                                           : last_view_model_.current_item;
        draw_list->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                           ImVec2(content_left, telemetry_y + 20.0f), kMuted,
                           phase_text.c_str(), nullptr, content_width);

        if (last_view_model_.failed && !last_view_model_.diagnostic.empty())
        {
            const float diagnostic_y = card_max.y - 47.0f;
            draw_list->AddRectFilled(ImVec2(content_left, diagnostic_y - 7.0f),
                                     ImVec2(content_right, card_max.y - 20.0f),
                                     IM_COL32(91, 27, 38, 170), 4.0f);
            draw_list->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                               ImVec2(content_left + 9.0f, diagnostic_y),
                               IM_COL32(255, 165, 174, 255),
                               last_view_model_.diagnostic.c_str(), nullptr,
                               content_right - 9.0f);
        }

        const float footer_y = screen_max.y - 31.0f;
        AddText(draw_list, ImVec2(screen_min.x + 28.0f, footer_y), kMuted,
                "KPE  //  GRAPHICS INITIALIZATION");
        const char *const footer_right = "REALTIME STARTUP MONITOR";
        const ImVec2 footer_size = ImGui::CalcTextSize(footer_right);
        AddText(draw_list, ImVec2(screen_max.x - footer_size.x - 28.0f, footer_y), kMuted,
                footer_right);
        ImGui::End();
    }
}
