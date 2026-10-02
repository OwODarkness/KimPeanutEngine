#include "audio_player_editor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <functional>
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
#include "audio_preview_widget.h"
#include "editor/ui/component/editor_audio_component.h"
#include "editor/ui/component/editor_window_component.h"
#include "editor/ui/component/editor_tool_row_component.h"
#include "editor/ui/editor_ui.h"
#include "runtime/audio/miniaudio_audio_system.h"
#include "runtime/core/config/path.h"
#include "runtime/engine.h"
#include "runtime/image_io/image_io.h"
#include "runtime/runtime_global_context.h"
#include "runtime/render/render_system.h"
#include "runtime/window/window_system.h"

namespace kpengine::audio_player
{
    namespace
    {
        constexpr ImVec4 kCyan{0.08f, 0.78f, 0.96f, 1.0f};
        constexpr ImVec4 kAmber{1.0f, 0.65f, 0.10f, 1.0f};
        constexpr ImVec4 kMuted{0.39f, 0.60f, 0.69f, 1.0f};
        constexpr ImVec4 kDanger{0.96f, 0.31f, 0.25f, 1.0f};
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

        std::array<std::vector<std::uint8_t>, 7> LoadTransportIcons()
        {
            return {LoadTransportIconMask("skip-previous.png"),
                    LoadTransportIconMask("play.png"),
                    LoadTransportIconMask("pause.png"),
                    LoadTransportIconMask("skipnext.png"),
                    LoadTransportIconMask("stop.png"),
                    LoadTransportIconMask("voice_open.png"),
                    LoadTransportIconMask("voice_close.png")};
        }

        class AudioPlayerDockPanel final : public editor::EditorWindowComponent
        {
        public:
            AudioPlayerDockPanel(std::string title, std::function<void()> render_content)
                : EditorWindowComponent(title),
                  render_content_(std::move(render_content))
            {
            }

            void RenderContent() override
            {
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.012f, 0.038f, 0.055f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.02f, 0.26f, 0.36f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.02f, 0.22f, 0.30f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.03f, 0.34f, 0.43f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.04f, 0.42f, 0.52f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.02f, 0.13f, 0.19f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.02f, 0.30f, 0.39f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.02f, 0.43f, 0.54f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.01f, 0.07f, 0.10f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.02f, 0.18f, 0.24f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.02f, 0.24f, 0.31f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_SliderGrab, kCyan);
                ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, kAmber);
                if (render_content_)
                {
                    render_content_();
                }
                ImGui::PopStyleColor(13);
            }

        private:
            std::function<void()> render_content_;
        };

        std::string LowerAscii(std::string value);

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
                ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.02f, 0.28f, 0.38f, 1.0f));
                ImGui::Separator();
                ImGui::PopStyleColor();
            }
        }

        void TextField(const char *label, const char *value)
        {
            ImGui::TextColored(kMuted, "%s", label);
            ImGui::SameLine(88.0f);
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
                         const std::string &status, const std::string &error)
        {
            std::vector<TrackView> queue = controller.GetQueue();
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

            ImGui::SetNextItemWidth(-1.0f);
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
                const float table_height = std::max(
                    80.0f, ImGui::GetContentRegionAvail().y - 42.0f);
                if (ImGui::BeginTable("AudioQueueTable", 4, flags,
                                      ImVec2(0.0f, table_height)))
                {
                    ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 46.0f);
                    ImGui::TableSetupColumn("TRACK", ImGuiTableColumnFlags_WidthStretch, 1.7f);
                    ImGui::TableSetupColumn("LENGTH", ImGuiTableColumnFlags_WidthFixed, 72.0f);
                    ImGui::TableSetupColumn("FORMAT", ImGuiTableColumnFlags_WidthFixed, 104.0f);
                    ImGui::TableHeadersRow();

                    ImGuiListClipper clipper;
                    clipper.Begin(static_cast<int>(visible.size()));
                    while (clipper.Step())
                    {
                        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
                        {
                            const TrackView &track = *visible[static_cast<std::size_t>(row)];
                            ImGui::PushID(static_cast<int>(track.id));
                            ImGui::TableNextRow(ImGuiTableRowFlags_None, 27.0f);
                            ImGui::TableSetColumnIndex(0);
                            ImGui::TextColored(kMuted, "%03llu",
                                static_cast<unsigned long long>(row + 1));
                            ImGui::TableSetColumnIndex(1);
                            const PlaybackView playback = controller.GetPlaybackView();
                            const bool selected = playback.track.has_value() &&
                                                  playback.track->id == track.id;
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
                            if (ImGui::IsItemHovered())
                            {
                                ImGui::SetTooltip("%s", track.path.c_str());
                            }
                            ImGui::TableSetColumnIndex(2);
                            ImGui::TextColored(kMuted, "%s", FormatTime(track.duration_seconds).c_str());
                            ImGui::TableSetColumnIndex(3);
                            ImGui::TextColored(kCyan, "%s  %u Hz", track.extension.c_str(),
                                               track.sample_rate);
                            ImGui::PopID();
                        }
                    }
                    ImGui::EndTable();
                }
            }

            if (ImGui::Button("REMOVE", ImVec2(80.0f, 0.0f)))
            {
                controller.RemoveSelected();
            }
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.28f, 0.07f, 0.06f, 1.0f));
            if (ImGui::Button("CLEAR QUEUE", ImVec2(110.0f, 0.0f)))
            {
                controller.ClearQueue();
            }
            ImGui::PopStyleColor();
            ImGui::SameLine();
            const PlaybackView playback = controller.GetPlaybackView();
            ImGui::TextColored(error.empty() ? kMuted : kDanger, "%s",
                               error.empty() ? status.c_str() : error.c_str());
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 130.0f);
            ImGui::TextColored(kCyan, "%llu tracks | %zu importing",
                static_cast<unsigned long long>(playback.queue_size), playback.pending_imports);
        }

        void RenderLibrary(AudioPlayerController &controller, const std::vector<TrackView> &queue,
                           int &filter, char *import_path, char *subtitle_path,
                           const std::size_t import_path_capacity,
                           std::string &status, std::string &error)
        {
            const auto favorites = static_cast<int>(std::count_if(queue.begin(), queue.end(),
                [](const TrackView &track) { return track.favorite; }));
            const std::array<std::pair<const char *, int>, 3> categories{{
                {"All", 0}, {"Recent", 1}, {"Favorites", 2}}};
            for (const auto &[label, value] : categories)
            {
                const int count = value == 2 ? favorites : static_cast<int>(queue.size());
                if (ImGui::Selectable(label, filter == value, 0, ImVec2(0.0f, 30.0f)))
                {
                    filter = value;
                }
                char count_text[16]{};
                std::snprintf(count_text, sizeof(count_text), "%03d", count);
                const float count_width = ImGui::CalcTextSize(count_text).x;
                ImGui::SameLine();
                ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(),
                    ImGui::GetWindowContentRegionMax().x - count_width - 8.0f));
                ImGui::TextColored(kMuted, "%s", count_text);
            }
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextColored(kCyan, "// OPTIONAL SUBTITLE");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint("##SubtitlePath", "Choose an SRT, WebVTT, or LRC file…",
                                     subtitle_path, import_path_capacity);
            const float subtitle_action_width = (ImGui::GetContentRegionAvail().x - 6.0f) * 0.5f;
            if (ImGui::Button("BROWSE SUBTITLE", ImVec2(subtitle_action_width, 0.0f)))
            {
                const std::string selected = PickSubtitleFile();
                if (!selected.empty())
                    std::snprintf(subtitle_path, import_path_capacity, "%s", selected.c_str());
            }
            ImGui::SameLine();
            ImGui::TextColored(kMuted, "Attached explicitly to the next track");
            if (ImGui::Button(subtitle_path[0] == '\0' ? "ADD FILES" : "ADD FILE",
                              ImVec2(-1.0f, 0.0f)))
            {
                for (const auto &path : PickAudioFiles(subtitle_path[0] == '\0'))
                {
                    SubmitFile(controller, path, subtitle_path, status, error);
                }
            }
            if (ImGui::Button("ADD FOLDER", ImVec2(-1.0f, 0.0f)))
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
            const float action_width = (ImGui::GetContentRegionAvail().x - 6.0f) * 0.5f;
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
            if (!error.empty() || !status.empty())
            {
                ImGui::TextColored(error.empty() ? kMuted : kDanger, "%s",
                                   error.empty() ? status.c_str() : error.c_str());
            }
        }

        void RenderPlaylists(const std::vector<TrackView> &queue)
        {
            char queue_label[96]{};
            std::snprintf(queue_label, sizeof(queue_label), "Current queue  [%03llu]",
                          static_cast<unsigned long long>(queue.size()));
            ImGui::Selectable(queue_label, true, ImGuiSelectableFlags_Disabled,
                              ImVec2(0.0f, 30.0f));
            ImGui::Spacing();
            ImGui::TextWrapped("Session playlist. Nothing is written to disk.");
        }

        void RenderAudioPreview(AudioPlayerController &controller,
                                const PlaybackView &playback, ImFont *font,
                                const std::array<std::vector<std::uint8_t>, 7> &icon_alpha)
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
            state.voice_open_icon = {std::span<const std::uint8_t>{icon_alpha[5]},
                                     kTransportIconSize, kTransportIconSize};
            state.voice_close_icon = {std::span<const std::uint8_t>{icon_alpha[6]},
                                      kTransportIconSize, kTransportIconSize};
            state.spectrum_bins = playback.spectrum;
            state.font = font;
            state.has_clip = playback.track.has_value();
            state.is_playing = playback.state == audio::AudioState::Playing;
            state.is_muted = playback.muted;
            state.is_error = !playback.error.empty();
            state.can_seek = playback.can_seek;
            state.has_subtitle_track = playback.subtitle_track_attached;

            const AudioPreviewActions actions = DrawAudioPreview(state);
            std::string diagnostic;
            if (actions.previous) controller.Previous(true, diagnostic);
            if (actions.toggle_play_pause) controller.TogglePlayPause(diagnostic);
            if (actions.next) controller.Next(true, diagnostic);
            if (actions.stop) controller.Stop();
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
            if (playback.track.has_value())
            {
                const auto &track = *playback.track;
                TextField("Name", track.name.c_str());
                TextField("Path", track.path.c_str());
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
                if (ImGui::SmallButton(track.favorite ? "Remove favorite" : "Add favorite"))
                {
                    controller.ToggleFavorite(track.id);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Reimport"))
                {
                    std::string diagnostic;
                    if (!controller.ReimportSelected(subtitle_path, diagnostic))
                        ui_error = std::move(diagnostic);
                    else
                        ui_status = "Reimport queued";
                }
            }
            else
            {
                ImGui::TextColored(kMuted, "Select a track to inspect its metadata.");
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
            ImGui::TextColored(kCyan, "// SOURCE SIGNAL");
            if (!playback.track.has_value())
            {
                ImGui::TextColored(kMuted, "Import a clip to inspect its played-cursor spectrum.");
                return;
            }
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, kCyan);
            ImGui::PlotHistogram("##SourceSpectrum", playback.spectrum.data(),
                                 static_cast<int>(playback.spectrum.size()), 0,
                                 nullptr, 0.0f, 1.0f, ImVec2(-1.0f, 115.0f));
            ImGui::PopStyleColor();
            ImGui::Text("RMS %.3f  |  PEAK %.3f", playback.rms, playback.peak);
            ImGui::TextColored(kMuted, "Decoded source at the played cursor; not device output telemetry.");
        }

        std::string LowerAscii(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char c) {
                return static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
            });
            return value;
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

        try
        {
            playback_icon_alpha_ = LoadTransportIcons();
            audio_panel_ = std::make_unique<editor::EditorAudioComponent>(&audio_system);
            ui_ = std::make_unique<editor::EditorUI>();
            layout_.ResetToThreeColumnTwoRowDefault();
            dock_model_.Clear();
            dock_host_ = std::make_unique<editor::EditorToolRowComponent>(
                dock_model_, editor::EditorWindowConfig{});
            dock_host_->SetLayoutModel(&layout_);
            dock_host_->AddPanel(
                "audio_library", "Library",
                std::make_unique<AudioPlayerDockPanel>("Library", [this] {
                    RenderLibrary(*controller_, controller_->GetQueue(), library_filter_,
                                 import_path_, subtitle_path_, sizeof(import_path_),
                                 ui_status_, ui_error_);
                }), true, editor::EditorLayoutSlot::WorldOutliner);
            dock_host_->AddPanel(
                "audio_playlists", "Playlists",
                std::make_unique<AudioPlayerDockPanel>("Playlists", [this] {
                    RenderPlaylists(controller_->GetQueue());
                }), true, editor::EditorLayoutSlot::ActorInspector);
            dock_host_->AddPanel(
                "audio_now_playing", "Now Playing",
                std::make_unique<AudioPlayerDockPanel>("Now Playing", [this] {
                    RenderAudioPreview(*controller_, controller_->GetPlaybackView(),
                                       ui_->GetCodeFont(), playback_icon_alpha_);
                }), true, editor::EditorLayoutSlot::Viewport);
            dock_host_->AddPanel(
                "audio_queue", "Playlist : All",
                std::make_unique<AudioPlayerDockPanel>("Playlist : All", [this] {
                    RenderQueue(*controller_, search_, sizeof(search_), library_filter_,
                                ui_status_, ui_error_);
                }), true, editor::EditorLayoutSlot::ToolRow);
            dock_host_->AddPanel(
                "audio_info", "Info",
                std::make_unique<AudioPlayerDockPanel>("Info", [this] {
                    RenderInspector(*controller_, *audio_system_,
                                    controller_->GetPlaybackView(), subtitle_path_,
                                    ui_status_, ui_error_, show_diagnostics_);
                }), true, editor::EditorLayoutSlot::CameraSettings);
            dock_host_->AddPanel(
                "audio_spectrum", "Spectrum",
                std::make_unique<AudioPlayerDockPanel>("Spectrum", [this] {
                    RenderSpectrum(controller_->GetPlaybackView());
                }),
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
        layout_.Resolve({viewport->WorkPos.x, viewport->WorkPos.y,
                         viewport->WorkSize.x, viewport->WorkSize.y});
    }
}
