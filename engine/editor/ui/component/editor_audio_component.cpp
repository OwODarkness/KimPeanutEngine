#include "editor_audio_component.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>

#include <imgui.h>

#include "runtime/audio/audio_system.h"
#include "runtime/audio/miniaudio_audio_system.h"
#include "log/logger.h"

namespace kpengine::editor
{
    namespace
    {
        constexpr std::size_t BusIndex(audio::AudioBus bus)
        {
            return static_cast<std::size_t>(bus);
        }

        void DrawMeter(const char *label, float level, ImVec4 tint)
        {
            ImGui::TextUnformatted(label);
            ImGui::SameLine(100.0f);
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, tint);
            ImGui::ProgressBar(std::clamp(level, 0.0f, 1.0f), ImVec2(-1.0f, 12.0f), "");
            ImGui::PopStyleColor();
        }

        double CallbackMilliseconds(uint64_t nanoseconds)
        {
            return static_cast<double>(nanoseconds) / 1'000'000.0;
        }
    }

    EditorAudioComponent::EditorAudioComponent(audio::MiniAudioSystem *audio_system)
        : EditorWindowComponent("Audio Player"), audio_system_(audio_system)
    {
        if (audio_system_ != nullptr)
        {
            master_gain_ = audio_system_->GetMasterGain();
            speech_gain_ = audio_system_->GetBusGain(audio::AudioBus::Speech);
            music_gain_ = audio_system_->GetBusGain(audio::AudioBus::Music);
            speech_muted_ = audio_system_->IsBusMuted(audio::AudioBus::Speech);
            music_muted_ = audio_system_->IsBusMuted(audio::AudioBus::Music);
        }
    }

    void EditorAudioComponent::RenderContent()
    {
        EditorWindowComponent::RenderContent();
        if (audio_system_ == nullptr)
        {
            ImGui::TextDisabled("Runtime audio service is unavailable.");
            return;
        }

        const bool initialized = audio_system_->IsInitialized();
        ImGui::TextColored(initialized ? ImVec4(0.30f, 0.88f, 0.76f, 1.0f)
                                       : ImVec4(0.95f, 0.68f, 0.24f, 1.0f),
                           initialized ? "OUTPUT DEVICE ONLINE" : "OUTPUT DEVICE OFFLINE");
        ImGui::SameLine();
        if (!initialized)
        {
            if (ImGui::Button("Start Audio Device"))
            {
                initialization_failed_ = !audio_system_->Initialize();
                if (initialization_failed_)
                {
                    KP_LOG("Audio", LOG_LEVEL_ERROR,
                           "Could not initialize the playback device from Audio Player");
                }
            }
        }
        else if (ImGui::Button("Stop Audio Device"))
        {
            audio_system_->ShutDown();
        }

        if (initialization_failed_ && !audio_system_->IsInitialized())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.30f, 1.0f),
                               "Device initialization failed. See Output Log.");
        }

        ImGui::SeparatorText("OUTPUT");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::SliderFloat("Master", &master_gain_, 0.0f, 1.0f, "%.0f%%"))
        {
            audio_system_->SetMasterGain(master_gain_);
        }

        ImGui::SeparatorText("BUSES");
        ImGui::SetNextItemWidth(-90.0f);
        if (ImGui::SliderFloat("Speech", &speech_gain_, 0.0f, 1.0f, "%.0f%%"))
        {
            audio_system_->SetBusGain(audio::AudioBus::Speech, speech_gain_);
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("Mute##speech", &speech_muted_))
        {
            audio_system_->SetBusMuted(audio::AudioBus::Speech, speech_muted_);
        }

        ImGui::SetNextItemWidth(-90.0f);
        if (ImGui::SliderFloat("Music", &music_gain_, 0.0f, 1.0f, "%.0f%%"))
        {
            audio_system_->SetBusGain(audio::AudioBus::Music, music_gain_);
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("Mute##music", &music_muted_))
        {
            audio_system_->SetBusMuted(audio::AudioBus::Music, music_muted_);
        }

        ImGui::SeparatorText("DEVICE");
        const audio::AudioDeviceInfo device = audio_system_->GetDeviceInfo();
        if (device.initialized)
        {
            ImGui::Text("Name: %s", device.name.empty() ? "Default output" : device.name.c_str());
            ImGui::Text("Format: %u Hz / %u channels", device.sample_rate, device.channels);
            ImGui::Text("Estimated latency: %.1f ms", device.estimated_latency_ms);
        }
        else
        {
            ImGui::TextDisabled("Start the device to show output details.");
        }

        const audio::AudioTelemetrySnapshot telemetry = audio_system_->GetTelemetrySnapshot();
        ImGui::SeparatorText("MIXER TELEMETRY");
        ImGui::Text("Callbacks: %llu  |  Frames: %llu",
                    static_cast<unsigned long long>(telemetry.callback_count),
                    static_cast<unsigned long long>(telemetry.callback_frames));
        ImGui::Text("Max callback: %.3f ms  |  Limited frames: %llu",
                    CallbackMilliseconds(telemetry.max_callback_duration_ns),
                    static_cast<unsigned long long>(telemetry.callback_work_limited_frames));
        ImGui::Text("Stream overflow rejects: %llu  |  Command rejects: %llu",
                    static_cast<unsigned long long>(telemetry.stream_overflow_rejections),
                    static_cast<unsigned long long>(telemetry.control_command_rejections));
        const auto &speech = telemetry.buses[BusIndex(audio::AudioBus::Speech)];
        const auto &music = telemetry.buses[BusIndex(audio::AudioBus::Music)];
        ImGui::Text("Speech played frames: %llu  |  underrun blocks: %llu",
                    static_cast<unsigned long long>(speech.played_frames),
                    static_cast<unsigned long long>(speech.underrun_blocks));
        ImGui::Text("Music played frames: %llu  |  underrun blocks: %llu",
                    static_cast<unsigned long long>(music.played_frames),
                    static_cast<unsigned long long>(music.underrun_blocks));
        const auto sample_time = std::chrono::steady_clock::now();
        if (last_activity_sample_ != std::chrono::steady_clock::time_point{})
        {
            const double elapsed = std::chrono::duration<double>(
                sample_time - last_activity_sample_).count();
            if (elapsed > 0.0)
            {
                const double expected_frames = elapsed * 48000.0;
                const uint64_t speech_delta = speech.played_frames >= last_speech_frames_
                    ? speech.played_frames - last_speech_frames_ : 0;
                const uint64_t music_delta = music.played_frames >= last_music_frames_
                    ? music.played_frames - last_music_frames_ : 0;
                speech_activity_ = static_cast<float>(std::clamp(
                    static_cast<double>(speech_delta) / expected_frames,
                    0.0, 1.0));
                music_activity_ = static_cast<float>(std::clamp(
                    static_cast<double>(music_delta) / expected_frames,
                    0.0, 1.0));
            }
        }
        last_activity_sample_ = sample_time;
        last_speech_frames_ = speech.played_frames;
        last_music_frames_ = music.played_frames;
        DrawMeter("Speech activity", speech_muted_ ? 0.0f : speech_activity_,
                  ImVec4(0.10f, 0.78f, 0.96f, 1.0f));
        DrawMeter("Music activity", music_muted_ ? 0.0f : music_activity_,
                  ImVec4(0.51f, 0.49f, 1.0f, 1.0f));

        ImGui::Spacing();
        ImGui::TextDisabled("Detailed logs: Output Log  |  Runtime category: Audio");
    }
}
