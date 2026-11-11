#include "audio_player_editor.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <exception>
#include <functional>
#include <string_view>
#include <tuple>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <commdlg.h>
#include <shlobj.h>
#endif

#include "module/audio/audio_player_controller.h"
#include "audio_player_theme.h"
#include "audio_preview_widget.h"
#include "editor/ui/api/editor_ui/ui_backend.h"
#include "editor/ui/editor_ui.h"
#include "editor/ui/component/editor_docked_workspace.h"
#include "editor/profile/editor_metric.h"
#include "editor/profile/editor_profile_metrics.h"
#include "runtime/audio/miniaudio_audio_system.h"
#include "runtime/core/log/log_system.h"
#include "runtime/engine.h"
#include "runtime/runtime_global_context.h"
#include "runtime/window/window_system.h"
#include "runtime/render/render_system.h"

namespace kpengine::audio_player
{
    namespace
    {
        using editor::ui::UiContext;
        using editor::ui::UiTableColumn;
        using editor::ui::UiTextRole;

        std::string CountLabel(const std::size_t count)
        {
            char text[32]{};
            std::snprintf(text, sizeof(text), "%03zu", count);
            return text;
        }

        void Field(UiContext &ui, const std::string_view label, const std::string_view value)
        {
            ui.TableRow();
            ui.TableColumn();
            ui.Text(label, UiTextRole::Muted);
            ui.TableColumn();
            ui.WrappedText(value);
        }

        std::string LowerAscii(std::string_view value)
        {
            std::string lowered{value};
            std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](const char ch)
            {
                return static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            });
            return lowered;
        }

        std::string FormatTime(const float seconds)
        {
            const auto value = static_cast<unsigned int>(std::max(0.0F, seconds));
            char result[24]{};
            std::snprintf(result, sizeof(result), "%02u:%02u", value / 60, value % 60);
            return result;
        }

        std::string FormatBytes(const std::uint64_t bytes)
        {
            char result[48]{};
            if (bytes >= 1024ull * 1024ull)
                std::snprintf(result, sizeof(result), "%.1f MiB",
                              static_cast<double>(bytes) / (1024.0 * 1024.0));
            else if (bytes >= 1024ull)
                std::snprintf(result, sizeof(result), "%.0f KiB",
                              static_cast<double>(bytes) / 1024.0);
            else
                std::snprintf(result, sizeof(result), "%llu B",
                              static_cast<unsigned long long>(bytes));
            return result;
        }

        std::string TrackControlId(const std::string_view owner, const std::uint64_t id)
        {
            return "audio." + std::string{owner} + ".track." + std::to_string(id);
        }

        std::string ProjectTrackControlId(const std::string_view content_id)
        {
            return "audio.library.project." + std::string{content_id};
        }

#ifdef _WIN32
        std::string WideToUtf8(const std::wstring_view wide)
        {
            if (wide.empty()) return {};
            const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
            if (bytes <= 0) return {};
            std::string result(static_cast<std::size_t>(bytes), '\0');
            WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                                result.data(), bytes, nullptr, nullptr);
            return result;
        }

        std::vector<std::string> PickAudioFiles()
        {
            std::vector<wchar_t> buffer(65536, L'\0');
            OPENFILENAMEW dialog{};
            dialog.lStructSize = sizeof(dialog);
            dialog.lpstrFilter = L"Audio files\0*.wav;*.mp3;*.flac;*.ogg;*.m4a\0All files\0*.*\0\0";
            dialog.lpstrFile = buffer.data();
            dialog.nMaxFile = static_cast<DWORD>(buffer.size());
            dialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT |
                           OFN_HIDEREADONLY;
            if (!GetOpenFileNameW(&dialog)) return {};
            const wchar_t *cursor = buffer.data();
            const std::wstring directory{cursor};
            cursor += directory.size() + 1;
            if (*cursor == L'\0') return {WideToUtf8(directory)};

            std::vector<std::string> result;
            while (*cursor != L'\0')
            {
                const std::wstring filename{cursor};
                result.push_back(WideToUtf8(directory + L"\\" + filename));
                cursor += filename.size() + 1;
            }
            return result;
        }

        std::string PickSubtitleFile()
        {
            std::array<wchar_t, 32768> path{};
            OPENFILENAMEW dialog{};
            dialog.lStructSize = sizeof(dialog);
            dialog.lpstrFilter = L"Subtitles\0*.srt;*.vtt;*.lrc\0All files\0*.*\0\0";
            dialog.lpstrFile = path.data();
            dialog.nMaxFile = static_cast<DWORD>(path.size());
            dialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
            return GetOpenFileNameW(&dialog) ? WideToUtf8(path.data()) : std::string{};
        }

        std::string PickAudioFolder()
        {
            BROWSEINFOW dialog{};
            dialog.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
            PIDLIST_ABSOLUTE selected = SHBrowseForFolderW(&dialog);
            if (selected == nullptr) return {};
            std::array<wchar_t, MAX_PATH> path{};
            const bool valid = SHGetPathFromIDListW(selected, path.data()) != FALSE;
            CoTaskMemFree(selected);
            return valid ? WideToUtf8(path.data()) : std::string{};
        }
#else
        std::vector<std::string> PickAudioFiles() { return {}; }
        std::string PickSubtitleFile() { return {}; }
        std::string PickAudioFolder() { return {}; }
#endif

        const char *LogLevelName(const program::LogLevel level)
        {
            switch (level)
            {
            case program::LogLevel::Debug: return "Debug";
            case program::LogLevel::Info: return "Info";
            case program::LogLevel::Warning: return "Warning";
            case program::LogLevel::Error: return "Error";
            case program::LogLevel::Fatal: return "Fatal";
            }
            return "Unknown";
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
        log_system_ = context.log_system_.get();

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
            icons_.Load();
            workspace_ = std::make_unique<editor::EditorDockedWorkspace>(
                editor::CreateEditorProfileMetrics(
                    &engine, context.memory_sampler_.get(), context.render_system_.get()));
            using Slot = editor::EditorLayoutSlot;
            const auto panel = [this](const char *id, const char *title, const Slot slot,
                                      void (AudioPlayerEditor::*draw)(UiContext &))
            {
                workspace_->AddPanel(id, title, slot, [this, draw](UiContext &ui) {
                    const auto theme = ui.Theme(AudioPlayerTheme::style);
                    (this->*draw)(ui);
                });
            };
            panel("audio_library", "Library", Slot::WorldOutliner, &AudioPlayerEditor::RenderLibrary);
            panel("audio_playlists", "Playlists", Slot::ActorInspector, &AudioPlayerEditor::RenderPlaylists);
            panel("audio_now_playing", "Now Playing", Slot::Viewport, &AudioPlayerEditor::RenderNowPlaying);
            panel("audio_queue", "Playlist : All", Slot::ToolRow, &AudioPlayerEditor::RenderQueue);
            panel("audio_log", "Output Log", Slot::ToolRow, &AudioPlayerEditor::RenderOutputLog);
            panel("audio_info", "Info", Slot::CameraSettings, &AudioPlayerEditor::RenderInfo);
            panel("audio_spectrum", "Spectrum", Slot::DebugViewer, &AudioPlayerEditor::RenderSpectrum);
            ui_ = std::make_unique<editor::EditorUI>();
            ui_->InitializeUiViewer(init_info,
                [this](UiContext &ui) { RenderWorkspace(ui); });
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
            diagnostic = "Audio Player presentation failed";
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
        workspace_.reset();
        controller_ = nullptr;
        audio_system_ = nullptr;
        log_system_ = nullptr;
    }

    void AudioPlayerEditor::RenderWorkspace(UiContext &ui)
    {
        if (workspace_) workspace_->Render(ui);
    }

    void AudioPlayerEditor::ReportImport(std::string path, std::string subtitle)
    {
        if (controller_ == nullptr || path.empty()) return;
        std::string diagnostic;
        const bool accepted = controller_->ImportFile(std::move(path), std::move(subtitle), diagnostic);
        if (accepted)
        {
            ui_status_ = "Import queued";
            ui_error_.clear();
        }
        else
        {
            ui_error_ = std::move(diagnostic);
        }
    }

    void AudioPlayerEditor::RenderLibrary(UiContext &ui)
    {
        ui.Section("// LIBRARY");
        const auto queue = controller_->GetQueue();
        const auto favorites = static_cast<std::size_t>(std::count_if(
            queue.begin(), queue.end(), [](const TrackView &track) { return track.favorite; }));
        constexpr std::array categories{"All", "Recent", "Favorites"};
        constexpr std::array<UiTableColumn, 2> nav_columns{{{"Collection"}, {"Count", 50.0F}}};
        ui.Table("audio.library.collections", nav_columns, 0.0F, false, [&] {
            for (std::size_t index = 0; index < categories.size(); ++index)
            {
                const int filter = static_cast<int>(index);
                const bool active = library_filter_ == filter;
                ui.TableRow(active);
                ui.TableColumn();
                const std::string id = "audio.library.category." + std::to_string(filter);
                editor::ui::UiControlDescriptor control{id, categories[index]};
                control.height = 26.0F;
                control.text_role = active ? UiTextRole::Warning : UiTextRole::Accent;
                control.span_row = true;
                if (ui.Selectable(control, active).activated)
                {
                    library_filter_ = filter;
                    queue_search_.clear();
                    workspace_->ShowPanel("audio_queue");
                }
                ui.TableColumn();
                ui.Text("[" + CountLabel(filter == 2 ? favorites : queue.size()) + "]",
                        active ? UiTextRole::Warning : UiTextRole::Primary);
            }
        });
        ui.Section("// PROJECT MUSIC");
        const auto library = controller_->GetProjectLibrary();
        const float field_width = std::max(1.0F, ui.AvailableContent().x);
        constexpr std::array<UiTableColumn, 2> refresh_columns{{{"Tracks"}, {"Refresh", 65.0F}}};
        ui.Table("audio.library.refresh-row", refresh_columns, 0.0F, false, [&] {
            ui.TableRow();
            ui.TableColumn();
            ui.Text(CountLabel(library.tracks.size()) + " tracks");
            ui.TableColumn();
            const editor::ui::UiControlDescriptor refresh{"audio.library.refresh", "REFRESH"};
            if (ui.Button(refresh).activated)
            {
                std::string diagnostic;
                if (!controller_->RefreshProjectLibrary(diagnostic)) ui_error_ = diagnostic;
            }
        });
        project_search_ = ui.TextInput({.id = "audio.library.search", .label = "",
            .hint = "Filter project music...", .width = field_width}, project_search_).value;
        const auto needle = LowerAscii(project_search_);
        const float list_height = std::clamp(ui.AvailableContent().y * 0.31F, 56.0F, 115.0F);
        constexpr std::array<UiTableColumn, 2> music_columns{{{"Track"}, {"Type", 54.0F}}};
        ui.Table("audio.library.project-tracks", music_columns, list_height, false, [&] {
            bool matched = false;
            for (const TrackView &track : library.tracks)
            {
                if (!needle.empty() && LowerAscii(track.name + " " + track.path).find(needle) ==
                                        std::string::npos)
                    continue;
                matched = true;
                ui.TableRow();
                ui.TableColumn();
                const std::string id = ProjectTrackControlId(track.content_id);
                editor::ui::UiControlDescriptor control{id, track.name};
                control.icon = icons_.Get(editor::EditorIcon::Music);
                control.text_role = UiTextRole::Accent;
                control.span_row = true;
                if (ui.Selectable(control, false).activated)
                {
                    std::string diagnostic;
                    if (!controller_->QueueProjectTrack(track.content_id, diagnostic))
                        ui_error_ = diagnostic;
                    else
                    {
                        ui_status_ = "Added to queue: " + track.name;
                        workspace_->ShowPanel("audio_queue");
                    }
                }
                ui.TableColumn();
                ui.Text(track.extension);
            }
            if (!matched)
            {
                ui.TableRow();
                ui.TableColumn();
                ui.Text(needle.empty() ? "No project music found." : "No matches.", UiTextRole::Muted);
            }
        });
        if (!library.status.empty()) ui.Text(library.status, UiTextRole::Muted);
        if (!library.error.empty()) ui.WrappedText(library.error);

        ui.Section("// IMPORT");
        const float half_button = std::max(1.0F, (field_width - AudioPlayerTheme::style.item_spacing_x) * 0.5F);
        if (ui.Button({.id = "audio.library.browse-files", .label = "ADD FILES",
                       .width = half_button}).activated)
        {
            for (std::string &file : PickAudioFiles()) ReportImport(std::move(file), subtitle_path_);
        }
        ui.SameLine();
        if (ui.Button({.id = "audio.library.import-folder", .label = "ADD FOLDER",
                       .width = half_button}).activated)
        {
            const std::string folder = PickAudioFolder();
            if (!folder.empty())
            {
                std::string diagnostic;
                if (!controller_->ImportFolder(folder, diagnostic)) ui_error_ = diagnostic;
                else ui_status_ = "Folder import queued";
            }
        }
        const auto path = ui.TextInput({.id = "audio.library.path", .label = "",
                                        .hint = "Audio file or folder path…",
                                        .width = field_width}, import_path_);
        import_path_ = path.value;
        const bool has_path = !import_path_.empty();
        if (ui.Button({.id = "audio.library.queue-file", .label = "QUEUE FILE",
                       .enabled = has_path, .disabled_reason = "Choose an audio path first",
                       .width = half_button}).activated)
            ReportImport(import_path_, subtitle_path_);
        ui.SameLine();
        if (ui.Button({.id = "audio.library.queue-folder", .label = "QUEUE FOLDER",
                       .enabled = has_path, .disabled_reason = "Choose a folder path first",
                       .width = half_button}).activated)
        {
            std::string diagnostic;
            if (!controller_->ImportFolder(import_path_, diagnostic)) ui_error_ = diagnostic;
            else ui_status_ = "Folder import queued";
        }
        if (ui.Button({.id = "audio.library.toggle-subtitle",
                       .label = show_subtitle_import_ ? "v  SUBTITLE (OPTIONAL)" : ">  SUBTITLE (OPTIONAL)",
                       .width = field_width}).activated)
            show_subtitle_import_ = !show_subtitle_import_;
        if (show_subtitle_import_)
        {
            subtitle_path_ = ui.TextInput({.id = "audio.library.subtitle", .label = "",
                .hint = "SRT, WebVTT, or LRC path...", .width = field_width}, subtitle_path_).value;
            if (ui.Button({.id = "audio.library.browse-subtitle", .label = "BROWSE SUBTITLE",
                           .width = field_width}).activated)
            {
                const auto selected = PickSubtitleFile();
                if (!selected.empty()) subtitle_path_ = selected;
            }
        }
        if (!ui_status_.empty()) ui.Text(ui_status_);
        if (!ui_error_.empty()) ui.Text("Error: " + ui_error_, editor::ui::UiTextRole::Error);
    }

    void AudioPlayerEditor::RenderPlaylists(UiContext &ui)
    {
        ui.Section("// PLAYLISTS");
        const auto queue = controller_->GetQueue();
        const std::string label = "Current queue   [" + CountLabel(queue.size()) + "]";
        if (ui.Selectable({.id = "audio.playlists.current", .label = label, .height = 28.0F,
                           .text_role = UiTextRole::Warning}, true).activated)
        {
            library_filter_ = 0;
            queue_search_.clear();
            workspace_->ShowPanel("audio_queue");
        }
        ui.Section("// COLLECTION");
        float duration = 0.0F;
        for (const TrackView &track : queue) duration += track.duration_seconds;
        ui.Text("Duration        " + FormatTime(duration));
        ui.Text("SESSION ONLY", UiTextRole::Muted);
    }

    void AudioPlayerEditor::RenderNowPlaying(UiContext &ui)
    {
        const PlaybackView playback = controller_->GetPlaybackView();
        const AudioPreviewActions actions = DrawAudioPreview(ui, playback, icons_);
        std::string diagnostic;
        if (actions.previous) (void)controller_->Previous(true, diagnostic);
        if (actions.toggle_play_pause && !controller_->TogglePlayPause(diagnostic)) ui_error_ = diagnostic;
        if (actions.next && !controller_->Next(true, diagnostic)) ui_error_ = diagnostic;
        if (actions.stop) controller_->Stop();
        if (actions.seek_seconds) (void)controller_->Seek(*actions.seek_seconds);
        if (actions.volume) controller_->SetVolume(*actions.volume);
        if (actions.playback_rate && !controller_->SetPlaybackRate(*actions.playback_rate))
            ui_error_ = "Playback rate must be between 0.5x and 2.0x";
        if (actions.muted) controller_->SetMuted(*actions.muted);
        if (actions.toggle_loop) controller_->SetLoopTrack(!playback.loop_track);
        if (!ui_error_.empty()) ui.Text("Error: " + ui_error_);
    }

    void AudioPlayerEditor::RenderQueue(UiContext &ui)
    {
        ui.Section(library_filter_ == 1 ? "// PLAYLIST / RECENT" :
                   library_filter_ == 2 ? "// PLAYLIST / FAVORITES" : "// PLAYLIST / ALL");
        const float width = std::max(1.0F, ui.AvailableContent().x);
        queue_search_ = ui.TextInput({.id = "audio.queue.search", .label = "",
            .hint = "Search tracks...", .width = std::min(width, 340.0F)}, queue_search_).value;
        const auto queue = controller_->GetQueue();
        const auto playback = controller_->GetPlaybackView();
        const auto needle = LowerAscii(queue_search_);
        std::vector<const TrackView *> tracks;
        tracks.reserve(queue.size());
        for (const auto &track : queue)
        {
            if ((library_filter_ != 2 || track.favorite) &&
                (needle.empty() || LowerAscii(track.name + " " + track.path).find(needle) != std::string::npos))
                tracks.push_back(&track);
        }
        if (library_filter_ == 1) std::reverse(tracks.begin(), tracks.end());

        constexpr std::array<UiTableColumn, 6> full{{
            {"#", 74.0F}, {"TRACK"}, {"DURATION", 86.0F}, {"SAMPLE RATE", 112.0F},
            {"TYPE", 70.0F}, {"SOURCE", 76.0F}}};
        constexpr std::array<UiTableColumn, 4> compact{{
            {"#", 28.0F}, {"TRACK"}, {"TIME", 46.0F}, {"FORMAT", 106.0F}}};
        const bool wide = width >= 720.0F;
        const auto columns = wide ? std::span<const UiTableColumn>{full}
                                  : std::span<const UiTableColumn>{compact};
        ui.Table("audio.queue.tracks", columns, std::max(48.0F, ui.AvailableContent().y - 34.0F),
                 true, [&] {
            if (tracks.empty())
            {
                ui.TableRow();
                ui.TableColumn();
                ui.TableColumn();
                ui.Text(queue.empty() ? "Add files or select a project track to start."
                                      : "No tracks match this filter.", UiTextRole::Muted);
            }
            std::size_t row = 0;
            for (const auto *track : tracks)
            {
                const bool selected = playback.track && playback.track->id == track->id;
                const auto color = selected ? UiTextRole::Warning : UiTextRole::Primary;
                ui.TableRow(selected);
                ui.TableColumn();
                ui.Text(CountLabel(++row), color);
                ui.TableColumn();
                const std::string id = TrackControlId("queue", track->id);
                editor::ui::UiControlDescriptor control{id, track->name};
                control.icon = icons_.Get(selected && playback.state == audio::AudioState::Playing
                    ? editor::EditorIcon::Play : editor::EditorIcon::Music);
                control.height = 20.0F;
                control.text_role = selected ? UiTextRole::Warning : UiTextRole::Accent;
                control.span_row = true;
                if (ui.Selectable(control, selected).activated)
                    (void)controller_->Select(track->id);
                ui.TableColumn();
                ui.Text(FormatTime(track->duration_seconds), color);
                ui.TableColumn();
                char rate[24]{};
                std::snprintf(rate, sizeof(rate), "%.1f kHz", track->sample_rate / 1000.0);
                ui.Text(wide ? rate : std::string{rate} + " " + track->extension, color);
                if (wide)
                {
                    ui.TableColumn();
                    ui.Text(track->extension, color);
                    ui.TableColumn();
                    ui.Text(track->project_asset ? "CONTENT" : "SESSION", color);
                }
            }
        });

        if (ui.Button({.id = "audio.queue.remove", .label = "REMOVE",
                       .enabled = playback.track.has_value()}).activated)
            (void)controller_->RemoveSelected();
        ui.SameLine();
        if (ui.Button({.id = "audio.queue.clear", .label = "CLEAR QUEUE",
                       .enabled = !queue.empty()}).activated)
            controller_->ClearQueue();
        if (width >= 480.0F)
        {
            ui.SameLine();
            ui.Text(std::to_string(queue.size()) + " tracks | " +
                    std::to_string(playback.pending_imports) + " importing", UiTextRole::Accent);
        }
        if (width >= 550.0F)
        {
            ui.SameLine();
            const auto shuffle = ui.Checkbox({.id = "audio.queue.shuffle", .label = "Shuffle"}, playback.shuffle);
            if (shuffle.changed) controller_->SetShuffle(shuffle.value);
        }
    }

    void AudioPlayerEditor::RenderInfo(UiContext &ui)
    {
        ui.Section("// AUDIO DETAIL");
        const auto playback = controller_->GetPlaybackView();
        const float width = std::max(1.0F, ui.AvailableContent().x);
        const std::array<UiTableColumn, 2> fields{{
            {"Field", std::clamp(width * 0.26F, 66.0F, 108.0F)}, {"Value"}}};
        if (playback.track)
        {
            const auto &track = *playback.track;
            ui.Text("// SOURCE", UiTextRole::Accent);
            ui.Table("audio.info.source", fields, 0.0F, false, [&] {
                Field(ui, "Name", track.name);
                Field(ui, "Path", track.path);
                Field(ui, "Origin", track.project_asset ? "Project Content" : "Session import");
            });
            ui.Text("// FORMAT", UiTextRole::Accent);
            ui.Table("audio.info.format", fields, 0.0F, false, [&] {
                Field(ui, "Type", track.extension);
                Field(ui, "Channels", track.channels == 1 ? "Mono" : track.channels == 2
                    ? "Stereo" : std::to_string(track.channels));
                Field(ui, "Sample rate", std::to_string(track.sample_rate) + " Hz");
                Field(ui, "Duration", FormatTime(track.duration_seconds));
                Field(ui, "File size", FormatBytes(track.file_size));
                Field(ui, "Runtime", track.native_product ? "Native Audio; file-backed streaming" : "Audio source");
                Field(ui, "Subtitles", track.has_subtitles ? track.subtitle_language + " (" +
                    std::to_string(track.subtitle_cue_count) + " cues)" : "Not attached");
            });
            ui.Text("// ACTIONS", UiTextRole::Accent);
            if (ui.Button({.id = "audio.info.favorite",
                           .label = track.favorite ? "Remove favorite" : "Add favorite"}).activated)
                (void)controller_->ToggleFavorite(track.id);
            ui.SameLine();
            if (track.project_asset)
                ui.Text("Reimport with AssetTool", UiTextRole::Muted);
            else if (ui.Button({.id = "audio.info.reimport", .label = "Reimport"}).activated)
            {
                std::string diagnostic;
                if (!controller_->ReimportSelected(subtitle_path_, diagnostic)) ui_error_ = diagnostic;
                else ui_status_ = "Reimport queued";
            }
        }
        else ui.Text("No track selected.", UiTextRole::Muted);

        ui.Section("// OUTPUT");
        const auto device = audio_system_->GetDeviceInfo();
        ui.Table("audio.info.output", fields, 0.0F, false, [&] {
            Field(ui, "Device", device.initialized
                ? (device.name.empty() ? "Default output" : device.name) : "Stopped");
            if (device.initialized)
            {
                Field(ui, "Format", std::to_string(device.sample_rate) + " Hz / " +
                      std::to_string(device.channels) + " ch");
                char latency[24]{};
                std::snprintf(latency, sizeof(latency), "%.1f ms", device.estimated_latency_ms);
                Field(ui, "Latency", latency);
            }
        });
        if (ui.Button({.id = "audio.info.output-device",
                       .label = device.initialized ? "Stop output device" : "Start output device"}).activated)
        {
            if (device.initialized) audio_system_->ShutDown();
            else if (!audio_system_->Initialize()) ui_error_ = "Could not initialize the audio device";
        }
        ui.Text("Player output uses the Music bus.", UiTextRole::Muted);
        if (ui.Button({.id = "audio.info.diagnostics", .label = "Mixer diagnostics",
                       .emphasized = show_diagnostics_}).activated)
            show_diagnostics_ = !show_diagnostics_;
        if (!show_diagnostics_) return;
        const auto master = ui.Slider<double>(
            {.id = "audio.info.master", .label = "Master output"},
            static_cast<double>(std::clamp(audio_system_->GetMasterGain(), 0.0F, 1.0F)), 0.0, 1.0);
        if (master.changed) audio_system_->SetMasterGain(static_cast<float>(master.value));
        for (const auto [bus, id, label] : std::array{
                 std::tuple{audio::AudioBus::Speech, "audio.info.speech", "Speech bus"},
                 std::tuple{audio::AudioBus::Music, "audio.info.music", "Music bus"}})
        {
            const float gain = std::clamp(audio_system_->GetBusGain(bus), 0.0F, 1.0F);
            const auto edit = ui.Slider<double>({.id = id, .label = label},
                                                static_cast<double>(gain), 0.0, 1.0);
            if (edit.changed) audio_system_->SetBusGain(bus, static_cast<float>(edit.value));
            const std::string mute_id = std::string{id} + ".mute";
            const auto muted = ui.Checkbox({.id = mute_id,
                                            .label = std::string{label} + " mute"},
                                           audio_system_->IsBusMuted(bus));
            if (muted.changed) audio_system_->SetBusMuted(bus, muted.value);
        }

        const audio::AudioTelemetrySnapshot telemetry = audio_system_->GetTelemetrySnapshot();
        ui.Text("Callbacks · " + std::to_string(telemetry.callback_count) +
                " · frames " + std::to_string(telemetry.callback_frames));
        ui.Text("Stream rejects · " + std::to_string(telemetry.stream_overflow_rejections) +
                " · command rejects " + std::to_string(telemetry.control_command_rejections));
        ui.Text("Max callback · " + std::to_string(telemetry.max_callback_duration_ns / 1'000'000.0) + " ms");
        for (std::size_t index = 0; index < telemetry.buses.size(); ++index)
            ui.Text("Bus " + std::to_string(index) + " · frames " +
                    std::to_string(telemetry.buses[index].played_frames) + " · underruns " +
                    std::to_string(telemetry.buses[index].underrun_blocks));
    }

    void AudioPlayerEditor::RenderSpectrum(UiContext &ui)
    {
        DrawAudioSpectrum(ui, controller_->GetPlaybackView());
    }

    void AudioPlayerEditor::RenderOutputLog(UiContext &ui)
    {
        ui.Text("// OUTPUT LOG", editor::ui::UiTextRole::Accent);
        if (log_system_ == nullptr)
        {
            ui.Text("Runtime log service is unavailable.");
            return;
        }
        const auto search = ui.TextInput({.id = "audio.log.search", .label = "",
            .hint = "Filter output…", .width = std::clamp(ui.AvailableContent().x, 1.0F, 280.0F)}, output_search_);
        output_search_ = search.value;
        const std::vector<program::LogEntry> logs = log_system_->GetLogSnapshot();
        const std::string needle = LowerAscii(output_search_);
        std::size_t emitted = 0;
        for (auto it = logs.rbegin(); it != logs.rend() && emitted < 250; ++it)
        {
            const std::string searchable = LowerAscii(it->name + " " + it->message + " " + it->file);
            if (!needle.empty() && searchable.find(needle) == std::string::npos) continue;
            ui.Text(std::string{"["} + LogLevelName(it->level) + "][" + it->name + "] " + it->message,
                    it->level == program::LogLevel::Error || it->level == program::LogLevel::Fatal
                        ? editor::ui::UiTextRole::Error : editor::ui::UiTextRole::Muted);
            ++emitted;
        }
        if (emitted == 0) ui.Text(needle.empty() ? "No log entries." : "No entries match this filter.");
    }
}
