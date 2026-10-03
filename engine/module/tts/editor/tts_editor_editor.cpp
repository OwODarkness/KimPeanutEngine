#include "tts_editor_editor.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <exception>
#include <optional>

#include <imgui.h>

#include "editor/ui/audio_transport_strip.h"
#include "editor/ui/editor_ui.h"
#include "runtime/engine.h"
#include "runtime/render/render_system.h"
#include "runtime/runtime_global_context.h"
#include "runtime/window/window_system.h"

namespace kpengine::tts_editor
{
    namespace
    {
        const ImVec4 kCyan{0.18f, 0.83f, 0.94f, 1.0f};
        const ImVec4 kMuted{0.58f, 0.7f, 0.75f, 1.0f};
        const ImVec4 kError{1.0f, 0.42f, 0.36f, 1.0f};

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
        CopySettingsToFields(controller.GetView().settings);
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
        ImGui::Text("%s // %s", selected->voice_name.c_str(),
                    selected->streaming ? "STREAM" : "BUFFER");
        ImGui::TextWrapped("%s", selected->text.c_str());
        if (!selected->error.empty())
            ImGui::TextColored(kError, "%s", selected->error.c_str());
        ImGui::Spacing();
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
                const auto action = editor::DrawTransportStrip(saved_transport);
                if (action.toggle_play_pause)
                    controller_->QueueTogglePlayPause();
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
        const auto action = editor::DrawTransportStrip(transport);
        if (action.toggle_play_pause) controller_->QueueTogglePlayPause();
        if (action.stop_voice) controller_->QueueStop();
        if (action.cancel_job) controller_->QueueCancel();
        if (action.seek_seconds) controller_->QueueSeek(*action.seek_seconds);
        const editor::EditorControlIcon voice_icon = transport_icons_.Get(
            view.volume > 0.001f ? editor::AudioTransportIcon::VoiceOpen :
                                   editor::AudioTransportIcon::VoiceClosed);
        const float volume_icon_size = 18.0f;
        const ImVec2 volume_origin = ImGui::GetCursorScreenPos();
        editor::DrawEditorControlIcon(voice_icon,
            {volume_origin.x, volume_origin.y + 2.0f}, volume_icon_size,
            IM_COL32(48, 211, 239, 255));
        ImGui::Dummy({volume_icon_size + 8.0f, ImGui::GetFrameHeight()});
        ImGui::SameLine();
        ImGui::SetNextItemWidth(std::max(100.0f,
            ImGui::GetContentRegionAvail().x - 92.0f));
        float volume = view.volume;
        if (ImGui::SliderFloat("Speech volume##tts", &volume, 0.0f, 1.0f, "%.2f"))
            controller_->QueueVolume(volume);
        ImGui::SameLine();
        ImGui::Text("%d%%", static_cast<int>(std::round(volume * 100.0f)));
        if (!selected->streaming && selected->player.IsValid())
        {
            ImGui::TextUnformatted("SPEECH SPEED");
            ImGui::SameLine();
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
            ImGui::SetNextItemWidth(110.0f);
            if (ImGui::Combo("##tts_preview_rate", &selected_rate, rate_labels,
                             6))
                controller_->QueuePlaybackRate(rates[selected_rate]);
        }
        if (selected->streaming)
            ImGui::TextColored(kMuted, "Stream preview: seek and rate unavailable.");
    }

    void TtsEditorEditor::RenderDialogList(const TtsEditorView &view)
    {
        ImGui::TextColored(kCyan, "// DIALOG LIST");
        ImGui::Separator();
        ImGui::SetNextItemWidth(-110.0f);
        ImGui::InputText("##import_wav", import_path_.data(), import_path_.size());
        ImGui::SameLine();
        if (ImGui::Button("IMPORT WAV")) controller_->QueueImport(import_path_.data());
        const auto selected = std::find_if(view.entries.begin(), view.entries.end(),
            [&view](const TtsEntryView &entry) { return entry.id == view.selected_id; });
        ImGui::BeginDisabled(selected == view.entries.end() || !selected->durable);
        if (ImGui::Button("DUPLICATE")) controller_->QueueDuplicate();
        ImGui::SameLine();
        if (ImGui::Button("DELETE")) controller_->QueueDelete();
        ImGui::EndDisabled();
        if (view.entries.empty())
        {
            ImGui::TextColored(kMuted, "No saved or generated dialogs.");
            return;
        }
        if (ImGui::BeginTable("##dialogs", 4,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
            ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
            ImVec2(0.0f, 220.0f)))
        {
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 44.0f);
            ImGui::TableSetupColumn("Text", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Voice", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableHeadersRow();
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(view.entries.size()));
            while (clipper.Step())
            {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
                {
                    const auto &entry = view.entries[static_cast<std::size_t>(row)];
                    ImGui::PushID(static_cast<int>(entry.id));
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    if (ImGui::Selectable("##entry", entry.id == view.selected_id,
                                          ImGuiSelectableFlags_SpanAllColumns))
                        controller_->QueueSelect(entry.id);
                    ImGui::SameLine();
                    ImGui::Text("%03llu", static_cast<unsigned long long>(entry.id));
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(entry.text.c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextUnformatted(entry.voice_name.c_str());
                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextUnformatted(JobLabel(entry.state));
                    ImGui::PopID();
                }
            }
            ImGui::EndTable();
        }
        if (selected != view.entries.end() && selected->durable)
        {
            if (export_basename_[0] == '\0')
                std::snprintf(export_basename_.data(), export_basename_.size(),
                              "speech-%llu", static_cast<unsigned long long>(selected->id));
            ImGui::SetNextItemWidth(-110.0f);
            ImGui::InputText("##export_basename", export_basename_.data(), export_basename_.size());
            ImGui::SameLine();
            if (ImGui::Button("EXPORT WAV")) controller_->QueueExport(export_basename_.data());
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
