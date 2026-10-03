#include "audio_player_editor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <functional>
#include <iterator>
#include <numbers>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include <imgui.h>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <commdlg.h>
#include <shlobj.h>
#endif

#include "audio_player_controller.h"
#include "audio_player_theme.h"
#include "audio_preview_widget.h"
#include "editor/log/editor_log_component.h"
#include "editor/profile/editor_builtin_metrics.h"
#include "editor/profile/editor_metric.h"
#include "editor/profile/editor_profile_bar.h"
#include "editor/profile/editor_profile_metrics.h"
#include "editor/settings/editor_settings.h"
#include "editor/ui/component/editor_audio_component.h"
#include "editor/ui/component/editor_window_component.h"
#include "editor/ui/component/editor_tool_row_component.h"
#include "editor/ui/editor_ui.h"
#include "runtime/audio/miniaudio_audio_system.h"
#include "runtime/core/config/path.h"
#include "runtime/engine.h"
#include "runtime/image_io/image_io.h"
#include "runtime/runtime_global_context.h"
#include "runtime/platform/memory_stats_sampler.h"
#include "runtime/render/render_system.h"
#include "runtime/window/window_system.h"

namespace kpengine::audio_player
{
    namespace
    {
        const ImVec4 kCyan = AudioPlayerTheme::Vec(AudioPlayerTheme::cyan);
        const ImVec4 kAmber = AudioPlayerTheme::Vec(AudioPlayerTheme::amber);
        const ImVec4 kMuted = AudioPlayerTheme::Vec(AudioPlayerTheme::muted);
        const ImVec4 kText = AudioPlayerTheme::Vec(AudioPlayerTheme::text);
        const ImVec4 kDanger = AudioPlayerTheme::Vec(AudioPlayerTheme::error);
        constexpr std::uint32_t kTransportIconSize = 24;

        std::vector<std::uint8_t> LoadTransportIconMask(const std::string_view filename)
        {
            const std::filesystem::path path = project_root / "resouce" / "icon" /
                "audio" / std::string{filename};
            const image_io::ImageDecodeResult decoded =
                image_io::DecodeImageFile(path.generic_string());
            if (!decoded.result.success || !decoded.image.IsValid() ||
                decoded.image.format != image_io::ImagePixelFormat::Rgba8)
                return {};

            std::uint32_t min_x = decoded.image.width;
            std::uint32_t min_y = decoded.image.height;
            std::uint32_t max_x = 0;
            std::uint32_t max_y = 0;
            bool has_opaque_pixel = false;
            for (std::uint32_t y = 0; y < decoded.image.height; ++y)
            {
                for (std::uint32_t x = 0; x < decoded.image.width; ++x)
                {
                    const std::size_t pixel =
                        (static_cast<std::size_t>(y) * decoded.image.width + x) * 4 + 3;
                    if (decoded.image.pixels[pixel] < 12)
                        continue;
                    min_x = std::min(min_x, x);
                    min_y = std::min(min_y, y);
                    max_x = std::max(max_x, x);
                    max_y = std::max(max_y, y);
                    has_opaque_pixel = true;
                }
            }
            if (!has_opaque_pixel)
                return {};

            const std::uint32_t source_width = max_x - min_x + 1;
            const std::uint32_t source_height = max_y - min_y + 1;
            constexpr float padding = 1.0f;
            const float scale = std::min(
                (kTransportIconSize - padding * 2.0f) / static_cast<float>(source_width),
                (kTransportIconSize - padding * 2.0f) / static_cast<float>(source_height));
            const auto target_width = std::max(1u, static_cast<std::uint32_t>(
                std::round(source_width * scale)));
            const auto target_height = std::max(1u, static_cast<std::uint32_t>(
                std::round(source_height * scale)));
            const std::uint32_t target_x = (kTransportIconSize - target_width) / 2;
            const std::uint32_t target_y = (kTransportIconSize - target_height) / 2;
            std::vector<std::uint8_t> alpha(kTransportIconSize * kTransportIconSize);
            for (std::uint32_t y = 0; y < target_height; ++y)
            {
                const std::uint32_t source_y = max_y - std::min(
                    source_height - 1, y * source_height / target_height);
                for (std::uint32_t x = 0; x < target_width; ++x)
                {
                    const std::uint32_t source_x = min_x + std::min(
                        source_width - 1, x * source_width / target_width);
                    const std::size_t source_pixel =
                        (static_cast<std::size_t>(source_y) * decoded.image.width +
                         source_x) * 4 + 3;
                    alpha[static_cast<std::size_t>(target_y + y) * kTransportIconSize +
                          target_x + x] = decoded.image.pixels[source_pixel];
                }
            }
            return alpha;
        }

        std::array<std::vector<std::uint8_t>, 8> LoadTransportIcons()
        {
            return {LoadTransportIconMask("skip-previous.png"),
                    LoadTransportIconMask("play.png"),
                    LoadTransportIconMask("pause.png"),
                    LoadTransportIconMask("skipnext.png"),
                    LoadTransportIconMask("stop.png"),
                    LoadTransportIconMask("voice_open.png"),
                    LoadTransportIconMask("voice_close.png"),
                    LoadTransportIconMask("loop.png")};
        }

        void DrawPlaylistIcon(const std::span<const std::uint8_t> alpha,
                              const ImVec2 origin, const float size, const ImU32 tint)
        {
            if (alpha.size() != kTransportIconSize * kTransportIconSize)
                return;
            const float pixel_size = size / static_cast<float>(kTransportIconSize);
            ImDrawList *const draw = ImGui::GetWindowDrawList();
            for (std::uint32_t y = 0; y < kTransportIconSize; ++y)
            {
                const std::size_t row_offset = static_cast<std::size_t>(y) * kTransportIconSize;
                for (std::uint32_t x = 0; x < kTransportIconSize;)
                {
                    const std::uint8_t opacity = alpha[row_offset + x];
                    const std::uint32_t run_start = x++;
                    while (x < kTransportIconSize && alpha[row_offset + x] == opacity)
                        ++x;
                    if (opacity == 0)
                        continue;
                    const ImU32 color = (tint & ~IM_COL32_A_MASK) |
                        (((opacity * (tint >> IM_COL32_A_SHIFT)) / 255u) << IM_COL32_A_SHIFT);
                    draw->AddRectFilled(
                        {origin.x + static_cast<float>(run_start) * pixel_size,
                         origin.y + static_cast<float>(y) * pixel_size},
                        {origin.x + static_cast<float>(x) * pixel_size,
                         origin.y + static_cast<float>(y + 1) * pixel_size}, color);
                }
            }
        }

        enum class CollectionIcon { Folder, Recent, Favorite, Music };

        bool DrawCollectionRow(const char *id, const char *label, const char *badge,
                               const bool selected, const CollectionIcon icon,
                               const std::span<const std::uint8_t> music_mask = {},
                               const float height_em = AudioPlayerTheme::collection_row_height_em)
        {
            const float font_size = ImGui::GetFontSize();
            const float padding = font_size * AudioPlayerTheme::collection_padding_em;
            ImGui::PushStyleColor(ImGuiCol_Header, AudioPlayerTheme::selected);
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, selected ?
                AudioPlayerTheme::selected_hovered : AudioPlayerTheme::raised);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                                ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));
            const bool activated = ImGui::Selectable(id, selected, 0,
                                                     ImVec2(0.0f, font_size * height_em));
            const ImVec2 row_min = ImGui::GetItemRectMin();
            const ImVec2 row_max = ImGui::GetItemRectMax();
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);

            ImDrawList *const draw = ImGui::GetWindowDrawList();
            const ImU32 accent = selected ? AudioPlayerTheme::amber : AudioPlayerTheme::cyan;
            const float center_y = (row_min.y + row_max.y) * 0.5f;
            const float icon_size = font_size * AudioPlayerTheme::collection_icon_em;
            const ImVec2 origin(row_min.x + padding, center_y - icon_size * 0.5f);
            const ImVec2 center(origin.x + icon_size * 0.5f, center_y);
            if (icon == CollectionIcon::Music)
            {
                DrawPlaylistIcon(music_mask, origin, icon_size, accent);
            }
            else if (icon == CollectionIcon::Folder)
            {
                const float top = center_y - icon_size * 0.375f;
                const std::array<ImVec2, 6> folder{{
                    {origin.x, top}, {origin.x + icon_size * 0.35f, top},
                    {origin.x + icon_size * 0.5f, top + icon_size * 0.1875f},
                    {origin.x + icon_size, top + icon_size * 0.1875f},
                    {origin.x + icon_size, top + icon_size * 0.75f},
                    {origin.x, top + icon_size * 0.75f}
                }};
                draw->AddPolyline(folder.data(), static_cast<int>(folder.size()),
                                  accent, ImDrawFlags_Closed, 1.0f);
            }
            else if (icon == CollectionIcon::Recent)
            {
                draw->AddCircle(center, icon_size * 0.5f, accent, 20);
                draw->AddLine(center, {center.x, center.y - icon_size * 0.3f}, accent);
                draw->AddLine(center, {center.x + icon_size * 0.25f, center.y}, accent);
            }
            else
            {
                std::array<ImVec2, 10> star{};
                for (std::size_t point = 0; point < star.size(); ++point)
                {
                    const float angle = -std::numbers::pi_v<float> * 0.5f +
                        static_cast<float>(point) * std::numbers::pi_v<float> * 0.2f;
                    const float radius = icon_size * (point % 2 == 0 ? 0.5f : 0.225f);
                    star[point] = {center.x + std::cos(angle) * radius,
                                   center.y + std::sin(angle) * radius};
                }
                draw->AddPolyline(star.data(), static_cast<int>(star.size()),
                                  accent, ImDrawFlags_Closed, 1.0f);
            }
            const float badge_x = row_max.x - padding - ImGui::CalcTextSize(badge).x;
            const float text_y = center_y - ImGui::GetTextLineHeight() * 0.5f;
            const ImVec4 label_clip(row_min.x, row_min.y,
                                   std::max(row_min.x, badge_x - padding), row_max.y);
            draw->AddText(ImGui::GetFont(), font_size,
                {origin.x + icon_size + padding, text_y}, accent, label, nullptr, 0.0f, &label_clip);
            draw->AddText({badge_x, text_y},
                          selected ? AudioPlayerTheme::amber : AudioPlayerTheme::text, badge);
            if (selected)
                draw->AddRect({row_min.x + 1.0f, row_min.y + 1.0f},
                              {row_max.x - 1.0f, row_max.y - 1.0f}, AudioPlayerTheme::amber);
            return activated;
        }

        class AudioPlayerDockPanel final : public editor::EditorWindowComponent
        {
        public:
            AudioPlayerDockPanel(std::string title, std::function<void()> render_content,
                                 std::function<ImFont *()> content_font)
                : EditorWindowComponent(title),
                  render_content_(std::move(render_content)),
                  content_font_(std::move(content_font))
            {
            }

            void RenderContent() override
            {
                const ScopedAudioPlayerTheme theme{};
                ImFont *const font = content_font_ ? content_font_() : nullptr;
                if (font != nullptr)
                    ImGui::PushFont(font);
                if (render_content_)
                {
                    render_content_();
                }
                if (font != nullptr)
                    ImGui::PopFont();
            }

        private:
            std::function<void()> render_content_;
            std::function<ImFont *()> content_font_;
        };

        std::string LowerAscii(std::string value);
        editor::TransportPlaybackState ToTransportPlaybackState(
            audio::AudioState state, bool has_clip, bool has_error);

        std::string FormatTime(const float seconds)
        {
            const auto whole_seconds = static_cast<unsigned int>(
                std::max(0.0f, seconds));
            const unsigned int hours = whole_seconds / 3600;
            const unsigned int minutes = (whole_seconds / 60) % 60;
            const unsigned int remainder = whole_seconds % 60;
            char text[24]{};
            if (hours > 0)
            {
                std::snprintf(text, sizeof(text), "%u:%02u:%02u", hours, minutes, remainder);
            }
            else
            {
                std::snprintf(text, sizeof(text), "%02u:%02u", minutes, remainder);
            }
            return text;
        }

        std::string FormatBytes(const std::uint64_t bytes)
        {
            char text[48]{};
            if (bytes >= 1024ull * 1024ull)
            {
                std::snprintf(text, sizeof(text), "%.1f MiB",
                              static_cast<double>(bytes) / (1024.0 * 1024.0));
            }
            else if (bytes >= 1024ull)
            {
                std::snprintf(text, sizeof(text), "%.0f KiB",
                              static_cast<double>(bytes) / 1024.0);
            }
            else
            {
                std::snprintf(text, sizeof(text), "%llu B",
                              static_cast<unsigned long long>(bytes));
            }
            return text;
        }

        void SectionTitle(const char *title)
        {
            ImGui::TextColored(kCyan, "// %s", title);
            ImGui::SameLine();
            const float remaining = ImGui::GetContentRegionAvail().x;
            if (remaining > 12.0f)
            {
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetTextLineHeight() * 0.5f);
                ImGui::PushStyleColor(ImGuiCol_Separator,
                                      AudioPlayerTheme::Vec(AudioPlayerTheme::border));
                ImGui::Separator();
                ImGui::PopStyleColor();
            }
        }

        void TextField(const char *label, const char *value)
        {
            ImGui::TextColored(kMuted, "%s", label);
            ImGui::SameLine(std::min(116.0f, ImGui::GetWindowWidth() * 0.38f));
            ImGui::TextWrapped("%s", value == nullptr ? "—" : value);
        }

        void TextTitle(const ImVec4 color, const char *text)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::SetWindowFontScale(1.55f);
            ImGui::TextWrapped("%s", text);
            ImGui::SetWindowFontScale(1.0f);
            ImGui::PopStyleColor();
        }

#ifdef _WIN32
        std::string ToUtf8(const std::wstring_view wide)
        {
            if (wide.empty())
            {
                return {};
            }
            const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
            if (bytes <= 0)
            {
                return {};
            }
            std::string result(static_cast<std::size_t>(bytes), '\0');
            WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                                result.data(), bytes, nullptr, nullptr);
            return result;
        }

        std::vector<std::string> PickAudioFiles(const bool allow_multiple = true)
        {
            std::vector<wchar_t> buffer(65536, L'\0');
            OPENFILENAMEW dialog{};
            dialog.lStructSize = sizeof(dialog);
            dialog.lpstrFilter = L"Audio files\0*.wav;*.mp3;*.flac\0All files\0*.*\0\0";
            dialog.lpstrFile = buffer.data();
            dialog.nMaxFile = static_cast<DWORD>(buffer.size());
            dialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
                           OFN_HIDEREADONLY;
            if (allow_multiple)
                dialog.Flags |= OFN_ALLOWMULTISELECT;
            if (!GetOpenFileNameW(&dialog))
            {
                return {};
            }

            std::vector<std::string> paths;
            const std::wstring first = buffer.data();
            const wchar_t *cursor = buffer.data() + first.size() + 1;
            if (*cursor == L'\0')
            {
                paths.push_back(ToUtf8(first));
                return paths;
            }
            const std::wstring folder = first;
            while (*cursor != L'\0')
            {
                const std::wstring file = cursor;
                paths.push_back(ToUtf8(folder + L"\\" + file));
                cursor += file.size() + 1;
            }
            return paths;
        }

        std::string PickSubtitleFile()
        {
            std::array<wchar_t, 32768> path{};
            OPENFILENAMEW dialog{};
            dialog.lStructSize = sizeof(dialog);
            dialog.lpstrFilter = L"Subtitle files\0*.srt;*.vtt;*.lrc\0All files\0*.*\0\0";
            dialog.lpstrFile = path.data();
            dialog.nMaxFile = static_cast<DWORD>(path.size());
            dialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
                           OFN_HIDEREADONLY;
            return GetOpenFileNameW(&dialog) ? ToUtf8(path.data()) : std::string{};
        }

        std::string PickAudioFolder()
        {
            BROWSEINFOW dialog{};
            dialog.lpszTitle = L"Add audio files from a folder";
            dialog.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
            PIDLIST_ABSOLUTE selection = SHBrowseForFolderW(&dialog);
            if (selection == nullptr)
            {
                return {};
            }
            std::array<wchar_t, 32768> folder{};
            const bool resolved = SHGetPathFromIDListW(selection, folder.data()) != FALSE;
            CoTaskMemFree(selection);
            return resolved ? ToUtf8(folder.data()) : std::string{};
        }
#else
        std::vector<std::string> PickAudioFiles(bool = true) { return {}; }
        std::string PickSubtitleFile() { return {}; }
        std::string PickAudioFolder() { return {}; }
#endif

        void SubmitFile(AudioPlayerController &controller, const std::string &path,
                        const std::string &subtitle_path,
                        std::string &status, std::string &error)
        {
            std::string diagnostic;
            if (controller.ImportFile(path, subtitle_path, diagnostic))
            {
                status = "Import queued";
                error.clear();
            }
            else
            {
                status = "Import rejected";
                error = std::move(diagnostic);
            }
        }

        void RenderQueue(AudioPlayerController &controller, char *search,
                         const std::size_t search_capacity, const int library_filter,
                         const std::string &status, const std::string &error,
                         const std::vector<std::uint8_t> &music_icon,
                         const std::vector<std::uint8_t> &play_icon)
        {
            std::vector<TrackView> queue = controller.GetQueue();
            const PlaybackView playback = controller.GetPlaybackView();
            std::vector<const TrackView *> visible;
            visible.reserve(queue.size());
            const std::string query = LowerAscii(search);
            for (const auto &track : queue)
            {
                if (library_filter == 2 && !track.favorite)
                {
                    continue;
                }
                if (!query.empty() && LowerAscii(track.name + " " + track.path).find(query) ==
                                          std::string::npos)
                {
                    continue;
                }
                visible.push_back(&track);
            }
            if (library_filter == 1)
            {
                std::reverse(visible.begin(), visible.end());
            }

            SectionTitle(library_filter == 1 ? "PLAYLIST / RECENT" :
                         library_filter == 2 ? "PLAYLIST / FAVORITES" :
                                               "PLAYLIST / ALL");
            ImGui::SetNextItemWidth(std::min(340.0f, ImGui::GetContentRegionAvail().x));
            ImGui::InputTextWithHint("##TrackSearch", "Search tracks…", search, search_capacity);
            ImGui::Spacing();
            if (visible.empty())
            {
                if (queue.empty())
                {
                    ImGui::Spacing();
                    ImGui::TextColored(kMuted, "The queue is empty.");
                    ImGui::TextWrapped("Add audio files or a folder. WAV, MP3, and FLAC are supported.");
                }
                else
                {
                    ImGui::TextColored(kMuted, "No tracks match this filter.");
                    if (ImGui::SmallButton("Clear search"))
                    {
                        search[0] = '\0';
                    }
                }
            }
            else
            {
                const ImGuiTableFlags flags = ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp |
                    ImGuiTableFlags_ScrollY;
                const bool wide = ImGui::GetContentRegionAvail().x >= 720.0f;
                const float table_height = std::max(
                    80.0f, ImGui::GetContentRegionAvail().y - 42.0f);
                const ImVec2 table_origin = ImGui::GetCursorScreenPos();
                const float cell_padding_y = ImGui::GetTextLineHeight() * 0.4f;
                const float row_height = ImGui::GetTextLineHeight() + cell_padding_y * 2.0f;
                ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,
                    ImVec2(ImGui::GetStyle().CellPadding.x, cell_padding_y));
                if (ImGui::BeginTable("AudioQueueTable", wide ? 6 : 4, flags,
                                      ImVec2(0.0f, table_height)))
                {
                    ImDrawList *const table_draw_list = ImGui::GetWindowDrawList();
                    ImVec2 body_clip_min = table_draw_list->GetClipRectMin();
                    const ImVec2 body_clip_max = table_draw_list->GetClipRectMax();
                    body_clip_min.y = std::max(body_clip_min.y,
                        table_origin.y + row_height);
                    ImVec2 selection_min{};
                    ImVec2 selection_max{};
                    bool selection_visible = false;
                    ImGui::TableSetupScrollFreeze(0, 1);
                    ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 74.0f);
                    ImGui::TableSetupColumn("TRACK", ImGuiTableColumnFlags_WidthStretch, 1.7f);
                    ImGui::TableSetupColumn("DURATION", ImGuiTableColumnFlags_WidthFixed, 86.0f);
                    ImGui::TableSetupColumn(wide ? "SAMPLE RATE" : "FORMAT",
                                            ImGuiTableColumnFlags_WidthFixed,
                                            wide ? 112.0f : 142.0f);
                    if (wide)
                    {
                        ImGui::TableSetupColumn("TYPE", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                        ImGui::TableSetupColumn("SOURCE", ImGuiTableColumnFlags_WidthFixed, 88.0f);
                    }
                    ImGui::TableHeadersRow();

                    ImGuiListClipper clipper;
                    clipper.Begin(static_cast<int>(visible.size()));
                    while (clipper.Step())
                    {
                        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
                        {
                            const TrackView &track = *visible[static_cast<std::size_t>(row)];
                            ImGui::PushID(static_cast<int>(track.id));
                            ImGui::TableNextRow(ImGuiTableRowFlags_None, row_height);
                            const bool selected = playback.track.has_value() &&
                                                  playback.track->id == track.id;
                            const bool playing = selected &&
                                playback.state == audio::AudioState::Playing;
                            if (selected)
                                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                                                       AudioPlayerTheme::selected);
                            ImGui::TableSetColumnIndex(0);
                            const float row_top = ImGui::GetCursorScreenPos().y - cell_padding_y;
                            ImGui::TextColored(selected ? kAmber : kText, "%03llu",
                                static_cast<unsigned long long>(row + 1));
                            ImGui::TableSetColumnIndex(1);
                            const ImVec2 icon_origin = ImGui::GetCursorScreenPos();
                            const float icon_size = ImGui::GetTextLineHeight();
                            ImGui::SetCursorScreenPos(
                                {icon_origin.x + icon_size + ImGui::GetStyle().ItemInnerSpacing.x,
                                 icon_origin.y});
                            ImGui::PushStyleColor(ImGuiCol_Text, selected ? kAmber : kCyan);
                            ImGui::PushStyleColor(ImGuiCol_Header, AudioPlayerTheme::selected);
                            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, selected ?
                                AudioPlayerTheme::selected_hovered : AudioPlayerTheme::raised);
                            if (ImGui::Selectable(track.name.c_str(), selected,
                                    ImGuiSelectableFlags_SpanAllColumns |
                                    ImGuiSelectableFlags_AllowDoubleClick))
                            {
                                controller.Select(track.id);
                                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                                {
                                    std::string diagnostic;
                                    controller.PlaySelected(diagnostic);
                                }
                            }
                            ImGui::PopStyleColor(3);
                            DrawPlaylistIcon(playing ? play_icon : music_icon,
                                icon_origin, icon_size,
                                selected ? AudioPlayerTheme::amber : AudioPlayerTheme::cyan);
                            if (selected)
                            {
                                selection_min = ImVec2(ImGui::GetItemRectMin().x + 1.0f, row_top);
                                selection_max = ImVec2(ImGui::GetItemRectMax().x - 1.0f,
                                                       row_top + row_height);
                                selection_visible = true;
                            }
                            if (ImGui::IsItemHovered())
                            {
                                ImGui::SetTooltip("%s", track.path.c_str());
                            }
                            ImGui::TableSetColumnIndex(2);
                            ImGui::TextColored(selected ? kAmber : kText, "%s",
                                               FormatTime(track.duration_seconds).c_str());
                            ImGui::TableSetColumnIndex(3);
                            if (wide)
                            {
                                ImGui::TextColored(selected ? kAmber : kText, "%.1f kHz",
                                                   track.sample_rate / 1000.0f);
                                ImGui::TableSetColumnIndex(4);
                                ImGui::TextColored(selected ? kAmber : kText, "%s",
                                                   track.extension.c_str());
                                ImGui::TableSetColumnIndex(5);
                                ImGui::TextColored(selected ? kAmber : kText, "%s",
                                                   track.project_asset ? "CONTENT" : "SESSION");
                            }
                            else
                            {
                                ImGui::TextColored(selected ? kAmber : kText, "%s / %.1fk",
                                                   track.extension.c_str(),
                                                   track.sample_rate / 1000.0f);
                            }
                            ImGui::PopID();
                        }
                    }
                    ImGui::EndTable();
                    if (selection_visible && body_clip_max.y > body_clip_min.y)
                    {
                        // Draw after table channels merge so every column shares one outline.
                        table_draw_list->PushClipRect(body_clip_min, body_clip_max, true);
                        table_draw_list->AddRect(selection_min, selection_max,
                                                AudioPlayerTheme::amber);
                        table_draw_list->PopClipRect();
                    }
                }
                ImGui::PopStyleVar();
            }

            if (ImGui::Button("REMOVE", ImVec2(80.0f, 0.0f)))
            {
                controller.RemoveSelected();
            }
            ImGui::SameLine();
            if (ImGui::Button("CLEAR QUEUE", ImVec2(110.0f, 0.0f)))
            {
                controller.ClearQueue();
            }
            ImGui::SameLine();
            ImGui::TextColored(error.empty() ? kMuted : kDanger, "%s",
                               error.empty() ? status.c_str() : error.c_str());
            if (ImGui::GetContentRegionAvail().x > 145.0f)
            {
                ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 145.0f);
                ImGui::TextColored(kCyan, "%llu tracks | %zu importing",
                    static_cast<unsigned long long>(playback.queue_size), playback.pending_imports);
            }
        }

        bool RenderLibrary(AudioPlayerController &controller, const std::vector<TrackView> &queue,
                           const ProjectLibraryView &project_library, int &filter,
                           char *project_search, const std::size_t project_search_capacity,
                           char *import_path, char *subtitle_path,
                           const std::size_t import_path_capacity,
                           std::string &status, std::string &error,
                           const std::vector<std::uint8_t> &music_icon,
                           const std::vector<std::uint8_t> &play_icon)
        {
            bool show_queue = false;
            const PlaybackView playback = controller.GetPlaybackView();
            const auto favorites = static_cast<int>(std::count_if(queue.begin(), queue.end(),
                [](const TrackView &track) { return track.favorite; }));
            SectionTitle("LIBRARY");
            const std::array<std::pair<const char *, int>, 3> categories{{
                {"All", 0}, {"Recent", 1}, {"Favorites", 2}}};
            for (const auto &[label, value] : categories)
            {
                const int count = value == 2 ? favorites : static_cast<int>(queue.size());
                const bool selected = filter == value;
                char count_text[32]{};
                std::snprintf(count_text, sizeof(count_text), "[%03d]", count);
                ImGui::PushID(value);
                if (DrawCollectionRow("##LibraryCategory", label, count_text, selected,
                    value == 0 ? CollectionIcon::Folder :
                    value == 1 ? CollectionIcon::Recent : CollectionIcon::Favorite))
                {
                    filter = value;
                    show_queue = true;
                }
                ImGui::PopID();
            }
            ImGui::Spacing();
            ImGui::Separator();
            SectionTitle("PROJECT MUSIC");
            ImGui::TextColored(kText, "%03llu tracks",
                static_cast<unsigned long long>(project_library.tracks.size()));
            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x -
                ImGui::CalcTextSize("REFRESH").x - ImGui::GetStyle().FramePadding.x * 2.0f);
            if (ImGui::SmallButton("REFRESH"))
            {
                std::string diagnostic;
                if (!controller.RefreshProjectLibrary(diagnostic))
                {
                    error = std::move(diagnostic);
                    status = "Project Library refresh rejected";
                }
            }
            if (ImGui::GetContentRegionAvail().x > 0.0f)
            {
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputTextWithHint("##ProjectMusicSearch", "Filter project music…",
                                         project_search, project_search_capacity);
            }
            const float project_list_height = std::clamp(
                ImGui::GetContentRegionAvail().y * 0.3f,
                ImGui::GetFontSize() * 4.5f, ImGui::GetFontSize() * 11.0f);
            if (ImGui::BeginChild("ProjectMusicEntries", ImVec2(0.0f, project_list_height),
                                  true))
            {
                if (project_library.refreshing)
                {
                    ImGui::TextColored(kMuted, "Scanning library…");
                }
                else if (project_library.tracks.empty())
                {
                    ImGui::TextWrapped("No project music found.");
                    ImGui::TextWrapped("Add local files below to start.");
                }
                else
                {
                    const std::string project_query = LowerAscii(project_search);
                    std::vector<const TrackView *> visible;
                    visible.reserve(project_library.tracks.size());
                    for (const TrackView &track : project_library.tracks)
                    {
                        if (!project_query.empty() &&
                            LowerAscii(track.name + " " + track.path).find(project_query) ==
                                std::string::npos)
                        {
                            continue;
                        }
                        visible.push_back(&track);
                    }
                    ImGuiListClipper clipper;
                    clipper.Begin(static_cast<int>(visible.size()));
                    while (clipper.Step())
                    {
                        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
                        {
                            const TrackView &track = *visible[static_cast<std::size_t>(row)];
                            const bool selected = playback.track && playback.track->project_asset &&
                                playback.track->content_id == track.content_id;
                            const bool playing = selected && playback.state == audio::AudioState::Playing;
                            ImGui::PushID(track.content_id.c_str());
                            if (DrawCollectionRow("##ProjectTrack", track.name.c_str(),
                                track.extension.c_str(), selected, CollectionIcon::Music,
                                playing ? play_icon : music_icon, AudioPlayerTheme::library_track_height_em))
                            {
                                std::string diagnostic;
                                if (!controller.QueueProjectTrack(track.content_id, diagnostic))
                                {
                                    error = std::move(diagnostic);
                                    status = "Project track was not queued";
                                }
                                else
                                {
                                    error.clear();
                                    status = "Project track queued";
                                    filter = 0;
                                    show_queue = true;
                                }
                            }
                            if (ImGui::IsItemHovered())
                                ImGui::SetTooltip("%s\n%s", track.name.c_str(), track.path.c_str());
                            ImGui::PopID();
                        }
                    }
                    if (visible.empty())
                    {
                        ImGui::TextWrapped("No tracks match this filter.");
                        if (ImGui::SmallButton("CLEAR FILTER"))
                            project_search[0] = '\0';
                    }
                }
            }
            ImGui::EndChild();
            if (!project_library.error.empty())
            {
                ImGui::PushStyleColor(ImGuiCol_Text, kDanger);
                ImGui::TextWrapped("%s", project_library.error.c_str());
                ImGui::PopStyleColor();
            }
            else
                ImGui::TextColored(kMuted, "%s", project_library.status.c_str());
            ImGui::Separator();
            SectionTitle("IMPORT");
            const float action_width = std::max(1.0f,
                (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f);
            if (ImGui::Button(subtitle_path[0] == '\0' ? "ADD FILES" : "ADD FILE",
                              ImVec2(action_width, 0.0f)))
            {
                for (const auto &path : PickAudioFiles(subtitle_path[0] == '\0'))
                {
                    SubmitFile(controller, path, subtitle_path, status, error);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("ADD FOLDER", ImVec2(action_width, 0.0f)))
            {
                const std::string folder = PickAudioFolder();
                if (!folder.empty())
                {
                    std::string diagnostic;
                    if (!controller.ImportFolder(folder, diagnostic))
                    {
                        error = std::move(diagnostic);
                        status = "Folder import rejected";
                    }
                    else
                    {
                        error.clear();
                        status = "Folder import queued";
                    }
                }
            }
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint("##ImportPath", "Audio file or folder path…",
                                     import_path, import_path_capacity);
            ImGui::BeginDisabled(import_path[0] == '\0');
            if (ImGui::Button("QUEUE FILE", ImVec2(action_width, 0.0f)))
            {
                SubmitFile(controller, import_path, subtitle_path, status, error);
            }
            ImGui::SameLine();
            if (ImGui::Button("QUEUE FOLDER", ImVec2(action_width, 0.0f)))
            {
                std::string diagnostic;
                if (!controller.ImportFolder(import_path, diagnostic))
                {
                    error = std::move(diagnostic);
                    status = "Folder import rejected";
                }
                else
                {
                    error.clear();
                    status = "Folder import queued";
                }
            }
            ImGui::EndDisabled();
            ImGui::Spacing();
            if (ImGui::CollapsingHeader(subtitle_path[0] == '\0' ?
                "SUBTITLE (OPTIONAL)###SubtitleOptions" : "SUBTITLE / ATTACHED###SubtitleOptions"))
            {
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputTextWithHint("##SubtitlePath", "SRT, WebVTT, or LRC path…",
                                         subtitle_path, import_path_capacity);
                if (ImGui::Button("BROWSE SUBTITLE", ImVec2(-1.0f, 0.0f)))
                {
                    const std::string selected = PickSubtitleFile();
                    if (!selected.empty())
                        std::snprintf(subtitle_path, import_path_capacity, "%s", selected.c_str());
                }
                ImGui::TextWrapped("Applies to the next file. Keep it beside the audio file.");
            }
            if (!error.empty() || !status.empty())
            {
                ImGui::PushStyleColor(ImGuiCol_Text, error.empty() ? kMuted : kDanger);
                ImGui::TextWrapped("%s", error.empty() ? status.c_str() : error.c_str());
                ImGui::PopStyleColor();
            }
            return show_queue;
        }

        bool RenderPlaylists(const std::vector<TrackView> &queue)
        {
            SectionTitle("PLAYLISTS");
            char count[32]{};
            std::snprintf(count, sizeof(count), "[%03llu]",
                          static_cast<unsigned long long>(queue.size()));
            const bool activated = DrawCollectionRow("##SessionQueue", "Current queue", count,
                                                      true, CollectionIcon::Folder);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Current queue — show all queued tracks.\n"
                                  "This collection lasts until the player closes.");

            ImGui::Spacing();
            ImGui::Spacing();
            if (queue.empty())
            {
                ImGui::TextColored(kText, "Your queue is empty.");
                ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
                ImGui::TextWrapped("Add files or choose a track in Library.");
                ImGui::PopStyleColor();
            }
            else
            {
                float total_seconds = 0.0f;
                for (const TrackView &track : queue)
                    total_seconds += track.duration_seconds;
                SectionTitle("COLLECTION");
                TextField("Duration", FormatTime(total_seconds).c_str());
            }
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextColored(kMuted, "SESSION ONLY");
            return activated;
        }

        void RenderAudioPreview(AudioPlayerController &controller,
                                const PlaybackView &playback, ImFont *font,
                                const std::array<std::vector<std::uint8_t>, 8> &icon_alpha)
        {
            char format[96]{};
            if (playback.track.has_value())
            {
                const TrackView &track = *playback.track;
                std::snprintf(format, sizeof(format), "%s // %u Hz // %s",
                              track.extension.c_str(), track.sample_rate,
                              track.channels == 1 ? "MONO" : "STEREO");
            }
            else
            {
                std::snprintf(format, sizeof(format), "NO SOURCE // SIGNAL IDLE");
            }
            AudioPreviewState state{};
            state.clip_name = playback.track ? std::string_view(playback.track->name) :
                                              std::string_view{};
            state.format_label = format;
            state.status = playback.error.empty() ?
                std::string_view(playback.status) : std::string_view(playback.error);
            state.subtitle_text = playback.subtitle_text;
            state.playback_state = ToTransportPlaybackState(
                playback.state, playback.track.has_value(), !playback.error.empty());
            state.current_time = playback.position_seconds;
            state.duration = playback.duration_seconds;
            state.volume = playback.muted ? 0.0f : playback.volume;
            state.playback_rate = playback.playback_rate;
            state.rms = playback.rms;
            state.peak = playback.peak;
            state.height = ImGui::GetContentRegionAvail().y;
            if (playback.track && playback.track->waveform)
                state.waveform_samples = *playback.track->waveform;
            state.previous_icon = {std::span<const std::uint8_t>{icon_alpha[0]},
                                   kTransportIconSize, kTransportIconSize};
            state.play_icon = {std::span<const std::uint8_t>{icon_alpha[1]},
                               kTransportIconSize, kTransportIconSize};
            state.pause_icon = {std::span<const std::uint8_t>{icon_alpha[2]},
                                kTransportIconSize, kTransportIconSize};
            state.next_icon = {std::span<const std::uint8_t>{icon_alpha[3]},
                               kTransportIconSize, kTransportIconSize};
            state.stop_icon = {std::span<const std::uint8_t>{icon_alpha[4]},
                               kTransportIconSize, kTransportIconSize};
            state.loop_icon = {std::span<const std::uint8_t>{icon_alpha[7]},
                               kTransportIconSize, kTransportIconSize};
            state.voice_open_icon = {std::span<const std::uint8_t>{icon_alpha[5]},
                                     kTransportIconSize, kTransportIconSize};
            state.voice_close_icon = {std::span<const std::uint8_t>{icon_alpha[6]},
                                      kTransportIconSize, kTransportIconSize};
            state.spectrum_bins = playback.spectrum;
            state.font = font;
            state.has_clip = playback.track.has_value();
            state.is_muted = playback.muted;
            state.is_error = !playback.error.empty();
            state.can_seek = playback.can_seek;
            state.has_subtitle_track = playback.subtitle_track_attached;
            state.loop_enabled = playback.loop_track;

            const AudioPreviewActions actions = DrawAudioPreview(state, "music-preview");
            std::string diagnostic;
            if (actions.previous) controller.Previous(true, diagnostic);
            if (actions.toggle_play_pause) controller.TogglePlayPause(diagnostic);
            if (actions.next) controller.Next(true, diagnostic);
            if (actions.stop_voice) controller.Stop();
            if (actions.toggle_loop) controller.SetLoopTrack(!playback.loop_track);
            if (actions.seek_seconds) controller.Seek(*actions.seek_seconds);
            if (actions.muted) controller.SetMuted(*actions.muted);
            if (actions.volume)
            {
                if (playback.muted && *actions.volume > 0.0f)
                    controller.SetMuted(false);
                controller.SetVolume(*actions.volume);
            }
            if (actions.playback_rate) controller.SetPlaybackRate(*actions.playback_rate);
        }

        void RenderInspector(AudioPlayerController &controller,
                             audio::MiniAudioSystem &audio_system,
                             const PlaybackView &playback, char *subtitle_path,
                             std::string &ui_status, std::string &ui_error,
                             bool &show_diagnostics)
        {
            SectionTitle("AUDIO DETAIL");
            if (playback.track.has_value())
            {
                const auto &track = *playback.track;
                ImGui::TextColored(kCyan, "// SOURCE");
                TextField("Name", track.name.c_str());
                TextField("Path", track.path.c_str());
                TextField("Origin", track.project_asset ? "Project Content" : "Session import");
                ImGui::Spacing();
                ImGui::TextColored(kCyan, "// FORMAT");
                TextField("Type", track.extension.c_str());
                TextField("Channels", track.channels == 1 ? "Mono" : "Stereo");
                char sample_rate[32]{};
                std::snprintf(sample_rate, sizeof(sample_rate), "%u Hz", track.sample_rate);
                TextField("Sample rate", sample_rate);
                TextField("Duration", FormatTime(track.duration_seconds).c_str());
                TextField("File size", FormatBytes(track.file_size).c_str());
                TextField("Runtime", track.native_product
                    ? "Native Audio; file-backed streaming" : "Buffered compatibility clip");
                if (track.has_subtitles)
                {
                    char subtitle_summary[96]{};
                    std::snprintf(subtitle_summary, sizeof(subtitle_summary), "%s (%u cues)",
                                  track.subtitle_language.c_str(), track.subtitle_cue_count);
                    TextField("Subtitles", subtitle_summary);
                }
                else
                {
                    TextField("Subtitles", "None attached");
                }
                ImGui::Spacing();
                ImGui::TextColored(kCyan, "// ACTIONS");
                if (ImGui::SmallButton(track.favorite ? "Remove favorite" : "Add favorite"))
                {
                    controller.ToggleFavorite(track.id);
                }
                ImGui::SameLine();
                if (track.project_asset)
                {
                    ImGui::TextColored(kMuted, "Reimport with AssetTool");
                }
                else
                {
                    if (ImGui::SmallButton("Reimport"))
                    {
                        std::string diagnostic;
                        if (!controller.ReimportSelected(subtitle_path, diagnostic))
                            ui_error = std::move(diagnostic);
                        else
                            ui_status = "Reimport queued";
                    }
                }
            }
            else
            {
                ImGui::TextColored(kMuted, "No track selected.");
                ImGui::TextWrapped("Choose a playlist row to inspect its source and format.");
            }

            ImGui::Spacing();
            ImGui::Separator();
            SectionTitle("OUTPUT");
            const audio::AudioDeviceInfo device = audio_system.GetDeviceInfo();
            if (device.initialized)
            {
                TextField("Device", device.name.empty() ? "Default output" : device.name.c_str());
                char format[48]{};
                std::snprintf(format, sizeof(format), "%u Hz / %u ch",
                              device.sample_rate, device.channels);
                TextField("Format", format);
                char latency[32]{};
                std::snprintf(latency, sizeof(latency), "%.1f ms", device.estimated_latency_ms);
                TextField("Latency", latency);
                if (ImGui::SmallButton("Stop output device"))
                {
                    audio_system.ShutDown();
                }
            }
            else
            {
                ImGui::TextColored(kAmber, "Output device offline");
                if (ImGui::SmallButton("Start / retry output"))
                {
                    audio_system.Initialize();
                }
            }
            ImGui::TextColored(kMuted, "Player output uses the Music bus.");
            if (ImGui::SmallButton("Mixer diagnostics"))
            {
                show_diagnostics = true;
            }
        }

        void RenderSpectrum(const PlaybackView &playback)
        {
            SectionTitle("SPECTRUM");
            ImGui::TextColored(kMuted, "// SOURCE SIGNAL / PLAYED CURSOR");

            const ImVec2 available = ImGui::GetContentRegionAvail();
            const ImVec2 size{std::max(180.0f, available.x),
                              std::clamp(available.y * 0.57f, 150.0f, 290.0f)};
            const ImVec2 min = ImGui::GetCursorScreenPos();
            const ImVec2 max{min.x + size.x, min.y + size.y};
            ImGui::Dummy(size);
            ImDrawList &draw = *ImGui::GetWindowDrawList();
            draw.AddRectFilled(min, max, AudioPlayerTheme::surface);
            draw.AddRect(min, max, AudioPlayerTheme::border);

            const float graph_left = min.x + 39.0f;
            const float graph_right = max.x - 10.0f;
            const float graph_top = min.y + 16.0f;
            const float graph_bottom = max.y - 27.0f;
            const float graph_height = std::max(1.0f, graph_bottom - graph_top);
            const ImU32 grid_label = AudioPlayerTheme::muted;
            for (int step = 0; step <= 3; ++step)
            {
                const float y = graph_top + graph_height * step / 3.0f;
                draw.AddLine({graph_left, y}, {graph_right, y}, AudioPlayerTheme::grid);
                char label[12]{};
                std::snprintf(label, sizeof(label), "%d", -20 * step);
                draw.AddText({min.x + 4.0f, y - 7.0f}, grid_label, label);
            }
            draw.AddLine({graph_left, graph_bottom}, {graph_right, graph_bottom},
                         AudioPlayerTheme::border);

            if (playback.track.has_value())
            {
                const std::size_t count = playback.spectrum.size();
                const float bin_width = (graph_right - graph_left) /
                                        static_cast<float>(count);
                const float bar_width = std::max(1.0f, bin_width - 2.0f);
                const auto loudest = std::max_element(playback.spectrum.begin(),
                                                       playback.spectrum.end());
                const std::size_t peak_bin = static_cast<std::size_t>(
                    std::distance(playback.spectrum.begin(), loudest));
                for (std::size_t i = 0; i < count; ++i)
                {
                    const float magnitude = std::clamp(playback.spectrum[i], 0.0f, 1.0f);
                    if (magnitude <= 0.001f)
                        continue;
                    const float relative_db = 20.0f * std::log10(magnitude);
                    const float height = graph_height *
                        std::clamp((relative_db + 60.0f) / 60.0f, 0.0f, 1.0f);
                    const float x = graph_left + bin_width * static_cast<float>(i) + 1.0f;
                    const ImU32 color = playback.state == audio::AudioState::Playing &&
                                        i == peak_bin ? AudioPlayerTheme::amber :
                                                        AudioPlayerTheme::cyan;
                    draw.AddRectFilled({x, graph_bottom - height},
                                       {x + bar_width, graph_bottom}, color);
                }
                constexpr std::array<std::size_t, 4> ticks{0, 16, 32, 47};
                for (const std::size_t index : ticks)
                {
                    const float x = graph_left + bin_width * static_cast<float>(index);
                    const float ratio = static_cast<float>(index) /
                        static_cast<float>(count - 1);
                    const float frequency = std::pow(120.0f, ratio) *
                        static_cast<float>(playback.track->sample_rate) / 256.0f;
                    char label[16]{};
                    if (frequency < 1000.0f)
                        std::snprintf(label, sizeof(label), "%.0f", frequency);
                    else
                        std::snprintf(label, sizeof(label), "%.1fk", frequency / 1000.0f);
                    draw.AddLine({x, graph_bottom}, {x, graph_bottom + 4.0f},
                                 AudioPlayerTheme::border);
                    draw.AddText({std::clamp(x - 8.0f, graph_left, graph_right - 32.0f),
                                  graph_bottom + 7.0f}, grid_label, label);
                }
            }
            else
            {
                draw.AddText({graph_left + 12.0f, graph_top + graph_height * 0.5f},
                             AudioPlayerTheme::muted, "NO SOURCE SIGNAL");
            }

            ImGui::TextColored(kCyan, "RMS %.3f  |  PEAK %.3f", playback.rms, playback.peak);
            ImGui::TextColored(kMuted, "Relative dB / 256-frame source FFT. Not device output.");
        }

        std::string LowerAscii(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char c) {
                return static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
            });
            return value;
        }

        editor::TransportPlaybackState ToTransportPlaybackState(
            const audio::AudioState state, const bool has_clip, const bool has_error)
        {
            if (has_error)
                return editor::TransportPlaybackState::Failed;
            switch (state)
            {
            case audio::AudioState::Playing: return editor::TransportPlaybackState::Playing;
            case audio::AudioState::Buffering: return editor::TransportPlaybackState::Buffering;
            case audio::AudioState::FadingOut: return editor::TransportPlaybackState::Draining;
            case audio::AudioState::Paused: return editor::TransportPlaybackState::Paused;
            case audio::AudioState::Stopped:
                return has_clip ? editor::TransportPlaybackState::Ready :
                                  editor::TransportPlaybackState::Idle;
            case audio::AudioState::Finished: return editor::TransportPlaybackState::Finished;
            case audio::AudioState::Cancelled: return editor::TransportPlaybackState::Cancelled;
            }
            return editor::TransportPlaybackState::Failed;
        }
    }

    AudioPlayerEditor::AudioPlayerEditor() = default;

    AudioPlayerEditor::~AudioPlayerEditor()
    {
        Shutdown();
    }

    bool AudioPlayerEditor::Initialize(runtime::Engine &engine,
                                       audio::MiniAudioSystem &audio_system,
                                       AudioPlayerController &controller,
                                       std::string &diagnostic)
    {
        diagnostic.clear();
        runtime::RuntimeContext &context = runtime::global_runtime_context;
        if (context.window_system_ == nullptr || context.render_system_ == nullptr ||
            context.log_system_ == nullptr)
        {
            diagnostic = "Audio Player presentation services are unavailable";
            return false;
        }
        controller_ = &controller;
        audio_system_ = &audio_system;

        editor::EditorUIInitInfo init_info{};
        init_info.window = context.window_system_->GetNativeHandle();
        init_info.editor_presentation_bridge =
            context.render_system_->GetEditorPresentationBridge();
        init_info.log_system = context.log_system_.get();
        init_info.engine = &engine;
        init_info.render_system = context.render_system_.get();
        init_info.window_system = context.window_system_.get();

        auto profile_metrics = editor::CreateEditorProfileMetrics(
            &engine, context.memory_sampler_.get(), context.render_system_.get());

        try
        {
            playback_icon_alpha_ = LoadTransportIcons();
            music_icon_alpha_ = LoadTransportIconMask("music.png");
            audio_panel_ = std::make_unique<editor::EditorAudioComponent>(&audio_system);
            if (!profile_metrics.empty())
            {
                profile_bar_ = std::make_unique<editor::EditorProfileBarComponent>(
                    std::move(profile_metrics));
            }
            ui_ = std::make_unique<editor::EditorUI>();
            layout_.ResetToThreeColumnTwoRowDefault();
            dock_model_.Clear();
            dock_host_ = std::make_unique<editor::EditorToolRowComponent>(
                dock_model_, editor::EditorWindowConfig{});
            dock_host_->SetLayoutModel(&layout_);
            const auto content_font = [this]() -> ImFont * {
                return ui_ != nullptr ? ui_->GetCodeFont() : nullptr;
            };
            dock_host_->AddPanel(
                "audio_library", "Library",
                std::make_unique<AudioPlayerDockPanel>("Library", [this] {
                    if (RenderLibrary(*controller_, controller_->GetQueue(),
                                 controller_->GetProjectLibrary(), library_filter_,
                                 project_search_, sizeof(project_search_),
                                 import_path_, subtitle_path_, sizeof(import_path_),
                                 ui_status_, ui_error_, music_icon_alpha_, playback_icon_alpha_[1]))
                    {
                        search_[0] = '\0';
                        dock_model_.ShowById("audio_queue");
                    }
                }, content_font), true, editor::EditorLayoutSlot::WorldOutliner);
            dock_host_->AddPanel(
                "audio_playlists", "Playlists",
                std::make_unique<AudioPlayerDockPanel>("Playlists", [this] {
                    if (RenderPlaylists(controller_->GetQueue()))
                    {
                        library_filter_ = 0;
                        search_[0] = '\0';
                        dock_model_.ShowById("audio_queue");
                    }
                }, content_font), true, editor::EditorLayoutSlot::ActorInspector);
            dock_host_->AddPanel(
                "audio_now_playing", "Now Playing",
                std::make_unique<AudioPlayerDockPanel>("Now Playing", [this] {
                    RenderAudioPreview(*controller_, controller_->GetPlaybackView(),
                                       ui_->GetCodeFont(), playback_icon_alpha_);
                }, content_font), true, editor::EditorLayoutSlot::Viewport);
            dock_host_->AddPanel(
                "audio_queue", "Playlist : All",
                std::make_unique<AudioPlayerDockPanel>("Playlist : All", [this] {
                    RenderQueue(*controller_, search_, sizeof(search_), library_filter_,
                                ui_status_, ui_error_, music_icon_alpha_, playback_icon_alpha_[1]);
                }, content_font), true, editor::EditorLayoutSlot::ToolRow);
            const auto log_panel = std::make_shared<editor::EditorLogComponent>(
                context.log_system_.get(), editor::DefaultLogColors(),
                editor::EditorWindowConfig{});
            dock_host_->AddPanel(
                "audio_log", "Output Log",
                std::make_unique<AudioPlayerDockPanel>("Output Log", [log_panel] {
                    log_panel->RenderContent();
                }, content_font), true, editor::EditorLayoutSlot::ToolRow);
            dock_host_->AddPanel(
                "audio_info", "Info",
                std::make_unique<AudioPlayerDockPanel>("Info", [this] {
                    RenderInspector(*controller_, *audio_system_,
                                    controller_->GetPlaybackView(), subtitle_path_,
                                    ui_status_, ui_error_, show_diagnostics_);
                }, content_font), true, editor::EditorLayoutSlot::CameraSettings);
            dock_host_->AddPanel(
                "audio_spectrum", "Spectrum",
                std::make_unique<AudioPlayerDockPanel>("Spectrum", [this] {
                    RenderSpectrum(controller_->GetPlaybackView());
                }, content_font),
                true, editor::EditorLayoutSlot::DebugViewer);
            ui_->InitializeViewer(init_info, [this] { RenderDockedWorkspace(); });
        }
        catch (const std::exception &exception)
        {
            diagnostic = std::string("Audio Player UI initialization failed: ") + exception.what();
            Shutdown();
            return false;
        }
        return true;
    }

    bool AudioPlayerEditor::Render(std::string &diagnostic)
    {
        diagnostic.clear();
        if (ui_ == nullptr || !ui_->Render())
        {
            diagnostic = "Audio Player ImGui presentation failed";
            return false;
        }
        return true;
    }

    void AudioPlayerEditor::Shutdown() noexcept
    {
        if (ui_ != nullptr)
        {
            ui_->Close();
            ui_.reset();
        }
        dock_host_.reset();
        dock_model_.Clear();
        audio_panel_.reset();
        profile_bar_.reset();
        controller_ = nullptr;
        audio_system_ = nullptr;
    }

    void AudioPlayerEditor::RenderDockedWorkspace()
    {
        if (controller_ == nullptr || audio_panel_ == nullptr || dock_host_ == nullptr)
        {
            return;
        }
        const PlaybackView playback = controller_->GetPlaybackView();
        if (ui_status_.empty() || playback.pending_imports > 0 || !playback.status.empty())
        {
            ui_status_ = playback.pending_imports > 0 ? "Importing audio…" : playback.status;
        }
        if (!playback.error.empty())
        {
            ui_error_ = playback.error;
        }
        ApplyLayout();
        dock_host_->Render();
        splitter_handles_.Render(layout_);
        if (profile_bar_ != nullptr)
            profile_bar_->Render();

        if (show_diagnostics_ && audio_panel_ != nullptr)
        {
            ImGui::SetNextWindowSize(ImVec2(470.0f, 620.0f), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Audio Mixer Diagnostics", &show_diagnostics_))
            {
                audio_panel_->RenderContent();
            }
            ImGui::End();
        }
    }

    void AudioPlayerEditor::ApplyLayout()
    {
        const ImGuiViewport *const viewport = ImGui::GetMainViewport();
        layout_.SetFixedExtentPixels(
            editor::EditorSplitterId::StatusBar,
            editor::EditorProfileBarComponent::MeasurePreferredHeightPx());
        layout_.Resolve({viewport->WorkPos.x, viewport->WorkPos.y,
                         viewport->WorkSize.x, viewport->WorkSize.y});
        if (profile_bar_ != nullptr)
        {
            profile_bar_->ApplyLayout(
                layout_.RectOf(editor::EditorLayoutSlot::ProfileBar));
        }
    }
}
