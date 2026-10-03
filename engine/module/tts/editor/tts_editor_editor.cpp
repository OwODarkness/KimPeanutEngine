#include "tts_editor_editor.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <exception>
#include <optional>
#include <string_view>

#include <imgui.h>

#include "editor/ui/audio_transport_strip.h"
#include "editor/ui/editor_ui.h"
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
        const ImVec4 kCyan{0.18f, 0.83f, 0.94f, 1.0f};
        const ImVec4 kMuted{0.58f, 0.7f, 0.75f, 1.0f};
        const ImVec4 kText{0.86f, 0.95f, 0.97f, 1.0f};
        const ImVec4 kError{1.0f, 0.42f, 0.36f, 1.0f};
        constexpr ImU32 kSelectedAmber = IM_COL32(255, 184, 58, 255);
        constexpr ImU32 kSelectedRow = IM_COL32(51, 39, 17, 255);
        constexpr ImU32 kSelectedRowHovered = IM_COL32(70, 49, 18, 255);

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

        editor::TransportPlaybackState TransportState(const TtsEntryView &entry,
                                                       const audio::AudioState audio_state)
        {
            if (entry.player.IsValid())
            {
                switch (audio_state)
                {
                case audio::AudioState::Playing: return editor::TransportPlaybackState::Playing;
                case audio::AudioState::Buffering: return editor::TransportPlaybackState::Buffering;
                case audio::AudioState::Paused: return editor::TransportPlaybackState::Paused;
                case audio::AudioState::Finished: return editor::TransportPlaybackState::Finished;
                case audio::AudioState::Cancelled: return editor::TransportPlaybackState::Cancelled;
                default: return editor::TransportPlaybackState::Ready;
                }
            }
            switch (entry.state)
            {
            case tts::TTSJobState::Queued: return editor::TransportPlaybackState::Queued;
            case tts::TTSJobState::Connecting:
            case tts::TTSJobState::Generating: return editor::TransportPlaybackState::Generating;
            case tts::TTSJobState::Buffering: return editor::TransportPlaybackState::Buffering;
            case tts::TTSJobState::Playing: return editor::TransportPlaybackState::Playing;
            case tts::TTSJobState::Draining: return editor::TransportPlaybackState::Draining;
            case tts::TTSJobState::Completed: return editor::TransportPlaybackState::Finished;
            case tts::TTSJobState::Cancelled: return editor::TransportPlaybackState::Cancelled;
            case tts::TTSJobState::Failed: return editor::TransportPlaybackState::Failed;
            }
            return editor::TransportPlaybackState::Idle;
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
        transport_icons_ = editor::LoadAudioTransportIconMasks();
        const auto avatar = image_io::DecodeImageFile(
            (project_root / "resouce/icon/tts/kurisu-avatar.jpg").string());
        if (avatar.result.success && avatar.image.IsValid() &&
            avatar.image.format == image_io::ImagePixelFormat::Rgba8)
        {
            constexpr std::uint32_t target_size =
                static_cast<std::uint32_t>(kAvatarRasterSize);
            const std::uint32_t side = std::min(avatar.image.width, avatar.image.height);
            const std::uint32_t crop_x = (avatar.image.width - side) / 2;
            const std::uint32_t crop_y = (avatar.image.height - side) / 2;
            for (std::uint32_t y = 0; y < target_size; ++y)
            {
                const std::uint32_t source_y = crop_y +
                    (target_size - 1 - y) * side / target_size;
                for (std::uint32_t x = 0; x < target_size; ++x)
                {
                    const std::uint32_t source_x = crop_x + x * side / target_size;
                    const std::size_t offset =
                        (static_cast<std::size_t>(source_y) * avatar.image.width + source_x) * 4;
                    const auto *rgba = avatar.image.pixels.data() + offset;
                    avatar_pixels_[y * target_size + x] = IM_COL32(
                        rgba[0], rgba[1], rgba[2], rgba[3]);
                }
            }
            avatar_loaded_ = true;
        }
        const TtsEditorView initial_view = controller.GetView();
        last_audible_volume_ = std::clamp(initial_view.volume, 0.05f, 1.0f);
        CopySettingsToFields(initial_view.settings);
        editor::EditorUIInitInfo init{};
        init.window = context.window_system_->GetNativeHandle();
        init.editor_presentation_bridge =
            context.render_system_->GetEditorPresentationBridge();
        init.log_system = context.log_system_.get();
        init.engine = &engine;
        init.render_system = context.render_system_.get();
        init.window_system = context.window_system_.get();
        try
        {
            ui_ = std::make_unique<editor::EditorUI>();
            ui_->InitializeViewer(init, [this] { RenderWorkspace(); });
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
        if (!ui_ || !ui_->Render())
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

    void TtsEditorEditor::RenderPreview(const TtsEditorView &view)
    {
        ImGui::TextColored(kCyan, "// AUDIO PREVIEW");
        ImGui::Separator();
        const auto selected = std::find_if(view.entries.begin(), view.entries.end(),
            [&view](const TtsEntryView &entry) { return entry.id == view.selected_id; });
        if (selected == view.entries.end())
        {
            ImGui::TextColored(kMuted, "Generate speech to preview it here.");
            return;
        }
        const float available_width = ImGui::GetContentRegionAvail().x;
        const float avatar_size = std::clamp(available_width * 0.2f, 112.0f, 176.0f);
        const float visual_height = std::max(avatar_size, 154.0f);
        const ImVec2 card_origin = ImGui::GetCursorScreenPos();
        const float gap = 14.0f;
        const float card_width = std::max(80.0f, available_width - avatar_size - gap);
        ImDrawList *draw = ImGui::GetWindowDrawList();

        ImGui::InvisibleButton("##tts_avatar_card", {avatar_size, visual_height});
        const ImVec2 avatar_min = ImGui::GetItemRectMin();
        const ImVec2 avatar_max = ImGui::GetItemRectMax();
        draw->AddRectFilled(avatar_min, avatar_max, IM_COL32(5, 18, 23, 255), 3.0f);
        if (avatar_loaded_)
        {
            const float inset = 4.0f;
            const float tile = (avatar_size - inset * 2.0f) /
                static_cast<float>(kAvatarRasterSize);
            for (std::uint32_t y = 0; y < kAvatarRasterSize; ++y)
            {
                for (std::uint32_t x = 0; x < kAvatarRasterSize; ++x)
                {
                    const float x0 = avatar_min.x + inset + x * tile;
                    const float y0 = avatar_min.y + inset + y * tile;
                    draw->AddRectFilled({x0, y0}, {x0 + tile + 0.4f, y0 + tile + 0.4f},
                                        avatar_pixels_[y * kAvatarRasterSize + x]);
                }
            }
        }
        else
            draw->AddText({avatar_min.x + 12.0f, avatar_min.y + avatar_size * 0.5f},
                          IM_COL32(48, 211, 239, 255), "TTS VOICE");
        draw->AddRect(avatar_min, avatar_max, IM_COL32(37, 190, 218, 255), 3.0f, 0, 1.5f);
        ImGui::SameLine(0.0f, gap);
        const ImVec2 wave_origin{card_origin.x + avatar_size + gap, card_origin.y};
        const ImVec2 wave_size{card_width, visual_height};
        ImGui::SetCursorScreenPos(wave_origin);
        ImGui::InvisibleButton("##tts_waveform", wave_size);
        const ImVec2 wave_min = ImGui::GetItemRectMin();
        const ImVec2 wave_max = ImGui::GetItemRectMax();
        draw->AddRectFilled(wave_min, wave_max, IM_COL32(3, 14, 19, 255), 3.0f);
        draw->AddRect(wave_min, wave_max, IM_COL32(17, 97, 115, 255), 3.0f);
        draw->AddText({wave_min.x + 10.0f, wave_min.y + 8.0f},
                      IM_COL32(48, 211, 239, 255), selected->voice_name.c_str());
        const char *mode = selected->streaming ? "STREAM" : "BUFFER";
        const ImVec2 mode_size = ImGui::CalcTextSize(mode);
        draw->AddText({wave_max.x - mode_size.x - 10.0f, wave_min.y + 8.0f},
                      IM_COL32(125, 184, 195, 255), mode);
        constexpr float text_top = 30.0f;
        constexpr float text_bottom = 56.0f;
        const float plot_left = wave_min.x + 10.0f;
        const float plot_right = wave_max.x - 10.0f;
        const float plot_top = wave_min.y + text_bottom;
        const float plot_bottom = wave_max.y - 18.0f;
        const float plot_center = (plot_top + plot_bottom) * 0.5f;
        draw->AddLine({plot_left, plot_center}, {plot_right, plot_center},
                      IM_COL32(17, 97, 115, 255), 1.0f);
        for (float x = plot_left; x < plot_right; x += 16.0f)
            draw->AddLine({x, plot_top}, {x, plot_bottom},
                          IM_COL32(10, 48, 59, 255), 1.0f);

        static const std::vector<float> empty_waveform;
        const auto &wave = selected->preview_data
            ? selected->preview_data->waveform_samples : empty_waveform;
        const float duration = selected->preview_data
            ? selected->preview_data->duration_seconds
            : view.duration_seconds.value_or(0.0f);
        const float played = duration > 0.0f
            ? std::clamp(view.elapsed_seconds / duration, 0.0f, 1.0f) : 0.0f;
        if (!wave.empty() && plot_right > plot_left)
        {
            const float spacing = std::max(2.0f, (plot_right - plot_left) /
                static_cast<float>(std::min<std::size_t>(wave.size(), 180)));
            const std::size_t visible_count = std::min<std::size_t>(wave.size(),
                static_cast<std::size_t>((plot_right - plot_left) / spacing));
            for (std::size_t i = 0; i < visible_count; ++i)
            {
                const std::size_t sample = i * wave.size() / visible_count;
                const float x = plot_left + (static_cast<float>(i) + 0.5f) * spacing;
                const float amplitude = std::clamp(wave[sample], 0.04f, 1.0f) *
                    (plot_bottom - plot_top) * 0.48f;
                const bool is_played = duration > 0.0f &&
                    x <= plot_left + played * (plot_right - plot_left);
                const ImU32 color = is_played ? IM_COL32(255, 176, 48, 255)
                                               : IM_COL32(34, 198, 229, 255);
                draw->AddLine({x, plot_center - amplitude},
                              {x, plot_center + amplitude}, color, 2.0f);
            }
            if (duration > 0.0f)
            {
                const float playhead_x = plot_left + played * (plot_right - plot_left);
                draw->AddLine({playhead_x, plot_top}, {playhead_x, plot_bottom},
                              IM_COL32(255, 176, 48, 255), 1.5f);
            }
            if (ImGui::IsItemHovered() && selected->player.IsValid() && !selected->streaming &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left) && duration > 0.0f)
            {
                const float fraction = std::clamp((ImGui::GetIO().MousePos.x - plot_left) /
                    (plot_right - plot_left), 0.0f, 1.0f);
                controller_->QueueSeek(fraction * duration);
            }
        }
        else
        {
            const char *waiting = selected->streaming
                ? "Waveform available when the server WAV arrives"
                : "Waveform will appear when WAV audio is ready";
            draw->AddText({plot_left, plot_center - 7.0f},
                          IM_COL32(125, 184, 195, 255), waiting);
        }
        if (!selected->text.empty())
        {
            std::string preview_text = selected->text;
            if (preview_text.size() > 112)
            {
                std::size_t end = 109;
                while (end > 0 &&
                    (static_cast<unsigned char>(preview_text[end]) & 0xC0u) == 0x80u)
                    --end;
                preview_text.resize(end);
                preview_text += "...";
            }
            const ImVec2 text_pos{wave_min.x + 10.0f, wave_min.y + text_top};
            draw->AddText(text_pos, IM_COL32(220, 235, 238, 255), preview_text.c_str());
        }
        const auto time_text = [duration](const float seconds)
        {
            char value[48]{};
            std::snprintf(value, sizeof(value), "%02d:%02d / %02d:%02d",
                static_cast<int>(seconds) / 60, static_cast<int>(seconds) % 60,
                static_cast<int>(duration) / 60, static_cast<int>(duration) % 60);
            return std::string(value);
        };
        const std::string timing = duration > 0.0f
            ? time_text(view.elapsed_seconds) : std::string("DURATION PENDING");
        const ImVec2 timing_size = ImGui::CalcTextSize(timing.c_str());
        draw->AddText({wave_max.x - timing_size.x - 10.0f, wave_max.y - 16.0f},
                      IM_COL32(125, 184, 195, 255), timing.c_str());
        ImGui::SetCursorScreenPos({card_origin.x, card_origin.y + visual_height});
        if (!selected->error.empty())
            ImGui::TextColored(kError, "%s", selected->error.c_str());
        ImGui::Spacing();
        const auto draw_preview_controls = [&]()
        {
            const bool can_change_rate = !selected->streaming && selected->durable;
            if (can_change_rate)
            {
                ImGui::SameLine(0.0f, 12.0f);
                const float rates[] = {0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f};
                const char *rate_labels[] = {
                    "0.5x", "0.75x", "1.0x", "1.25x", "1.5x", "2.0x"};
                int selected_rate = 2;
                for (int i = 0; i < 6; ++i)
                {
                    if (std::abs(view.playback_rate - rates[i]) < 0.001f)
                    {
                        selected_rate = i;
                        break;
                    }
                }
                ImGui::TextUnformatted("SPEED");
                ImGui::SameLine(0.0f, 8.0f);
                ImGui::SetNextItemWidth(132.0f);
                if (ImGui::Combo("##tts_preview_rate", &selected_rate,
                                 rate_labels, 6))
                    controller_->QueuePlaybackRate(rates[selected_rate]);
            }

            ImGui::SameLine(0.0f, 12.0f);
            const bool muted = view.volume <= 0.001f;
            if (!muted)
                last_audible_volume_ = view.volume;
            const editor::EditorControlIcon voice_icon = transport_icons_.Get(
                muted ? editor::AudioTransportIcon::VoiceClosed :
                        editor::AudioTransportIcon::VoiceOpen);
            const ImVec2 mute_button_size{60.0f, 36.0f};
            const bool toggle_mute = ImGui::Button(
                muted ? "##tts_unmute" : "##tts_mute", mute_button_size);
            const ImVec2 button_min = ImGui::GetItemRectMin();
            const ImVec2 button_max = ImGui::GetItemRectMax();
            constexpr float icon_size = 18.0f;
            const ImVec2 icon_origin{
                button_min.x + (mute_button_size.x - icon_size) * 0.5f,
                button_min.y + (mute_button_size.y - icon_size) * 0.5f};
            editor::DrawEditorControlIcon(voice_icon,
                icon_origin, icon_size,
                IM_COL32(48, 211, 239, 255));
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", muted ? "Unmute voice" : "Mute voice");
            if (toggle_mute)
                controller_->QueueVolume(muted ? last_audible_volume_ : 0.0f);
            ImGui::SameLine(0.0f, 12.0f);
            const float percent_width = ImGui::CalcTextSize("100%").x;
            const float volume_width = std::max(36.0f,
                ImGui::GetContentRegionAvail().x - 10.0f - percent_width);
            const editor::AudioVolumeProgressStyle volume_style{};
            if (const auto volume = editor::DrawAudioVolumeProgressControl(
                    "##tts_volume_progress", view.volume, volume_width, volume_style))
                controller_->QueueVolume(*volume);
            ImGui::Unindent(10.0f);
            ImGui::Dummy({0.0f, 8.0f});
        };
        if (!selected->player.IsValid())
        {
            if (selected->durable)
            {
                ImGui::TextColored(kMuted, "Saved WAV ready for buffer preview.");
                editor::TransportStripState saved_transport{};
                saved_transport.instance_id = "tts_saved_preview";
                saved_transport.playback_state = editor::TransportPlaybackState::Finished;
                saved_transport.capabilities.toggle_play_pause = true;
                saved_transport.icons = transport_icons_.GetTransportIcons();
                editor::TransportStripStyle transport_style{};
                transport_style.button_width = 78.0f;
                transport_style.button_height = 36.0f;
                transport_style.stop_before_play = true;
                ImGui::Indent(10.0f);
                ImGui::Dummy({0.0f, 8.0f});
                const auto action = editor::DrawTransportStrip(saved_transport,
                                                                transport_style);
                if (action.toggle_play_pause)
                    controller_->QueueTogglePlayPause();
                draw_preview_controls();
                if (!selected->error.empty()) ImGui::TextColored(kError, "%s", selected->error.c_str());
                return;
            }
            ImGui::Text("Job state: %s", JobLabel(selected->state));
            if (selected->state == tts::TTSJobState::Playing ||
                selected->state == tts::TTSJobState::Draining)
                ImGui::TextColored(kMuted,
                    "Audio is playing; transport appears after the server response ends.");
            if (selected->job.IsValid() &&
                selected->state != tts::TTSJobState::Cancelled &&
                selected->state != tts::TTSJobState::Failed &&
                selected->state != tts::TTSJobState::Completed &&
                ImGui::Button("CANCEL GENERATION"))
                controller_->QueueCancel();
            return;
        }
        editor::TransportStripState transport{};
        transport.instance_id = "tts_preview";
        transport.playback_state = TransportState(*selected, view.audio_state);
        transport.elapsed_seconds = view.elapsed_seconds;
        transport.duration_seconds = view.duration_seconds;
        transport.capabilities.toggle_play_pause = selected->player.IsValid() &&
            (!selected->streaming || view.audio_state != audio::AudioState::Finished ||
             selected->durable);
        transport.capabilities.stop_voice = selected->player.IsValid() &&
            selected->state == tts::TTSJobState::Completed;
        transport.capabilities.cancel_job = selected->job.IsValid() &&
            selected->state != tts::TTSJobState::Completed &&
            selected->state != tts::TTSJobState::Cancelled &&
            selected->state != tts::TTSJobState::Failed;
        transport.capabilities.seek = selected->player.IsValid() && !selected->streaming;
        transport.icons = transport_icons_.GetTransportIcons();
        editor::TransportStripStyle transport_style{};
        transport_style.button_width = 78.0f;
        transport_style.button_height = 36.0f;
        transport_style.stop_before_play = true;
        ImGui::Indent(10.0f);
        ImGui::Dummy({0.0f, 8.0f});
        const auto action = editor::DrawTransportStrip(transport, transport_style);
        if (action.toggle_play_pause) controller_->QueueTogglePlayPause();
        if (action.stop_voice) controller_->QueueStop();
        if (action.cancel_job) controller_->QueueCancel();
        if (action.seek_seconds) controller_->QueueSeek(*action.seek_seconds);
        draw_preview_controls();
        if (selected->streaming)
            ImGui::TextColored(kMuted, "Stream preview: seek and speed unavailable.");
    }

    void TtsEditorEditor::RenderDialogList(const TtsEditorView &view)
    {
        ImGui::TextColored(kCyan, "// DIALOG LIST");
        ImGui::Separator();
        const auto selected = std::find_if(view.entries.begin(), view.entries.end(),
            [&view](const TtsEntryView &entry) { return entry.id == view.selected_id; });

        if (ImGui::Button("NEW SPEECH"))
            focus_text_input_ = true;
        ImGui::SameLine();
        ImGui::BeginDisabled(selected == view.entries.end() || !selected->durable);
        if (ImGui::Button("DUPLICATE")) controller_->QueueDuplicate();
        ImGui::SameLine();
        if (ImGui::Button("DELETE")) controller_->QueueDelete();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("IMPORT WAV"))
            ImGui::OpenPopup("##tts_import_wav_popup");
        ImGui::SameLine();
        ImGui::BeginDisabled(selected == view.entries.end() || !selected->durable);
        if (ImGui::Button("EXPORT WAV"))
        {
            std::snprintf(export_basename_.data(), export_basename_.size(),
                          "speech-%llu", static_cast<unsigned long long>(selected->id));
            ImGui::OpenPopup("##tts_export_wav_popup");
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        const float search_width = std::clamp(ImGui::GetContentRegionAvail().x * 0.42f,
                                              150.0f, 300.0f);
        ImGui::SetNextItemWidth(search_width);
        ImGui::InputTextWithHint("##tts_dialog_search", "Search dialogs...",
                                 dialog_search_.data(), dialog_search_.size());

        if (ImGui::BeginPopup("##tts_import_wav_popup"))
        {
            ImGui::TextUnformatted("Import a WAV into the dialog library");
            ImGui::SetNextItemWidth(340.0f);
            ImGui::InputTextWithHint("##import_wav_path", "WAV file path",
                                     import_path_.data(), import_path_.size());
            if (ImGui::Button("IMPORT"))
            {
                controller_->QueueImport(import_path_.data());
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("CANCEL")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if (ImGui::BeginPopup("##tts_export_wav_popup"))
        {
            ImGui::TextUnformatted("Export selected dialog as WAV");
            ImGui::SetNextItemWidth(240.0f);
            ImGui::InputText("File name", export_basename_.data(), export_basename_.size());
            if (ImGui::Button("EXPORT"))
            {
                controller_->QueueExport(export_basename_.data());
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("CANCEL")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        filtered_dialog_indices_.clear();
        const std::string_view query{dialog_search_.data()};
        for (std::size_t index = 0; index < view.entries.size(); ++index)
        {
            const TtsEntryView &entry = view.entries[index];
            const std::string_view state = JobLabel(entry.state);
            char id_text[24]{};
            std::snprintf(id_text, sizeof(id_text), "%llu",
                          static_cast<unsigned long long>(entry.id));
            if (ContainsCaseInsensitive(id_text, query) ||
                ContainsCaseInsensitive(entry.text, query) ||
                ContainsCaseInsensitive(entry.voice_name, query) ||
                ContainsCaseInsensitive(state, query))
                filtered_dialog_indices_.push_back(index);
        }
        ImGui::TextColored(kMuted, "%zu / %zu dialogs",
                          filtered_dialog_indices_.size(), view.entries.size());
        if (view.entries.empty())
        {
            ImGui::TextColored(kMuted,
                "No dialogs yet. Enter speech text and choose Generate Speech.");
            return;
        }
        if (filtered_dialog_indices_.empty())
        {
            ImGui::TextColored(kMuted, "No dialogs match this search.");
            return;
        }
        const float max_table_height = std::clamp(
            ImGui::GetContentRegionAvail().y * 0.62f, 220.0f, 470.0f);
        const float table_height = std::min(max_table_height,
            std::max(150.0f, 43.0f + filtered_dialog_indices_.size() * 30.0f));
        const ImVec2 table_origin = ImGui::GetCursorScreenPos();
        const float cell_padding_y = ImGui::GetStyle().CellPadding.y;
        const float header_height = ImGui::GetTextLineHeight() + cell_padding_y * 2.0f;
        if (ImGui::BeginTable("##dialogs", 6,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
            ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
            ImVec2(0.0f, table_height)))
        {
            ImDrawList *const table_draw_list = ImGui::GetWindowDrawList();
            ImVec2 body_clip_min = table_draw_list->GetClipRectMin();
            const ImVec2 body_clip_max = table_draw_list->GetClipRectMax();
            body_clip_min.y = std::max(body_clip_min.y, table_origin.y + header_height);
            ImVec2 selection_min{};
            ImVec2 selection_max{};
            bool selection_visible = false;
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 44.0f);
            ImGui::TableSetupColumn("Text", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Voice", ImGuiTableColumnFlags_WidthFixed, 88.0f);
            ImGui::TableSetupColumn("Duration", ImGuiTableColumnFlags_WidthFixed, 74.0f);
            ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 66.0f);
            ImGui::TableHeadersRow();
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(filtered_dialog_indices_.size()), 30.0f);
            while (clipper.Step())
            {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
                {
                    const auto &entry = view.entries[
                        filtered_dialog_indices_[static_cast<std::size_t>(row)]];
                    const std::string row_id = std::to_string(entry.id);
                    ImGui::PushID(row_id.c_str());
                    ImGui::TableNextRow(ImGuiTableRowFlags_None, 30.0f);
                    const bool is_selected = entry.id == view.selected_id;
                    if (is_selected)
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, kSelectedRow);
                    ImGui::TableSetColumnIndex(0);
                    char row_label[24]{};
                    std::snprintf(row_label, sizeof(row_label), "%03llu",
                                  static_cast<unsigned long long>(entry.id));
                    const float row_top = ImGui::GetCursorScreenPos().y -
                        ImGui::GetStyle().CellPadding.y;
                    ImGui::PushStyleColor(ImGuiCol_Text,
                        is_selected ? ImGui::ColorConvertU32ToFloat4(kSelectedAmber) : kCyan);
                    ImGui::PushStyleColor(ImGuiCol_Header,
                        ImGui::ColorConvertU32ToFloat4(kSelectedRow));
                    ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
                        ImGui::ColorConvertU32ToFloat4(is_selected ?
                            kSelectedRowHovered : IM_COL32(7, 32, 42, 255)));
                    const bool clicked = ImGui::Selectable(row_label, is_selected,
                        ImGuiSelectableFlags_SpanAllColumns |
                        ImGuiSelectableFlags_AllowDoubleClick);
                    if (is_selected)
                    {
                        selection_min = ImVec2(ImGui::GetItemRectMin().x + 1.0f, row_top);
                        selection_max = ImVec2(ImGui::GetItemRectMax().x - 1.0f,
                                               row_top + 30.0f);
                        selection_visible = true;
                    }
                    ImGui::PopStyleColor(3);
                    if (clicked)
                    {
                        controller_->QueueSelect(entry.id);
                        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        {
                            controller_->QueueTogglePlayPause();
                        }
                    }
                    ImGui::TableSetColumnIndex(1);
                    const auto text_capacity = std::max(12, static_cast<int>(
                        (ImGui::GetColumnWidth() - 20.0f) / ImGui::GetFontSize()));
                    const std::string row_text = CompactDialogText(entry.text,
                        static_cast<std::size_t>(text_capacity));
                    ImGui::TextColored(is_selected ?
                        ImGui::ColorConvertU32ToFloat4(kSelectedAmber) : kText,
                        "%s", row_text.c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextColored(is_selected ?
                        ImGui::ColorConvertU32ToFloat4(kSelectedAmber) : kText,
                        "%s", entry.voice_name.c_str());
                    ImGui::TableSetColumnIndex(3);
                    std::optional<float> duration;
                    if (entry.preview_data)
                        duration = entry.preview_data->duration_seconds;
                    else if (entry.duration_seconds)
                        duration = entry.duration_seconds;
                    else if (entry.id == view.selected_id)
                        duration = view.duration_seconds;
                    ImGui::TextColored(is_selected ?
                        ImGui::ColorConvertU32ToFloat4(kSelectedAmber) : kText,
                        "%s", FormatDuration(duration).c_str());
                    ImGui::TableSetColumnIndex(4);
                    ImGui::TextColored(is_selected ?
                        ImGui::ColorConvertU32ToFloat4(kSelectedAmber) : kText,
                        "%s", JobLabel(entry.state));
                    ImGui::TableSetColumnIndex(5);
                    const bool can_cancel = entry.job.IsValid() &&
                        entry.state != tts::TTSJobState::Completed &&
                        entry.state != tts::TTSJobState::Cancelled &&
                        entry.state != tts::TTSJobState::Failed;
                    const bool can_play = entry.durable || entry.player.IsValid();
                    if (can_cancel)
                    {
                        if (is_selected)
                        {
                            ImGui::PushStyleColor(ImGuiCol_Button,
                                ImGui::ColorConvertU32ToFloat4(kSelectedRow));
                            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                ImGui::ColorConvertU32ToFloat4(kSelectedRowHovered));
                            ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                                ImGui::ColorConvertU32ToFloat4(kSelectedRowHovered));
                            ImGui::PushStyleColor(ImGuiCol_Text,
                                ImGui::ColorConvertU32ToFloat4(kSelectedAmber));
                        }
                        if (ImGui::SmallButton("CANCEL"))
                        {
                            controller_->QueueSelect(entry.id);
                            controller_->QueueCancel();
                        }
                        if (is_selected) ImGui::PopStyleColor(4);
                    }
                    else if (can_play)
                    {
                        const bool playing = entry.audio_state == audio::AudioState::Playing ||
                            entry.audio_state == audio::AudioState::Buffering;
                        if (is_selected)
                        {
                            ImGui::PushStyleColor(ImGuiCol_Button,
                                ImGui::ColorConvertU32ToFloat4(kSelectedRow));
                            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                ImGui::ColorConvertU32ToFloat4(kSelectedRowHovered));
                            ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                                ImGui::ColorConvertU32ToFloat4(kSelectedRowHovered));
                            ImGui::PushStyleColor(ImGuiCol_Text,
                                ImGui::ColorConvertU32ToFloat4(kSelectedAmber));
                        }
                        if (ImGui::SmallButton(playing ? "PAUSE" : "PLAY"))
                        {
                            controller_->QueueSelect(entry.id);
                            controller_->QueueTogglePlayPause();
                        }
                        if (is_selected) ImGui::PopStyleColor(4);
                    }
                    else
                        ImGui::TextColored(kMuted, "—");
                    ImGui::PopID();
                }
            }
            ImGui::EndTable();
            if (selection_visible && body_clip_max.y > body_clip_min.y)
            {
                table_draw_list->PushClipRect(body_clip_min, body_clip_max, true);
                table_draw_list->AddRect(selection_min, selection_max, kSelectedAmber);
                table_draw_list->PopClipRect();
            }
        }
        if (!view.last_export_path.empty())
            ImGui::TextWrapped("Exported: %s", view.last_export_path.c_str());
        if (!view.error.empty()) ImGui::TextColored(kError, "%s", view.error.c_str());
    }

    void TtsEditorEditor::RenderSettings(const TtsEditorView &view)
    {
        ImGui::TextColored(kCyan, "// TTS CONTROL");
        ImGui::Separator();
        const TtsVoicePreset *selected_voice = SelectedVoice(draft_settings_);
        std::optional<std::string> chosen_voice;
        if (ImGui::BeginCombo("Voice preset", selected_voice ?
            selected_voice->name.c_str() : "Select voice"))
        {
            for (const auto &voice : draft_settings_.voices)
            {
                if (ImGui::Selectable(voice.name.c_str(),
                                      voice.id == draft_settings_.selected_voice_id))
                    chosen_voice = voice.id;
            }
            ImGui::EndCombo();
        }
        if (chosen_voice)
        {
            TtsEditorSettings updated = FieldsToSettings();
            updated.selected_voice_id = *chosen_voice;
            CopySettingsToFields(updated);
        }
        ImGui::BeginDisabled(draft_settings_.voices.size() >= 16);
        if (ImGui::SmallButton("ADD VOICE"))
        {
            TtsEditorSettings updated = FieldsToSettings();
            std::uint32_t number = 1;
            std::string id;
            do
            {
                id = "voice-" + std::to_string(number++);
            } while (std::any_of(updated.voices.begin(), updated.voices.end(),
                [&id](const TtsVoicePreset &voice) { return voice.id == id; }));
            updated.voices.push_back({id, "New voice", {}, {}, {}});
            updated.selected_voice_id = id;
            CopySettingsToFields(updated);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(draft_settings_.voices.size() <= 1);
        if (ImGui::SmallButton("REMOVE VOICE"))
        {
            TtsEditorSettings updated = FieldsToSettings();
            std::erase_if(updated.voices, [&updated](const TtsVoicePreset &voice) {
                return voice.id == updated.selected_voice_id;
            });
            updated.selected_voice_id = updated.voices.front().id;
            CopySettingsToFields(updated);
        }
        ImGui::EndDisabled();
        ImGui::InputText("Voice", voice_name_.data(), voice_name_.size());
        ImGui::InputText("Language", text_language_.data(), text_language_.size());
        if (ImGui::BeginCombo("Playback mode", streaming_ ? "Stream" : "Buffer"))
        {
            if (ImGui::Selectable("Stream", streaming_)) streaming_ = true;
            if (ImGui::Selectable("Buffer", !streaming_)) streaming_ = false;
            ImGui::EndCombo();
        }
        if (ImGui::CollapsingHeader("SERVER AND REFERENCE", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::InputText("Address", address_.data(), address_.size());
            ImGui::InputInt("Port", &port_);
            ImGui::InputText("API path", api_path_.data(), api_path_.size());
            ImGui::InputInt("Timeout (s)", &timeout_seconds_);
            ImGui::TextUnformatted("Reference audio (server path)");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText("##reference_audio", ref_audio_path_.data(),
                             ref_audio_path_.size());
            ImGui::TextUnformatted("Reference text");
            ImGui::InputTextMultiline("##reference_text", ref_text_.data(), ref_text_.size(),
                                      ImVec2(-1.0f, 74.0f));
            ImGui::TextUnformatted("Reference language");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText("##reference_language", ref_language_.data(),
                             ref_language_.size());
        }
        ImGui::BeginDisabled(!view.settings_loaded);
        if (ImGui::Button("SAVE SETTINGS"))
            controller_->QueueSettings(FieldsToSettings());
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("RELOAD"))
        {
            controller_->QueueReloadSettings();
            refresh_fields_ = true;
        }
        if (!view.settings_loaded)
            ImGui::TextColored(kError, "Settings file needs repair before saving.");
        else if (!view.generation_blocker.empty())
            ImGui::TextColored(kMuted, "%s", view.generation_blocker.c_str());
        ImGui::TextColored(kMuted, "Changes apply to new speech jobs after Save.");
    }

    void TtsEditorEditor::RenderTextInput(const TtsEditorView &view)
    {
        ImGui::TextColored(kCyan, "// TEXT INPUT");
        ImGui::Separator();
        if (focus_text_input_)
        {
            ImGui::SetKeyboardFocusHere();
            focus_text_input_ = false;
        }
        ImGui::InputTextMultiline("##speech_text", text_.data(), text_.size(),
                                  ImVec2(-1.0f, 130.0f));
        const bool text_focused = ImGui::IsItemFocused();
        ImGui::TextColored(kMuted, "%zu / 4096 bytes", std::strlen(text_.data()));
        const bool submit_shortcut = text_focused && ImGui::GetIO().KeyCtrl &&
            ImGui::IsKeyPressed(ImGuiKey_Enter);
        const bool can_submit = view.can_generate && text_[0] != '\0';
        ImGui::BeginDisabled(!can_submit);
        if (ImGui::Button("GENERATE SPEECH", ImVec2(-1.0f, 34.0f)) ||
            (can_submit && submit_shortcut))
            controller_->QueueGenerate(text_.data());
        ImGui::EndDisabled();
        const auto selected = std::find_if(view.entries.begin(), view.entries.end(),
            [&view](const TtsEntryView &entry) { return entry.id == view.selected_id; });
        const std::string *message = !view.error.empty() ? &view.error :
            selected != view.entries.end() && !selected->error.empty() ?
                &selected->error : nullptr;
        if (message)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, kError);
            ImGui::TextWrapped("%s", message->c_str());
            ImGui::PopStyleColor();
        }
        else if (!view.status.empty())
            ImGui::TextColored(kMuted, "%s", view.status.c_str());
        ImGui::Separator();
        ImGui::TextColored(kCyan, "// OUTPUT");
        ImGui::Text("Output folder: %s", output_directory_.data());
    }

    void TtsEditorEditor::RenderWorkspace()
    {
        if (!controller_) return;
        const TtsEditorView view = controller_->GetView();
        if (refresh_fields_ && view.status == "Settings reloaded")
        {
            CopySettingsToFields(view.settings);
            refresh_fields_ = false;
        }
        const ImGuiViewport *viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoTitleBar;
        if (ImGui::Begin("TTS Tool", nullptr, flags))
        {
            if (ui_->GetCodeFont()) ImGui::PushFont(ui_->GetCodeFont());
            ImGui::TextColored(kCyan, "// TTS TOOL");
            ImGui::Separator();
            const float width = ImGui::GetContentRegionAvail().x;
            if (width >= 900.0f)
            {
                if (ImGui::BeginTable("##workspace", 2, ImGuiTableFlags_Resizable))
                {
                    ImGui::TableSetupColumn("##content", ImGuiTableColumnFlags_WidthStretch, 0.69f);
                    ImGui::TableSetupColumn("##controls", ImGuiTableColumnFlags_WidthStretch, 0.31f);
                    ImGui::TableNextColumn();
                    RenderPreview(view);
                    ImGui::Spacing();
                    RenderDialogList(view);
                    ImGui::TableNextColumn();
                    RenderSettings(view);
                    ImGui::Spacing();
                    RenderTextInput(view);
                    ImGui::EndTable();
                }
            }
            else
            {
                RenderTextInput(view);
                ImGui::Spacing();
                RenderPreview(view);
                ImGui::Spacing();
                RenderDialogList(view);
                ImGui::Spacing();
                RenderSettings(view);
            }
            if (ui_->GetCodeFont()) ImGui::PopFont();
        }
        ImGui::End();
    }
}
