#include "tts_editor_editor.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <exception>
#include <optional>
#include <string_view>

#include "editor/asset/editor_asset_file_dialog.h"
#include "module/audio/editor/audio_transport_controls.h"
#include "editor/ui/host/editor_ui_host.h"
#include "editor/ui/api/editor_ui/ui_plot.h"
#include "runtime/core/config/path.h"
#include "runtime/engine.h"
#include "runtime/image_io/image_io.h"
#include "runtime/render/render_system.h"
#include "runtime/runtime_global_context.h"
#include "runtime/window/window_system.h"

namespace kpengine::tts_editor
{
    namespace
    {
        constexpr editor::ui::UiColor kCyan{0.18F, 0.83F, 0.94F, 1.0F};

        bool ContainsCaseInsensitive(const std::string_view text,
                                     const std::string_view query)
        {
            if (query.empty()) return true;
            if (query.size() > text.size()) return false;
            for (std::size_t start = 0; start <= text.size() - query.size(); ++start)
            {
                bool matches = true;
                for (std::size_t offset = 0; offset < query.size(); ++offset)
                {
                    const auto lhs = static_cast<unsigned char>(text[start + offset]);
                    const auto rhs = static_cast<unsigned char>(query[offset]);
                    const auto fold = [](const unsigned char value)
                    {
                        return value < 0x80u
                            ? static_cast<unsigned char>(std::tolower(value)) : value;
                    };
                    if (fold(lhs) != fold(rhs))
                    {
                        matches = false;
                        break;
                    }
                }
                if (matches) return true;
            }
            return false;
        }

        std::string CompactDialogText(const std::string_view text,
                                      const std::size_t max_codepoints = 76)
        {
            std::string compact;
            compact.reserve(std::min(text.size(), max_codepoints * 3));
            std::size_t codepoints = 0;
            bool pending_space = false;
            for (std::size_t offset = 0; offset < text.size();)
            {
                const unsigned char lead = static_cast<unsigned char>(text[offset]);
                if (lead == '\r' || lead == '\n' || lead == '\t' || lead == ' ')
                {
                    pending_space = !compact.empty();
                    ++offset;
                    continue;
                }
                if (codepoints == max_codepoints)
                {
                    compact += "...";
                    break;
                }
                if (pending_space)
                    compact.push_back(' ');
                pending_space = false;
                const std::size_t width = lead < 0x80u ? 1 :
                    (lead & 0xE0u) == 0xC0u ? 2 :
                    (lead & 0xF0u) == 0xE0u ? 3 :
                    (lead & 0xF8u) == 0xF0u ? 4 : 1;
                const std::size_t available = std::min(width, text.size() - offset);
                compact.append(text.substr(offset, available));
                offset += available;
                ++codepoints;
            }
            return compact;
        }

        std::string FormatDuration(const std::optional<float> duration)
        {
            if (!duration || *duration < 0.0f) return "—";
            const int total_seconds = static_cast<int>(*duration);
            char label[16]{};
            std::snprintf(label, sizeof(label), "%02d:%02d",
                          total_seconds / 60, total_seconds % 60);
            return label;
        }

        template<std::size_t Size>
        void CopyTo(std::array<char, Size> &destination, const std::string &source)
        {
            std::snprintf(destination.data(), destination.size(), "%s", source.c_str());
            if (source.size() >= destination.size())
            {
                std::size_t end = destination.size() - 1;
                if ((static_cast<unsigned char>(source[end]) & 0xC0u) == 0x80u)
                {
                    while (end > 0 &&
                        (static_cast<unsigned char>(destination[end - 1]) & 0xC0u) == 0x80u)
                        --end;
                    if (end > 0) --end;
                    destination[end] = '\0';
                }
            }
        }

        const char *JobLabel(const tts::TTSJobState state)
        {
            switch (state)
            {
            case tts::TTSJobState::Queued: return "Queued";
            case tts::TTSJobState::Connecting: return "Connecting";
            case tts::TTSJobState::Generating: return "Generating";
            case tts::TTSJobState::Buffering: return "Buffering";
            case tts::TTSJobState::Playing: return "Playing";
            case tts::TTSJobState::Draining: return "Draining";
            case tts::TTSJobState::Completed: return "Completed";
            case tts::TTSJobState::Cancelled: return "Cancelled";
            case tts::TTSJobState::Failed: return "Failed";
            }
            return "Unknown";
        }

        audio_editor::TransportPlaybackState TransportState(const TtsEntryView &entry,
                                                       const audio::AudioState audio_state)
        {
            if (entry.player.IsValid())
            {
                switch (audio_state)
                {
                case audio::AudioState::Playing: return audio_editor::TransportPlaybackState::Playing;
                case audio::AudioState::Buffering: return audio_editor::TransportPlaybackState::Buffering;
                case audio::AudioState::Paused: return audio_editor::TransportPlaybackState::Paused;
                case audio::AudioState::Finished: return audio_editor::TransportPlaybackState::Finished;
                case audio::AudioState::Cancelled: return audio_editor::TransportPlaybackState::Cancelled;
                default: return audio_editor::TransportPlaybackState::Ready;
                }
            }
            switch (entry.state)
            {
            case tts::TTSJobState::Queued: return audio_editor::TransportPlaybackState::Queued;
            case tts::TTSJobState::Connecting:
            case tts::TTSJobState::Generating: return audio_editor::TransportPlaybackState::Generating;
            case tts::TTSJobState::Buffering: return audio_editor::TransportPlaybackState::Buffering;
            case tts::TTSJobState::Playing: return audio_editor::TransportPlaybackState::Playing;
            case tts::TTSJobState::Draining: return audio_editor::TransportPlaybackState::Draining;
            case tts::TTSJobState::Completed: return audio_editor::TransportPlaybackState::Finished;
            case tts::TTSJobState::Cancelled: return audio_editor::TransportPlaybackState::Cancelled;
            case tts::TTSJobState::Failed: return audio_editor::TransportPlaybackState::Failed;
            }
            return audio_editor::TransportPlaybackState::Idle;
        }
    }

    TtsEditorEditor::TtsEditorEditor() = default;
    TtsEditorEditor::~TtsEditorEditor() { Shutdown(); }

    void TtsEditorEditor::CopySettingsToFields(const TtsEditorSettings &settings)
    {
        draft_settings_ = settings;
        CopyTo(address_, settings.address);
        CopyTo(api_path_, settings.api_path);
        const TtsVoicePreset *voice = SelectedVoice(settings);
        CopyTo(voice_name_, voice ? voice->name : std::string{});
        CopyTo(ref_audio_path_, voice ? voice->ref_audio_path : std::string{});
        CopyTo(ref_text_, voice ? voice->ref_text : std::string{});
        CopyTo(ref_language_, voice ? voice->ref_language : std::string{});
        CopyTo(text_language_, settings.text_language);
        CopyTo(output_directory_, settings.output_directory);
        port_ = settings.port;
        timeout_seconds_ = static_cast<int>(settings.timeout_seconds);
        streaming_ = settings.streaming;
    }

    TtsEditorSettings TtsEditorEditor::FieldsToSettings() const
    {
        TtsEditorSettings settings = draft_settings_;
        settings.address = address_.data();
        settings.port = static_cast<std::uint16_t>(std::clamp(port_, 0, 65535));
        settings.api_path = api_path_.data();
        settings.timeout_seconds = static_cast<std::uint32_t>(
            std::clamp(timeout_seconds_, 1, 3600));
        const auto voice = std::find_if(settings.voices.begin(), settings.voices.end(),
            [&settings](const TtsVoicePreset &candidate) {
                return candidate.id == settings.selected_voice_id;
            });
        if (voice != settings.voices.end())
        {
            voice->name = voice_name_.data();
            voice->ref_audio_path = ref_audio_path_.data();
            voice->ref_text = ref_text_.data();
            voice->ref_language = ref_language_.data();
        }
        settings.text_language = text_language_.data();
        settings.streaming = streaming_;
        settings.output_directory = output_directory_.data();
        return settings;
    }

    bool TtsEditorEditor::Initialize(runtime::Engine &engine,
                                     TtsEditorController &controller,
                                     std::string &diagnostic)
    {
        auto &context = runtime::global_runtime_context;
        if (!context.window_system_ || !context.render_system_ || !context.log_system_)
        {
            diagnostic = "TTS presentation services are unavailable";
            return false;
        }
        controller_ = &controller;
        icons_.Load();
        const auto avatar = image_io::DecodeImageFile(
            (project_root / "resouce/icon/tts/kurisu-avatar.jpg").string());
        if (avatar.result.success && avatar.image.IsValid() &&
            avatar.image.format == image_io::ImagePixelFormat::Rgba8)
        {
            const std::uint32_t side = std::min(avatar.image.width, avatar.image.height);
            const std::uint32_t crop_x = (avatar.image.width - side) / 2;
            const std::uint32_t crop_y = (avatar.image.height - side) / 2;
            for (std::uint32_t y = 0; y < kAvatarRasterSize; ++y)
            {
                const std::uint32_t source_y = crop_y + side - 1 -
                    y * side / static_cast<std::uint32_t>(kAvatarRasterSize);
                for (std::uint32_t x = 0; x < kAvatarRasterSize; ++x)
                {
                    const std::uint32_t source_x = crop_x +
                        x * side / static_cast<std::uint32_t>(kAvatarRasterSize);
                    const std::size_t source_offset =
                        (static_cast<std::size_t>(source_y) * avatar.image.width + source_x) * 4;
                    const std::size_t target_offset =
                        (static_cast<std::size_t>(y) * kAvatarRasterSize + x) * 4;
                    std::copy_n(avatar.image.pixels.data() + source_offset, 4,
                                avatar_pixels_.data() + target_offset);
                }
            }
            avatar_loaded_ = true;
        }
        const TtsEditorView initial_view = controller.GetView();
        last_audible_volume_ = std::clamp(initial_view.volume, 0.05f, 1.0f);
        CopySettingsToFields(initial_view.settings);
        try
        {
            ui_ = std::make_unique<editor::EditorUiHost>();
            ui_->Initialize({
                .window = context.window_system_->GetNativeHandle(),
                .presentation_bridge = context.render_system_->GetEditorPresentationBridge()});
        }
        catch (const std::exception &error)
        {
            diagnostic = std::string("TTS editor initialization failed: ") + error.what();
            Shutdown();
            return false;
        }
        diagnostic.clear();
        return true;
    }

    bool TtsEditorEditor::Render(std::string &diagnostic)
    {
        diagnostic.clear();
        if (!ui_ || !ui_->RenderFrame(
                [this](editor::ui::UiContext &ui) { RenderWorkspace(ui); }))
        {
            diagnostic = "TTS editor presentation failed";
            return false;
        }
        return true;
    }

    void TtsEditorEditor::Shutdown() noexcept
    {
        if (ui_)
        {
            ui_->Close();
            ui_.reset();
        }
        controller_ = nullptr;
    }

    void TtsEditorEditor::RenderPreview(editor::ui::UiContext &ui, const TtsEditorView &view)
    {
        ui.Section("AUDIO PREVIEW");
        const auto selected = std::find_if(view.entries.begin(), view.entries.end(),
            [&view](const TtsEntryView &entry) { return entry.id == view.selected_id; });
        if (selected == view.entries.end())
        {
            ui.Text("Generate speech to preview it here.", editor::ui::UiTextRole::Muted);
            return;
        }

        static const std::vector<float> empty_waveform;
        const auto &wave = selected->preview_data
            ? selected->preview_data->waveform_samples : empty_waveform;
        const double duration = selected->preview_data
            ? selected->preview_data->duration_seconds
            : view.duration_seconds.value_or(0.0F);
        const double plot_duration = duration > 0.0 ? duration : 1.0;
        const std::size_t count = std::min<std::size_t>(wave.size(), 180);
        const double played = duration > 0.0
            ? std::clamp(static_cast<double>(view.elapsed_seconds) / duration, 0.0, 1.0)
            : 0.0;
        std::vector<editor::ui::UiPlotPoint> played_points;
        std::vector<editor::ui::UiPlotPoint> pending_points;
        played_points.reserve(count);
        pending_points.reserve(count);
        for (std::size_t i = 0; i < count; ++i)
        {
            const std::size_t sample_index = i * wave.size() / count;
            const double x = (static_cast<double>(i) + 0.5) * plot_duration /
                static_cast<double>(count);
            const double amplitude = std::clamp(static_cast<double>(wave[sample_index]), 0.02, 1.0);
            (x / plot_duration <= played ? played_points : pending_points).push_back({x, amplitude});
        }
        const editor::ui::UiPlotSeries series[] = {
            {.label = "Played", .points = played_points,
             .color = {1.0F, 0.69F, 0.19F, 1.0F},
             .kind = editor::ui::UiPlotSeriesKind::Bars, .bar_width = 2.0F},
            {.label = "Waveform", .points = pending_points, .color = kCyan,
             .kind = editor::ui::UiPlotSeriesKind::Bars, .bar_width = 2.0F}};
        auto style = editor::ui::UiPlotStyle{};
        style.background = {0.02F, 0.055F, 0.07F, 1.0F};
        style.border = {0.08F, 0.38F, 0.45F, 1.0F};
        style.grid = {0.04F, 0.20F, 0.25F, 1.0F};
        style.axis = kCyan;
        style.labels = {0.49F, 0.72F, 0.76F, 1.0F};
        style.left_margin = 12.0F;
        style.top_margin = 8.0F;
        style.right_margin = 8.0F;
        style.bottom_margin = 22.0F;
        const float available_width = std::max(1.0F, ui.AvailableContent().x);
        const float avatar_size = std::clamp(available_width * 0.20F, 112.0F, 176.0F);
        const editor::ui::UiTableColumn preview_columns[] = {
            {"Portrait", avatar_size + 14.0F}, {"Waveform", 0.0F}};
        editor::ui::UiPlotResult plot{};
        ui.Table("tts.preview.layout", preview_columns, 0.0F, false, [&]
        {
            ui.TableRow();
            ui.TableColumn();
            std::vector<editor::ui::UiCanvasPrimitive> portrait;
            portrait.reserve(kAvatarRasterSize * kAvatarRasterSize + 2);
            portrait.push_back({.kind = editor::ui::UiCanvasPrimitiveKind::FilledRect,
                .first = {0.0F, 0.0F}, .second = {1.0F, 1.0F},
                .color = {0.02F, 0.06F, 0.08F, 1.0F}});
            if (avatar_loaded_)
            {
                constexpr float inset = 3.0F;
                const float scale = (avatar_size - inset * 2.0F) /
                    static_cast<float>(kAvatarRasterSize);
                for (std::size_t y = 0; y < kAvatarRasterSize; ++y)
                {
                    for (std::size_t x = 0; x < kAvatarRasterSize; ++x)
                    {
                        const std::size_t offset =
                            (y * kAvatarRasterSize + x) * 4;
                        const auto channel = [this, offset](const std::size_t index)
                        {
                            return static_cast<float>(avatar_pixels_[offset + index]) / 255.0F;
                        };
                        portrait.push_back({.kind = editor::ui::UiCanvasPrimitiveKind::FilledRect,
                            .first = {(inset + static_cast<float>(x) * scale) / avatar_size,
                                      (inset + static_cast<float>(y) * scale) / avatar_size},
                            .second = {(inset + static_cast<float>(x + 1) * scale) / avatar_size,
                                       (inset + static_cast<float>(y + 1) * scale) / avatar_size},
                            .color = {channel(0), channel(1), channel(2), channel(3)}});
                    }
                }
            }
            else
            {
                portrait.push_back({.kind = editor::ui::UiCanvasPrimitiveKind::Text,
                    .first = {0.5F, 0.5F}, .color = kCyan,
                    .text = "TTS VOICE", .text_anchor = {0.5F, 0.5F}});
            }
            portrait.push_back({.kind = editor::ui::UiCanvasPrimitiveKind::Rect,
                .first = {0.005F, 0.005F}, .second = {0.995F, 0.995F},
                .color = kCyan, .thickness = 1.5F});
            ui.Canvas({.id = "tts.preview.portrait", .label = "TTS voice portrait"},
                      {avatar_size, avatar_size}, std::move(portrait));

            ui.TableColumn();
            ui.Text(selected->voice_name, editor::ui::UiTextRole::Accent);
            ui.SameLine();
            ui.Text(selected->streaming ? "STREAM" : "BUFFER",
                    editor::ui::UiTextRole::Muted);
            if (!selected->text.empty())
                ui.WrappedText(CompactDialogText(selected->text, 112));
            const float plot_height = std::clamp(avatar_size * 0.72F, 112.0F, 154.0F);
            plot = editor::ui::Plot(ui, {
                .control = {.id = "tts.preview.waveform", .label = "Waveform",
                    .enabled = selected->player.IsValid() && !selected->streaming && duration > 0.0},
                .size = {std::max(240.0F, ui.AvailableContent().x), plot_height},
                .x_axis = {.minimum = 0.0, .maximum = plot_duration, .divisions = 4},
                .y_axis = {.minimum = 0.0, .maximum = 1.0, .divisions = 2},
                .series = wave.empty() ? std::span<const editor::ui::UiPlotSeries>{}
                                       : std::span<const editor::ui::UiPlotSeries>{series},
                .style = style,
                .empty_label = selected->streaming
                    ? "Waveform appears when the server WAV arrives"
                    : "Waveform appears when WAV audio is ready"});
        });
        if (plot.canvas.pressed && plot.has_pointer_value && duration > 0.0 &&
            selected->player.IsValid() && !selected->streaming)
            controller_->QueueSeek(static_cast<float>(plot.pointer_x));
        if (duration > 0.0)
            ui.Text(FormatDuration(view.elapsed_seconds) + " / " +
                    FormatDuration(static_cast<float>(duration)),
                    editor::ui::UiTextRole::Muted);
        if (!selected->error.empty()) ui.Text(selected->error, editor::ui::UiTextRole::Error);

        if (!selected->streaming && selected->durable)
        {
            const std::vector<editor::ui::UiChoice> rates{
                {"0.5", "0.5x", 0.5}, {"0.75", "0.75x", 0.75},
                {"1.0", "1.0x", 1.0}, {"1.25", "1.25x", 1.25},
                {"1.5", "1.5x", 1.5}, {"2.0", "2.0x", 2.0}};
            const auto rate = ui.Combo<double>({.id = "tts.preview.rate", .label = "Speed"},
                view.playback_rate, rates);
            if (rate.changed) controller_->QueuePlaybackRate(static_cast<float>(rate.value));
            ui.SameLine();
        }
        const bool muted = view.volume <= 0.001F;
        if (!muted) last_audible_volume_ = view.volume;
        if (ui.Button({.id = "tts.preview.mute", .label = muted ? "Unmute voice" : "Mute voice",
                .text_role = editor::ui::UiTextRole::Accent,
                .icon = icons_.Get(muted ? editor::EditorIcon::VolumeOff : editor::EditorIcon::VolumeOn)}).activated)
            controller_->QueueVolume(muted ? last_audible_volume_ : 0.0F);
        if (const auto volume = audio_editor::DrawAudioVolumeProgressControl(
                ui, "tts.preview.volume", view.volume, ui.AvailableContent().x))
            controller_->QueueVolume(*volume);

        audio_editor::TransportStripState transport{};
        transport.instance_id = "tts_preview";
        transport.playback_state = selected->player.IsValid()
            ? TransportState(*selected, view.audio_state)
            : audio_editor::TransportPlaybackState::Finished;
        transport.elapsed_seconds = view.elapsed_seconds;
        transport.duration_seconds = view.duration_seconds;
        transport.capabilities.toggle_play_pause = selected->durable || selected->player.IsValid();
        transport.capabilities.stop_voice = selected->player.IsValid() &&
            selected->state == tts::TTSJobState::Completed;
        transport.capabilities.cancel_job = selected->job.IsValid() &&
            selected->state != tts::TTSJobState::Completed &&
            selected->state != tts::TTSJobState::Cancelled &&
            selected->state != tts::TTSJobState::Failed;
        transport.capabilities.seek = selected->player.IsValid() && !selected->streaming;
        audio_editor::TransportStripStyle transport_style{};
        transport_style.button_width = 78.0F;
        transport_style.button_height = 36.0F;
        transport_style.stop_before_play = true;
        const auto action = audio_editor::DrawTransportStrip(ui, transport, icons_, transport_style);
        if (action.toggle_play_pause) controller_->QueueTogglePlayPause();
        if (action.stop_voice) controller_->QueueStop();
        if (action.cancel_job) controller_->QueueCancel();
        if (action.seek_seconds) controller_->QueueSeek(*action.seek_seconds);
        if (!selected->player.IsValid() && !selected->durable)
            ui.Text(std::string("Job state: ") + JobLabel(selected->state), editor::ui::UiTextRole::Muted);
        if (selected->streaming)
            ui.Text("Stream preview: seek and speed unavailable.", editor::ui::UiTextRole::Muted);
    }

    void TtsEditorEditor::RenderDialogList(editor::ui::UiContext &ui, const TtsEditorView &view)
    {
        ui.Section("DIALOG LIST");
        const auto selected = std::find_if(view.entries.begin(), view.entries.end(),
            [&view](const TtsEntryView &entry) { return entry.id == view.selected_id; });
        if (ui.Button({.id = "tts.dialog.new", .label = "NEW SPEECH"}).activated)
            CopyTo(text_, std::string{});
        ui.SameLine();
        if (ui.Button({.id = "tts.dialog.duplicate", .label = "Duplicate",
                .enabled = selected != view.entries.end() && selected->durable}).activated)
            controller_->QueueDuplicate();
        ui.SameLine();
        if (ui.Button({.id = "tts.dialog.delete", .label = "Delete",
                .enabled = selected != view.entries.end() && selected->durable}).activated)
            controller_->QueueDelete();
        ui.SameLine();
        if (ui.Button({.id = "tts.dialog.import", .label = "Import WAV"}).activated)
        {
            const editor::EditorFileDialogFilter filters[]{{"WAV audio", "*.wav"}};
            for (const std::string &path : editor::PickFiles({
                    .title = "Import WAV into dialog library", .filters = filters}))
                controller_->QueueImport(path);
        }
        if (selected != view.entries.end() && selected->durable)
        {
            ui.SameLine();
            if (ui.Button({.id = "tts.dialog.export.quick", .label = "Export WAV"}).activated)
            {
                if (export_basename_[0] == '\0')
                    std::snprintf(export_basename_.data(), export_basename_.size(), "speech-%llu",
                        static_cast<unsigned long long>(selected->id));
                controller_->QueueExport(export_basename_.data());
            }
        }
        ui.SameLine();
        const auto search = ui.TextInput({.id = "tts.dialog.search", .label = "Search dialogs",
            .hint = "Search dialogs...", .width = 220.0F}, dialog_search_.data());
        if (search.changed) CopyTo(dialog_search_, search.value);

        filtered_dialog_indices_.clear();
        const std::string_view query{dialog_search_.data()};
        for (std::size_t index = 0; index < view.entries.size(); ++index)
        {
            const TtsEntryView &entry = view.entries[index];
            const std::string id = std::to_string(entry.id);
            if (ContainsCaseInsensitive(id, query) || ContainsCaseInsensitive(entry.text, query) ||
                ContainsCaseInsensitive(entry.voice_name, query) ||
                ContainsCaseInsensitive(JobLabel(entry.state), query))
                filtered_dialog_indices_.push_back(index);
        }
        ui.Text(std::to_string(filtered_dialog_indices_.size()) + " / " +
            std::to_string(view.entries.size()) + " dialogs", editor::ui::UiTextRole::Muted);
        if (filtered_dialog_indices_.empty())
            ui.Text(view.entries.empty() ? "No dialogs yet. Enter speech text and generate."
                                         : "No dialogs match this search.",
                    editor::ui::UiTextRole::Muted);
        if (!filtered_dialog_indices_.empty())
        {
            const editor::ui::UiTableColumn columns[] = {{"#", 44.0F}, {"Text", 0.0F},
                {"Voice", 88.0F}, {"Duration", 74.0F}, {"State", 90.0F}, {"", 66.0F}};
            const float table_height = std::clamp(
                42.0F + static_cast<float>(filtered_dialog_indices_.size()) * 30.0F,
                150.0F, 470.0F);
            ui.Table("tts.dialog.table", columns, table_height, true, [&]
            {
                for (const std::size_t index : filtered_dialog_indices_)
                {
                    const TtsEntryView &entry = view.entries[index];
                    const bool is_selected = entry.id == view.selected_id;
                    const std::string prefix = "tts.dialog." + std::to_string(entry.id);
                    const editor::ui::UiTextRole row_role = is_selected
                        ? editor::ui::UiTextRole::Accent : editor::ui::UiTextRole::Primary;
                    ui.TableRow(is_selected);
                    ui.TableColumn();
                    editor::ui::UiControlDescriptor select_control{};
                    const std::string select_id = prefix + ".select";
                    select_control.id = select_id;
                    char row_label[24]{};
                    std::snprintf(row_label, sizeof(row_label), "%03llu",
                        static_cast<unsigned long long>(entry.id));
                    select_control.label = row_label;
                    select_control.span_row = true;
                    if (ui.Selectable(select_control, is_selected).activated)
                        controller_->QueueSelect(entry.id);

                    ui.TableColumn();
                    ui.Text(CompactDialogText(entry.text, 112), row_role);
                    ui.TableColumn();
                    ui.Text(entry.voice_name, row_role);
                    ui.TableColumn();
                    const std::optional<float> duration = entry.preview_data
                        ? std::optional<float>{entry.preview_data->duration_seconds}
                        : entry.duration_seconds;
                    ui.Text(FormatDuration(duration), row_role);
                    ui.TableColumn();
                    ui.Text(JobLabel(entry.state), row_role);
                    ui.TableColumn();
                    const bool cancellable = entry.job.IsValid() &&
                        entry.state != tts::TTSJobState::Completed &&
                        entry.state != tts::TTSJobState::Cancelled &&
                        entry.state != tts::TTSJobState::Failed;
                    const bool playable = entry.durable || entry.player.IsValid();
                    if (cancellable)
                    {
                        const std::string cancel_id = prefix + ".cancel";
                        editor::ui::UiControlDescriptor cancel_control{};
                        cancel_control.id = cancel_id;
                        cancel_control.label = "CANCEL";
                        if (ui.Button(cancel_control).activated)
                        {
                            controller_->QueueSelect(entry.id);
                            controller_->QueueCancel();
                        }
                    }
                    else if (playable)
                    {
                        const bool playing = entry.audio_state == audio::AudioState::Playing ||
                            entry.audio_state == audio::AudioState::Buffering;
                        const std::string play_id = prefix + ".play";
                        editor::ui::UiControlDescriptor play_control{};
                        play_control.id = play_id;
                        play_control.label = playing ? "PAUSE" : "PLAY";
                        if (ui.Button(play_control).activated)
                        {
                            controller_->QueueSelect(entry.id);
                            controller_->QueueTogglePlayPause();
                        }
                    }
                }
            });
        }
        if (selected != view.entries.end() && selected->durable)
        {
            if (export_basename_[0] == '\0')
                std::snprintf(export_basename_.data(), export_basename_.size(), "speech-%llu",
                    static_cast<unsigned long long>(selected->id));
            const auto basename = ui.TextInput({.id = "tts.dialog.export_name",
                .label = "Export file name", .width = 220.0F}, export_basename_.data());
            if (basename.changed) CopyTo(export_basename_, basename.value);
            ui.SameLine();
            if (ui.Button({.id = "tts.dialog.export", .label = "Export WAV"}).activated)
                controller_->QueueExport(export_basename_.data());
        }
        if (!view.last_export_path.empty()) ui.WrappedText("Exported: " + view.last_export_path);
        if (!view.error.empty()) ui.Text(view.error, editor::ui::UiTextRole::Error);
    }

    void TtsEditorEditor::RenderSettings(editor::ui::UiContext &ui, const TtsEditorView &view)
    {
        ui.Section("TTS CONTROL");
        const TtsVoicePreset *selected_voice = SelectedVoice(draft_settings_);
        std::vector<editor::ui::UiChoice> voices;
        voices.reserve(draft_settings_.voices.size());
        for (const auto &voice : draft_settings_.voices)
            voices.push_back({voice.id, voice.name, voice.id});
        if (!voices.empty())
        {
            const auto voice = ui.Combo<std::string>({.id = "tts.settings.voice_preset",
                .label = "Voice preset"}, selected_voice ? selected_voice->id : voices.front().key,
                std::move(voices));
            if (voice.changed)
            {
                TtsEditorSettings updated = FieldsToSettings();
                updated.selected_voice_id = voice.value;
                CopySettingsToFields(updated);
            }
        }
        if (ui.Button({.id = "tts.settings.add_voice", .label = "Add voice",
                .enabled = draft_settings_.voices.size() < 16}).activated)
        {
            TtsEditorSettings updated = FieldsToSettings();
            std::uint32_t number = 1;
            std::string id;
            do { id = "voice-" + std::to_string(number++); }
            while (std::any_of(updated.voices.begin(), updated.voices.end(),
                [&id](const TtsVoicePreset &voice) { return voice.id == id; }));
            updated.voices.push_back({id, "New voice", {}, {}, {}});
            updated.selected_voice_id = id;
            CopySettingsToFields(updated);
        }
        ui.SameLine();
        if (ui.Button({.id = "tts.settings.remove_voice", .label = "Remove voice",
                .enabled = draft_settings_.voices.size() > 1}).activated)
        {
            TtsEditorSettings updated = FieldsToSettings();
            std::erase_if(updated.voices, [&updated](const TtsVoicePreset &voice) {
                return voice.id == updated.selected_voice_id;
            });
            updated.selected_voice_id = updated.voices.front().id;
            CopySettingsToFields(updated);
        }
        const auto edit_text = [&ui](const std::string_view id, const std::string_view label,
                                     auto &field, const bool multiline = false)
        {
            const auto result = ui.TextInput({.id = id, .label = label,
                .height = multiline ? 82.0F : 0.0F, .multiline = multiline}, field.data());
            if (result.changed) CopyTo(field, result.value);
        };
        edit_text("tts.settings.voice_name", "Voice", voice_name_);
        edit_text("tts.settings.language", "Text language", text_language_);
        const std::vector<editor::ui::UiChoice> modes{
            {"stream", "Stream", true}, {"buffer", "Buffer", false}};
        const auto mode = ui.Combo<bool>({.id = "tts.settings.playback_mode", .label = "Playback mode"},
            streaming_, modes);
        if (mode.changed) streaming_ = mode.value;
        ui.Section("SERVER AND REFERENCE");
        edit_text("tts.settings.address", "Address", address_);
        const auto port = ui.NumericInput<std::int64_t>({.id = "tts.settings.port", .label = "Port"},
            port_, 0, 65535, 1);
        if (port.changed) port_ = static_cast<int>(port.value);
        edit_text("tts.settings.api_path", "API path", api_path_);
        const auto timeout = ui.NumericInput<std::int64_t>(
            {.id = "tts.settings.timeout", .label = "Timeout (s)"}, timeout_seconds_, 1, 3600, 1);
        if (timeout.changed) timeout_seconds_ = static_cast<int>(timeout.value);
        edit_text("tts.settings.ref_audio", "Reference audio (server path)", ref_audio_path_);
        edit_text("tts.settings.ref_text", "Reference text", ref_text_, true);
        edit_text("tts.settings.ref_language", "Reference language", ref_language_);
        edit_text("tts.settings.output_directory", "Output folder", output_directory_);
        if (ui.Button({.id = "tts.settings.save", .label = "Save settings",
                .enabled = view.settings_loaded}).activated)
            controller_->QueueSettings(FieldsToSettings());
        ui.SameLine();
        if (ui.Button({.id = "tts.settings.reload", .label = "Reload"}).activated)
        {
            controller_->QueueReloadSettings();
            refresh_fields_ = true;
        }
        if (!view.settings_loaded)
            ui.Text("Settings file needs repair before saving.", editor::ui::UiTextRole::Error);
        else if (!view.generation_blocker.empty())
            ui.Text(view.generation_blocker, editor::ui::UiTextRole::Muted);
        ui.Text("Changes apply to new speech jobs after Save.", editor::ui::UiTextRole::Muted);
    }

    void TtsEditorEditor::RenderTextInput(editor::ui::UiContext &ui, const TtsEditorView &view)
    {
        ui.Section("TEXT INPUT");
        const auto text = ui.TextInput({.id = "tts.text.input", .label = "Speech text",
            .hint = "Enter text to synthesize", .height = 136.0F,
            .multiline = true}, text_.data());
        if (text.changed) CopyTo(text_, text.value);
        ui.Text(std::to_string(std::strlen(text_.data())) + " / 4096 bytes",
                editor::ui::UiTextRole::Muted);
        const bool can_submit = view.can_generate && text_[0] != '\0';
        if (ui.Button({.id = "tts.text.generate", .label = "Generate speech",
                .enabled = can_submit, .height = 34.0F}).activated)
            controller_->QueueGenerate(text_.data());
        const auto selected = std::find_if(view.entries.begin(), view.entries.end(),
            [&view](const TtsEntryView &entry) { return entry.id == view.selected_id; });
        if (!view.error.empty()) ui.Text(view.error, editor::ui::UiTextRole::Error);
        else if (selected != view.entries.end() && !selected->error.empty())
            ui.Text(selected->error, editor::ui::UiTextRole::Error);
        else if (!view.status.empty()) ui.Text(view.status, editor::ui::UiTextRole::Muted);
        ui.Section("OUTPUT");
        ui.WrappedText(std::string("Output folder: ") + output_directory_.data());
    }

    void TtsEditorEditor::RenderWorkspace(editor::ui::UiContext &ui)
    {
        if (!controller_) return;
        const editor::ui::UiViewportInfo &viewport = ui.Viewport();
        const TtsEditorView view = controller_->GetView();
        if (refresh_fields_ && view.status == "Settings reloaded")
        {
            CopySettingsToFields(view.settings);
            refresh_fields_ = false;
        }
        editor::ui::UiTheme theme{};
        theme.accent = kCyan;
        theme.font = editor::ui::UiFontRole::Code;
        const auto theme_scope = ui.Theme(theme);
        {
            const auto window = ui.Window({.id = "tts.workspace.main", .label = "TTS Tool"},
                viewport.logical_origin,
                {viewport.logical_extent.x * 0.69F, viewport.logical_extent.y});
            if (window.ContentVisible())
            {
                ui.SetHostContainer({.id = editor::ui::StableUiNodeId("tts.workspace.main.content"),
                    .role = editor::ui::UiRole::Group, .label = "TTS Tool"});
                ui.Section("TTS TOOL");
                RenderPreview(ui, view);
                RenderDialogList(ui, view);
            }
        }
        {
            const editor::ui::UiVec2 control_origin{
                viewport.logical_origin.x + viewport.logical_extent.x * 0.69F,
                viewport.logical_origin.y};
            const auto window = ui.Window(
                {.id = "tts.workspace.controls", .label = "TTS Control"},
                control_origin,
                {viewport.logical_extent.x * 0.31F, viewport.logical_extent.y});
            if (!window.ContentVisible()) return;
            ui.SetHostContainer({.id = editor::ui::StableUiNodeId("tts.workspace.controls.content"),
                .role = editor::ui::UiRole::Group, .label = "TTS Control"});
            RenderSettings(ui, view);
            RenderTextInput(ui, view);
        }
    }
}
